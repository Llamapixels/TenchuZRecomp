// tenchuz - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/ppc/context.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/user_module.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/xmemory.h>
#include <rex/ui/keybinds.h>

#include "cheat_menu.h"
#include "controller.h"
#include "gameplay.h"
#include "mod_loader.h"
#include "photo_mode.h"
#include "ui_settings.h"
#include "upnp.h"
#include "version.h"

namespace {

rex::memory::Memory* g_guest_memory = nullptr;  // set in OnPostSetup

// Guest address ranges that the runtime keeps committed for the whole session
// (heap, thread stacks, loaded image). Memory::TranslateVirtual performs no
// validation, so every diagnostic read must be gated on one of these ranges:
// a nested fault inside the crash handler would be fatal.
bool GuestReadable(uint32_t addr) {
  return (addr >= 0x30000000u && addr < 0x60000000u) ||  // guest heap
         (addr >= 0x70000000u && addr < 0x72000000u) ||  // guest thread stacks
         (addr >= 0x82000000u && addr < 0x83440000u);    // loaded XEX image
}

bool ReadGuestU32(uint32_t addr, uint32_t* out) {
  if (!g_guest_memory || !GuestReadable(addr)) {
    return false;
  }
  const uint8_t* p = g_guest_memory->TranslateVirtual<const uint8_t*>(addr);
  std::memcpy(out, p, sizeof(*out));
  return true;
}

uint32_t ReadGuestCodeWord(uint32_t addr) {
  uint32_t v = 0;
  ReadGuestU32(addr, &v);
  return v;
}

// Walk the guest PowerPC stack back-chain (0(r1)) and, for every frame, report
// the first code-range words found in the frame body. Those are saved LR /
// return-address slots, which identify the caller chain that led to the fault.
void LogGuestStack(PPCContext* ctx) {
  constexpr uint32_t kCodeLo = 0x82270000u;
  constexpr uint32_t kCodeHi = 0x82829DB8u;
  uint32_t fp = static_cast<uint32_t>(ctx->r1.u64);
  for (int frame = 0; frame < 32; ++frame) {
    if (fp < 0x10000u) {
      break;
    }
    uint32_t chain = 0;
    const bool have_chain = ReadGuestU32(fp, &chain);
    char hits[256];
    int n = 0;
    hits[0] = '\0';
    int found = 0;
    for (uint32_t off = 4; off < 0x100u && found < 4; off += 4) {
      uint32_t v = 0;
      if (!ReadGuestU32(fp + off, &v)) {
        break;
      }
      if (v >= kCodeLo && v < kCodeHi) {
        n += std::snprintf(hits + n, sizeof(hits) - static_cast<size_t>(n),
                           " [sp+%#x]=%#010x", off, v);
        ++found;
      }
    }
    REXLOG_ERROR("[crash] stack[{}] sp={:#010x} back={:#010x}{}", frame, fp,
                 have_chain ? chain : 0u, hits);
    if (!have_chain || chain <= fp || chain - fp > 0x100000u) {
      break;
    }
    fp = chain;
  }
}

// Raw little-endian word dump of a guest range, annotating each word that looks
// like a code / thread-stack / heap pointer. Used to reconstruct the frame that
// the faulting function (or its callees) left on the guest stack when we are
// chasing a corrupted callee-saved register (r31) or a drifted r1.
void LogGuestRange(const char* tag, uint32_t addr, uint32_t len) {
  constexpr uint32_t kCodeLo = 0x82270000u;
  constexpr uint32_t kCodeHi = 0x82829DB8u;
  for (uint32_t off = 0; off < len; off += 32) {
    char line[320];
    int n = std::snprintf(line, sizeof(line), "[crash] %s %#010x:", tag,
                          addr + off);
    for (uint32_t i = 0; i < 32; i += 4) {
      uint32_t v = 0;
      if (n >= static_cast<int>(sizeof(line)) - 12) {
        break;
      }
      if (!ReadGuestU32(addr + off + i, &v)) {
        n += std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n),
                           " ????????");
        continue;
      }
      const char mark = (v >= kCodeLo && v < kCodeHi)             ? 'C'
                        : (v >= 0x70000000u && v < 0x72000000u)   ? 'S'
                        : (v >= 0x30000000u && v < 0x60000000u)   ? 'H'
                        : (v >= 0x82000000u && v < 0x83440000u)   ? 'I'
                                                                  : ' ';
      n += std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n),
                         " %08x%c", v, mark);
    }
    REXLOG_ERROR("{}", line);
  }
}

