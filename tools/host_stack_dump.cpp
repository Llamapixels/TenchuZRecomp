// tools/host_stack_dump.cpp
//
// Launch the recompiled game as a debuggee, let it run for N seconds, then
// break in and dump the HOST call stack of every thread, symbolized with the
// game PDB. Recompiled guest functions carry their guest address in the
// function name (DEFINE_REX_FUNC(sub_82296478) etc.), so a host stack frame
// sequence IS the guest call chain, and runtime frames show exactly which
// kernel wait a blocked thread is sitting in.
//
// Build (from a VS x64 developer prompt):
//   cl /nologo /EHsc /O2 tools\host_stack_dump.cpp /Fe:tools\host_stack_dump.exe \
//      /link dbghelp.lib
// Usage:
//   host_stack_dump.exe <path\to\tenchuz.exe> [workdir] [seconds=8]
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <string.h>

static HANDLE g_proc = nullptr;

static void DumpThread(DWORD tid) {
  HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                                 THREAD_QUERY_INFORMATION,
                             FALSE, tid);
  if (!thread) return;
  if (SuspendThread(thread) == (DWORD)-1) { CloseHandle(thread); return; }

  CONTEXT ctx;
  ZeroMemory(&ctx, sizeof(ctx));
  ctx.ContextFlags = CONTEXT_FULL;
  if (!GetThreadContext(thread, &ctx)) {
    ResumeThread(thread); CloseHandle(thread); return;
  }

  STACKFRAME64 sf;
  ZeroMemory(&sf, sizeof(sf));
  sf.AddrPC.Offset = ctx.Rip;
  sf.AddrPC.Mode = AddrModeFlat;
  sf.AddrFrame.Offset = ctx.Rbp;
  sf.AddrFrame.Mode = AddrModeFlat;
  sf.AddrStack.Offset = ctx.Rsp;
  sf.AddrStack.Mode = AddrModeFlat;

  printf("---- tid %lu  rsp=%llX ----\n", tid, (unsigned long long)ctx.Rsp);
  for (int i = 0; i < 80; ++i) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, g_proc, thread, &sf, &ctx,
                     nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                     nullptr)) {
      break;
    }
    if (sf.AddrPC.Offset == 0) break;

    char symbuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    SYMBOL_INFO* si = (SYMBOL_INFO*)symbuf;
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = MAX_SYM_NAME;
    DWORD64 disp = 0;
    char mod[64] = "?";
    DWORD64 mbase = SymGetModuleBase64(g_proc, sf.AddrPC.Offset);
    if (mbase) {
      char path[MAX_PATH];
      HMODULE hm = (HMODULE)mbase;
      if (GetModuleFileNameA(hm, path, MAX_PATH)) {
        const char* base = path;
        for (const char* p = path; *p; ++p)
          if (*p == '\\' || *p == '/') base = p + 1;
        snprintf(mod, sizeof(mod), "%s", base);
      }
    }
    if (SymFromAddr(g_proc, sf.AddrPC.Offset, &disp, si)) {
      printf("  %016llX  %s!%s+0x%llX\n", (unsigned long long)sf.AddrPC.Offset,
             mod, si->Name, (unsigned long long)disp);
    } else {
      printf("  %016llX  %s!+0x%llX\n", (unsigned long long)sf.AddrPC.Offset,
             mod, (unsigned long long)(sf.AddrPC.Offset - mbase));
    }
  }
  ResumeThread(thread);
  CloseHandle(thread);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("usage: host_stack_dump.exe <exe> [workdir] [seconds]\n");
    return 2;
  }
  const char* exe = argv[1];
  const char* cwd = argc > 2 ? argv[2] : nullptr;
  int seconds = argc > 3 ? atoi(argv[3]) : 8;

  char cmdline[MAX_PATH * 2];
  snprintf(cmdline, sizeof(cmdline), "\"%s\"", exe);

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  if (!CreateProcessA(exe, cmdline, nullptr, nullptr, FALSE,
                      DEBUG_ONLY_THIS_PROCESS, nullptr, cwd, &si, &pi)) {
    printf("CreateProcess failed: %lu\n", GetLastError());
    return 1;
  }
  g_proc = pi.hProcess;
  printf("launched pid %lu, running %ds under debug...\n",
         pi.dwProcessId, seconds);

  // Drain debug events while the game starts up.
  DWORD deadline = GetTickCount() + seconds * 1000;
  for (;;) {
    DWORD remaining = GetTickCount() < deadline ? deadline - GetTickCount() : 0;
    DEBUG_EVENT ev;
    if (!WaitForDebugEvent(&ev, remaining > 200 ? 200 : remaining)) break;
    if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
      printf("game exited (%lu) before the sampling point\n",
             ev.u.ExitProcess.dwExitCode);
      return 3;
    }
    ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
    if (GetTickCount() >= deadline) break;
  }

  // Load symbols for every module of the live process.
  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
  if (!SymInitialize(g_proc, NULL, TRUE)) {
    printf("SymInitialize failed: %lu\n", GetLastError());
  }

  // Break all threads in.
  if (!DebugBreakProcess(g_proc)) {
    printf("DebugBreakProcess failed: %lu\n", GetLastError());
  }
  for (;;) {
    DEBUG_EVENT ev;
    if (!WaitForDebugEvent(&ev, 2000)) break;
    BOOL cont = TRUE;
    if (ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
        (ev.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT ||
         ev.u.Exception.ExceptionRecord.ExceptionCode ==
             0x80000003L)) {
      cont = FALSE;  // stop the world here
    }
    if (!cont) {
      // give other threads a moment to reach their debug stops
      Sleep(100);
      break;
    }
    ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
  }

  printf("=== thread stacks at +%ds ===\n", seconds);
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 te;
  te.dwSize = sizeof(te);
  if (Thread32First(snap, &te)) {
    do {
      if (te.th32OwnerProcessID == pi.dwProcessId) DumpThread(te.th32ThreadID);
    } while (Thread32Next(snap, &te));
  }
  CloseHandle(snap);
  fflush(stdout);

  TerminateProcess(g_proc, 0);
  WaitForSingleObject(pi.hProcess, 3000);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return 0;
}