// ReXGlue lowers every guest call to a real host call, so the *host* stack
// carries the exact guest call chain independently of the guest r1 (which may
// itself be corrupted by whatever we are chasing). Scan upward from the fault
// RSP for values that fall inside the image's code section and log them as
// RVAs; tools/pdb_publics.py resolve turns them into function names.
void LogHostStack(EXCEPTION_POINTERS* info) {
  const uintptr_t base =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const uintptr_t code_lo = base + nt->OptionalHeader.BaseOfCode;
  const uintptr_t code_hi = code_lo + nt->OptionalHeader.SizeOfCode;
  const uintptr_t rsp = static_cast<uintptr_t>(info->ContextRecord->Rsp);
  const uintptr_t end = rsp + 0x8000;
  char hits[1024];
  int n = 0;
  hits[0] = '\0';
  int found = 0;
  uintptr_t p = rsp;
  while (p + sizeof(uintptr_t) <= end && found < 40) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(p), &mbi, sizeof(mbi)) == 0) {
      break;
    }
    const uintptr_t region_end =
        reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
      break;
    }
    const uintptr_t stop = region_end < end ? region_end : end;
    for (; p + sizeof(uintptr_t) <= stop; p += sizeof(uintptr_t)) {
      uintptr_t v = 0;
      std::memcpy(&v, reinterpret_cast<const void*>(p), sizeof(v));
      if (v >= code_lo && v < code_hi) {
        n += std::snprintf(hits + n, sizeof(hits) - static_cast<size_t>(n),
                           " %#x", static_cast<unsigned int>(v - base));
        ++found;
      }
    }
  }
  REXLOG_ERROR("[crash] host stack return candidates (rva, outer last):{}", hits);
  REXLOG_ERROR("[crash] host rsp={:#x} code=[{:#x}..{:#x})", rsp,
               code_lo - base, code_hi - base);
}

// Crash diagnostics (Milestone D): log the faulting host RIP and the bound
// guest context on any access violation, then let the runtime's own handler do
// its thing. tools/resolve_rip.py maps the reported RIP to the recompiled
// function using the linker map emitted by CMake (LINKER:/MAP).
LONG WINAPI TenchuzCrashHandler(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const uintptr_t rip = static_cast<uintptr_t>(info->ContextRecord->Rip);
  const uintptr_t rsp = static_cast<uintptr_t>(info->ContextRecord->Rsp);
  const uintptr_t addr = info->ExceptionRecord->ExceptionInformation[1];
  const uintptr_t image_base =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const uintptr_t rip_rva = rip - image_base;
  auto* ts = rex::runtime::ThreadState::Get();
  if (!ts) {
    REXLOG_ERROR("[crash] AV rip={:#x} (rva={:#x}) rsp={:#x} bad_addr={:#x} "
                 "(no bound guest thread)",
                 rip, rip_rva, rsp, addr);
    return EXCEPTION_CONTINUE_SEARCH;
  }
  auto* ctx = ts->context();
  const uint32_t lr = static_cast<uint32_t>(ctx->lr);
  REXLOG_ERROR(
      "[crash] AV rip={:#x} (rva={:#x}) rsp={:#x} bad_addr={:#x} | guest "
      "lr={:#010x} ctr={:#010x} r1={:#010x} r3={:#010x} last_indirect={:#010x}",
      rip, rip_rva, rsp, addr, lr, static_cast<uint32_t>(ctx->ctr.u64),
      static_cast<uint32_t>(ctx->r1.u64), static_cast<uint32_t>(ctx->r3.u64),
      ctx->last_indirect_target);
  // The instruction before LR is the call into the faulting function; decode it
  // offline to recover the callee (bl target) or the indirect target register.
  // A zero LR means the function was reached by tail-calls from a thread entry,
  // in which case the full GPR dump below is what identifies the fault site.
  const uint32_t call_site = lr - 4;
  REXLOG_ERROR("[crash] guest call-site {:#010x}: [{:#010x}] [{:#010x}]",
               call_site, ReadGuestCodeWord(call_site - 4),
               ReadGuestCodeWord(call_site));
  REXLOG_ERROR("[crash] gpr r0={:#010x} r1={:#010x} r2={:#010x} r3={:#010x} "
               "r4={:#010x} r5={:#010x} r6={:#010x} r7={:#010x} r8={:#010x} "
               "r9={:#010x} r10={:#010x} r11={:#010x} r12={:#010x} r13={:#010x} "
               "r14={:#010x} r15={:#010x}",
               static_cast<uint32_t>(ctx->r0.u64),
               static_cast<uint32_t>(ctx->r1.u64),
               static_cast<uint32_t>(ctx->r2.u64),
               static_cast<uint32_t>(ctx->r3.u64),
               static_cast<uint32_t>(ctx->r4.u64),
               static_cast<uint32_t>(ctx->r5.u64),
               static_cast<uint32_t>(ctx->r6.u64),
               static_cast<uint32_t>(ctx->r7.u64),
               static_cast<uint32_t>(ctx->r8.u64),
               static_cast<uint32_t>(ctx->r9.u64),
               static_cast<uint32_t>(ctx->r10.u64),
               static_cast<uint32_t>(ctx->r11.u64),
               static_cast<uint32_t>(ctx->r12.u64),
               static_cast<uint32_t>(ctx->r13.u64),
               static_cast<uint32_t>(ctx->r14.u64),
               static_cast<uint32_t>(ctx->r15.u64));
  REXLOG_ERROR("[crash] gpr r16={:#010x} r17={:#010x} r18={:#010x} "
               "r19={:#010x} r20={:#010x} r21={:#010x} r22={:#010x} "
               "r23={:#010x} r24={:#010x} r25={:#010x} r26={:#010x} "
               "r27={:#010x} r28={:#010x} r29={:#010x} r30={:#010x} r31={:#010x}",
               static_cast<uint32_t>(ctx->r16.u64),
               static_cast<uint32_t>(ctx->r17.u64),
               static_cast<uint32_t>(ctx->r18.u64),
               static_cast<uint32_t>(ctx->r19.u64),
               static_cast<uint32_t>(ctx->r20.u64),
               static_cast<uint32_t>(ctx->r21.u64),
               static_cast<uint32_t>(ctx->r22.u64),
               static_cast<uint32_t>(ctx->r23.u64),
               static_cast<uint32_t>(ctx->r24.u64),
               static_cast<uint32_t>(ctx->r25.u64),
               static_cast<uint32_t>(ctx->r26.u64),
               static_cast<uint32_t>(ctx->r27.u64),
               static_cast<uint32_t>(ctx->r28.u64),
               static_cast<uint32_t>(ctx->r29.u64),
               static_cast<uint32_t>(ctx->r30.u64),
               static_cast<uint32_t>(ctx->r31.u64));
  LogGuestStack(ctx);
  {
    // Frame forensics: dump around r1 and every GPR that points into the guest
    // thread-stack range, so the (possibly drifted) frame layout is visible.
    const uint32_t r1 = static_cast<uint32_t>(ctx->r1.u64);
    if (r1 >= 0x70000000u && r1 < 0x72000000u) {
      LogGuestRange("r1-", r1 - 0x100u, 0x300u);
    }
    const uint32_t gprs[32] = {
        static_cast<uint32_t>(ctx->r0.u64),  static_cast<uint32_t>(ctx->r1.u64),
        static_cast<uint32_t>(ctx->r2.u64),  static_cast<uint32_t>(ctx->r3.u64),
        static_cast<uint32_t>(ctx->r4.u64),  static_cast<uint32_t>(ctx->r5.u64),
        static_cast<uint32_t>(ctx->r6.u64),  static_cast<uint32_t>(ctx->r7.u64),
        static_cast<uint32_t>(ctx->r8.u64),  static_cast<uint32_t>(ctx->r9.u64),
        static_cast<uint32_t>(ctx->r10.u64), static_cast<uint32_t>(ctx->r11.u64),
        static_cast<uint32_t>(ctx->r12.u64), static_cast<uint32_t>(ctx->r13.u64),
        static_cast<uint32_t>(ctx->r14.u64), static_cast<uint32_t>(ctx->r15.u64),
        static_cast<uint32_t>(ctx->r16.u64), static_cast<uint32_t>(ctx->r17.u64),
        static_cast<uint32_t>(ctx->r18.u64), static_cast<uint32_t>(ctx->r19.u64),
        static_cast<uint32_t>(ctx->r20.u64), static_cast<uint32_t>(ctx->r21.u64),
        static_cast<uint32_t>(ctx->r22.u64), static_cast<uint32_t>(ctx->r23.u64),
        static_cast<uint32_t>(ctx->r24.u64), static_cast<uint32_t>(ctx->r25.u64),
        static_cast<uint32_t>(ctx->r26.u64), static_cast<uint32_t>(ctx->r27.u64),
        static_cast<uint32_t>(ctx->r28.u64), static_cast<uint32_t>(ctx->r29.u64),
        static_cast<uint32_t>(ctx->r30.u64), static_cast<uint32_t>(ctx->r31.u64)};
    for (int i = 0; i < 32; ++i) {
      const uint32_t v = gprs[i];
      if (v >= 0x70000000u && v < 0x72000000u && v != r1) {
        char tag[16];
        std::snprintf(tag, sizeof(tag), "r%d-", i);
        LogGuestRange(tag, v - 0x40u, 0x80u);
      }
    }
  }
  LogHostStack(info);
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

class TenchuzApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    WaitForRestartingInstance();
    return std::unique_ptr<TenchuzApp>(new TenchuzApp(ctx, "tenchuz",
        PPCImageConfig));
  }

  // F6 > Shader > Restart now starts this process with TENCHUZ_RESTART_FROM
  // set to the old process's ID. Wait for it to exit first, so the two never
  // share the log, settings or shader cache files.
  static void WaitForRestartingInstance() {
    wchar_t pid_text[32];
    const DWORD n = GetEnvironmentVariableW(L"TENCHUZ_RESTART_FROM", pid_text,
                                            32);
    if (n == 0 || n >= 32) {
      return;
    }
    SetEnvironmentVariableW(L"TENCHUZ_RESTART_FROM", nullptr);
    const DWORD pid = wcstoul(pid_text, nullptr, 10);
    if (HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
      WaitForSingleObject(old, 15000);
      CloseHandle(old);
    }
  }

  // Register the crash handler as early as possible.
  void OnPostInitLogging() override {
    AddVectoredExceptionHandler(1, &TenchuzCrashHandler);
    KeepFullSpeedInBackground();
  }

  // Windows 11 throttles windows that are not in front: less CPU, and their
  // timer requests are ignored (15.6 ms steps). The game paces its frames
  // with timers, so a host whose window was in the background (alt-tabbed,
  // or the second copy on one PC) sent its System Link updates in bursts
  // and enemies stuttered for the other players. Ask for a 1 ms timer and
  // opt out of the throttling.
  static void KeepFullSpeedInBackground() {
    timeBeginPeriod(1);
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif
    PROCESS_POWER_THROTTLING_STATE state = {};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                        PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    state.StateMask = 0;  // throttling off for both
    const BOOL ok = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                                          &state, sizeof(state));
    REXLOG_INFO("Background throttling off: {}", ok ? "yes" : "no");
  }

  // Cache the guest memory for the crash handler and cheat menu, and mount
  // mods. This runs after the runtime has set up the file system but before
  // the game's first instruction, so every file the game opens sees the mods.
  void OnPostSetup() override {
    g_guest_memory = runtime()->memory();
    tenchuz::gameplay::SetMemory(g_guest_memory);
    tenchuz::controller::LoadFromConfig();
    if (cheat_menu_) {
      cheat_menu_->SetMemory(runtime()->memory());
    }
    mods_ = tenchuz::InstallMods(runtime()->file_system(), game_root_,
                                 exe_dir_ / "mods");
    if (cheat_menu_) {
      cheat_menu_->SetMods(mods_);
    }
    // Forward the System Link ports on the router (F6 > Online), in the
    // background so start-up does not wait for the router.
    if (tenchuz::upnp::enabled()) {
      tenchuz::upnp::Start();
    }
  }

  // Close the router ports opened at start-up. The runtime hard-exits once
  // the window closes (OnShutdown never runs then), so this happens when the
  // close is requested; OnShutdown covers the other exits.
  bool OnWindowCloseRequested() override {
    tenchuz::upnp::Stop();
    return true;
  }
  void OnShutdown() override { tenchuz::upnp::Stop(); }

  // Cheat / memory scanner overlay (F6).
  //
  // The dialog is created here and deliberately never destroyed: ImGuiDrawer
  // draws every registered dialog each frame, and that is the only per-frame
  // callback available to the app. Destroying the dialog on a second F6 press
  // would stop the pin from being re-applied, silently breaking the cheat while
  // the menu was closed. Toggle() only shows/hides the window instead.
  //
  // Note the runtime is not necessarily constructed yet at this point, so the
  // Memory pointer may be null here; OnPostSetup fills it in.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    // The window exists by now; the runtime titles it with its own name and
    // build stamp.
    if (window()) {
      window()->SetTitle(TENCHUZ_PROJECT_NAME " - v" TENCHUZ_VERSION);
    }
    cheat_menu_ = std::make_unique<tenchuz::CheatMenuDialog>(
        drawer, runtime() ? runtime()->memory() : nullptr);
    cheat_menu_->SetExeDir(exe_dir_);
    // Same path as the window's close button: the runtime shuts the title
    // down and exits.
    cheat_menu_->SetExitHandler([this] {
      tenchuz::upnp::Stop();
      if (window()) {
        window()->RequestClose();
      }
    });
    cheat_menu_->SetMods(mods_);
    rex::ui::RegisterBind("bind_cheat_menu", "F6", "Toggle Tenchu Z menu", [this] {
      if (cheat_menu_) {
        cheat_menu_->Toggle();
      }
    });
    rex::ui::RegisterBind("bind_god_mode", "F8", "Toggle god mode", [] {
      tenchuz::gameplay::set_god_mode(!tenchuz::gameplay::god_mode());
    });
    rex::ui::RegisterBind("bind_refill_health", "F5", "Refill health", [] {
      if (!tenchuz::gameplay::RefillLocalPlayer(g_guest_memory)) {
        REXLOG_INFO("refill health: no living player character");
      }
    });
    rex::ui::RegisterBind("bind_mouse_look", "F9", "Pause/resume mouse look",
                          [this] {
                            if (cheat_menu_) {
                              cheat_menu_->ToggleMouseLookPause();
                            }
                          });
    rex::ui::RegisterBind("bind_save_position", "F1", "Save position", [this] {
      if (cheat_menu_) {
        cheat_menu_->SavePosition();
      }
    });
    rex::ui::RegisterBind("bind_load_position", "F2", "Load position", [this] {
      if (cheat_menu_) {
        cheat_menu_->LoadPosition();
      }
    });
    rex::ui::RegisterBind("bind_photo_mode", "F10", "Photo mode", [] {
      tenchuz::photo_mode::Toggle();
    });
    REXLOG_INFO("Tenchu Z menu: F6 menu, F8 god mode, F5 refill health, "
                "F9 mouse look, F1/F2 save/load position, F10 photo mode");
  }

  // Select the GPU emulation plugin by default. The SDK reads the
  // `gpu_plugin` cvar (empty by default) and loads rexgpu-<name>.dll from the
  // executable's folder; GPU_PLUGINS xenos in CMakeLists.txt stages it there.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
    SetUpShaderOverrides(config);
  }

  // Shader overrides (runtime: src/graphics/d3d12/shader_override.h): HLSL
  // replacements for the game's pixel shaders, named <ucode hash>.ps.hlsl, in
  // shader_overrides\ next to the exe and in each enabled mod's shaders\
  // folder (mods first, in the same priority order as their files).
  //
  // The runtime's settings for this live in the GPU plugin, which registers
  // them when it loads - normally right after this hook. So when overrides
  // exist, the plugin is loaded here instead, and the settings applied:
  // the override folders, and bindful resource binding, which overrides need.
  void SetUpShaderOverrides(rex::RuntimeConfig& config) {
    if (!rex::cvar::Query<bool>("shader_overrides_enabled")) {
      tenchuz::shader_settings::Init(exe_dir_, {});
      return;
    }
    std::vector<std::filesystem::path> roots;
    std::error_code ec;
    auto has_hlsl = [&](const std::filesystem::path& dir) {
      if (!std::filesystem::is_directory(dir, ec)) {
        return false;
      }
      for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".hlsl") {
          return true;
        }
      }
      return false;
    };
    // The SDK uses the first folder that has a shader's file, so the
    // player's own shader_overrides folder comes before any mod.
    if (std::filesystem::is_directory(exe_dir_ / "shader_overrides", ec)) {
      roots.push_back(exe_dir_ / "shader_overrides");
    }
    if (rex::cvar::Query<bool>("mods_enabled")) {
      std::vector<std::filesystem::path> mods;
      for (const auto& entry :
           std::filesystem::directory_iterator(exe_dir_ / "mods", ec)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory() && !name.empty() && name[0] != '_' &&
            name[0] != '.') {
          mods.push_back(entry.path());
        }
      }
      std::sort(mods.rbegin(), mods.rend());  // later in the alphabet wins
      for (const auto& mod : mods) {
        if (std::filesystem::is_directory(mod / "shaders", ec)) {
          roots.push_back(mod / "shaders");
        }
      }
    }
    // Each root can hold preset subfolders; the active preset's folders
    // come first (shader_settings.h).
    tenchuz::shader_settings::Init(exe_dir_, roots);
    std::vector<std::filesystem::path> dirs;
    for (const auto& dir : tenchuz::shader_settings::OverrideDirs()) {
      if (has_hlsl(dir)) {
        dirs.push_back(dir);
      }
    }
    if (dirs.empty()) {
      return;
    }
    config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin);
    if (!config.graphics) {
      return;  // ReXApp reports the failure when it retries
    }
    std::string list;
    for (const auto& dir : dirs) {
      list += (list.empty() ? "" : ";") + dir.string();
    }
    // The renderer stays in its default (bindless) resource mode: the bindful
    // mode breaks some of this game's textures (purple floors in mission 1).
    const bool dir_set = rex::cvar::SetFlagByName("shader_override_dir", list);
    REXLOG_INFO("shader overrides: {} folder(s) [{}]{}", dirs.size(), list,
                dir_set ? "" : " - runtime does not support them");
  }

  // Resolve the game data directory when it was not supplied on the command
  // line, so that tenchuz.exe can simply be double-clicked.
  //
  // `game_data_root` is normally passed as --game_data_root="...". ReXApp reads
  // that cvar in SetupEnvironment() *before* it loads <exe>.toml, so the value
  // cannot come from the config file - this hook is the only place a fallback
  // can be applied. The SDK calls it after the cvar read but before the
  // non-empty / is-directory validation in ConstructRuntime().
  //
  // Search order (first hit wins):
  //   1. --game_data_root (untouched)
  //   2. <exe_dir>\extracted            - the packaged layout
  //   3. <exe_dir>\..\..\..\..\extracted - running from the build tree
  //
  // Deliberately relative to the executable rather than an absolute path, so a
  // packaged build keeps working after the folder is moved or copied.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    // PathConfig has no exe_dir member, but config_path is always
    // <exe_dir>/<name>.toml (set in ReXApp::SetupEnvironment), so its parent
    // is the executable's directory.
    exe_dir_ = paths.config_path.parent_path();
    game_root_ = paths.game_data_root;
    if (!game_root_.empty() || paths.config_path.empty()) {
      return;  // explicit --game_data_root wins
    }
    const std::filesystem::path& exe_dir = exe_dir_;
    // exe_dir is <game root>\tenchuz-recomp\out\build\win-amd64-release, so the
    // game root is four levels up from it.
    const std::filesystem::path candidates[] = {
        exe_dir / "extracted",
        exe_dir / ".." / ".." / ".." / ".." / "extracted",
    };
    std::error_code ec;
    for (const auto& candidate : candidates) {
      if (std::filesystem::is_directory(candidate, ec) && !ec) {
        paths.game_data_root = std::filesystem::absolute(candidate, ec);
        game_root_ = paths.game_data_root;
        REXLOG_INFO("game_data_root not specified; using {}",
                    paths.game_data_root.string());
        return;
      }
    }
  }

  // Mute while the window is in the background, if enabled. The runtime's
  // own audio_mute setting is left as the player had it when focus returns.
  void OnWindowFocusChanged(bool focused) override {
    if (!tenchuz::ui_settings::mute_when_unfocused()) {
      return;
    }
    if (!focused) {
      muted_before_unfocus_ = rex::cvar::Query<bool>("audio_mute");
      rex::cvar::SetFlagByName("audio_mute", "true");
    } else {
      rex::cvar::SetFlagByName("audio_mute",
                               muted_before_unfocus_ ? "true" : "false");
    }
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;

 private:
  std::unique_ptr<tenchuz::CheatMenuDialog> cheat_menu_;
  std::filesystem::path exe_dir_;
  std::filesystem::path game_root_;
  std::vector<std::string> mods_;
  bool muted_before_unfocus_ = false;
};
