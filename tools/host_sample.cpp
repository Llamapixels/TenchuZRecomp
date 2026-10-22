// tools/host_sample.cpp
//
// Non-invasive sampler: launches the recompiled game WITHOUT a debugger (so
// timing is real), lets it run N seconds, then samples the instruction pointer
// of every thread repeatedly. The hottest thread is the one burning CPU; the
// distinct RIPs map (through tenchuz.pdb line info) to the generated C++ line,
// and the generated sources keep "// <ppc insn>" comments, so the sampled
// instruction pinpoints the guest spin loop.
//
// Build (VS x64 environment):
//   cl /nologo /EHsc /O2 tools\host_sample.cpp /Fe:tools\host_sample.exe /link dbghelp.lib
// Usage:
//   host_sample.exe <tenchuz.exe> <workdir> <seconds> [game_data_root]

#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <string>

static HANDLE g_proc = nullptr;

struct ThreadInfo {
  DWORD tid;
  double cpu;
};

static const char* BaseName(const char* path) {
  const char* base = path;
  for (const char* p = path; *p; ++p)
    if (*p == '\\' || *p == '/') base = p + 1;
  return base;
}

static void SymFor(DWORD64 addr, char* out, size_t outsz) {
  char symbuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
  SYMBOL_INFO* si = (SYMBOL_INFO*)symbuf;
  si->SizeOfStruct = sizeof(SYMBOL_INFO);
  si->MaxNameLen = MAX_SYM_NAME;
  DWORD64 disp = 0;

  char modname[64] = "?";
  IMAGEHLP_MODULE64 mi;
  ZeroMemory(&mi, sizeof(mi));
  mi.SizeOfStruct = sizeof(mi);
  DWORD64 mbase = SymGetModuleBase64(g_proc, addr);
  if (SymGetModuleInfo64(g_proc, addr, &mi)) {
    snprintf(modname, sizeof(modname), "%s", mi.ModuleName);
  }

  char linepart[256] = "";
  IMAGEHLP_LINE64 line;
  ZeroMemory(&line, sizeof(line));
  line.SizeOfStruct = sizeof(line);
  DWORD lineDisp = 0;
  if (SymGetLineFromAddr64(g_proc, addr, &lineDisp, &line)) {
    snprintf(linepart, sizeof(linepart), "  %s:%lu", BaseName(line.FileName),
             line.LineNumber);
  }

  if (SymFromAddr(g_proc, addr, &disp, si)) {
    snprintf(out, outsz, "%s!%s+0x%llX%s", modname, si->Name,
             (unsigned long long)disp, linepart);
  } else {
    snprintf(out, outsz, "%s!+0x%llX%s", modname,
             (unsigned long long)(addr - mbase), linepart);
  }
}

static bool SampleThread(DWORD tid, CONTEXT* ctx) {
  HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                            THREAD_QUERY_INFORMATION,
                        FALSE, tid);
  if (!t) return false;
  bool ok = false;
  if (SuspendThread(t) != (DWORD)-1) {
    ctx->ContextFlags = CONTEXT_CONTROL;
    ok = GetThreadContext(t, ctx) != FALSE;
    ResumeThread(t);
  }
  CloseHandle(t);
  return ok;
}

static void WalkStack(DWORD tid, const CONTEXT& start) {
  HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                                 THREAD_QUERY_INFORMATION,
                             FALSE, tid);
  if (!thread) return;
  if (SuspendThread(thread) == (DWORD)-1) { CloseHandle(thread); return; }

  CONTEXT ctx = start;
  ctx.ContextFlags = CONTEXT_FULL;
  GetThreadContext(thread, &ctx);

  STACKFRAME64 sf;
  ZeroMemory(&sf, sizeof(sf));
  sf.AddrPC.Offset = ctx.Rip;
  sf.AddrPC.Mode = AddrModeFlat;
  sf.AddrFrame.Offset = ctx.Rbp;
  sf.AddrFrame.Mode = AddrModeFlat;
  sf.AddrStack.Offset = ctx.Rsp;
  sf.AddrStack.Mode = AddrModeFlat;

  printf("---- stack tid %lu ----\n", tid);
  for (int i = 0; i < 40; ++i) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, g_proc, thread, &sf, &ctx,
                     nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                     nullptr))
      break;
    if (sf.AddrPC.Offset == 0) break;
    char sym[1024];
    SymFor(sf.AddrPC.Offset, sym, sizeof(sym));
    printf("  %016llX  %s\n", (unsigned long long)sf.AddrPC.Offset, sym);
  }
  ResumeThread(thread);
  CloseHandle(thread);
}

int main(int argc, char** argv) {
  if (argc < 4) {
    printf("usage: host_sample.exe <exe> <workdir> <seconds> [game_data_root]\n");
    return 2;
  }
  const char* exe = argv[1];
  const char* cwd = argv[2];
  int seconds = atoi(argv[3]);
  const char* game_dir = argc > 4 ? argv[4] : nullptr;

  char cmdline[MAX_PATH * 4];
  if (game_dir) {
    snprintf(cmdline, sizeof(cmdline),
             "\"%s\" --game_data_root=\"%s\" --log_level=debug "
             "--log_file=\"%s\\logs\\runtime_sample.log\"",
             exe, game_dir, cwd);
  } else {
    snprintf(cmdline, sizeof(cmdline), "\"%s\"", exe);
  }

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  if (!CreateProcessA(exe, cmdline, nullptr, nullptr, FALSE, 0, nullptr, cwd,
                      &si, &pi)) {
    printf("CreateProcess failed: %lu\n", GetLastError());
    return 1;
  }
  g_proc = pi.hProcess;
  printf("launched pid %lu, sampling at +%ds (no debugger)\n", pi.dwProcessId,
         seconds);
  Sleep((DWORD)seconds * 1000);

  if (WaitForSingleObject(g_proc, 0) == WAIT_OBJECT_0) {
    DWORD code = 0;
    GetExitCodeProcess(g_proc, &code);
    printf("process exited before sampling, code=0x%08lX\n", code);
    return 3;
  }

  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
  if (!SymInitialize(g_proc, nullptr, TRUE)) {
    printf("SymInitialize failed: %lu\n", GetLastError());
  }

  // Enumerate threads and their CPU time.
  std::vector<ThreadInfo> threads;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  THREADENTRY32 te;
  te.dwSize = sizeof(te);
  if (Thread32First(snap, &te)) {
    do {
      if (te.th32OwnerProcessID != pi.dwProcessId) continue;
      double cpu = 0;
      HANDLE t = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
      if (t) {
        FILETIME c, e, k, u;
        if (GetThreadTimes(t, &c, &e, &k, &u)) {
          ULARGE_INTEGER a, b;
          a.LowPart = k.dwLowDateTime; a.HighPart = k.dwHighDateTime;
          b.LowPart = u.dwLowDateTime; b.HighPart = u.dwHighDateTime;
          cpu = (double)(a.QuadPart + b.QuadPart) / 1e7;
        }
        CloseHandle(t);
      }
      threads.push_back({te.th32ThreadID, cpu});
    } while (Thread32Next(snap, &te));
  }
  CloseHandle(snap);

  std::sort(threads.begin(), threads.end(),
            [](const ThreadInfo& a, const ThreadInfo& b) { return a.cpu > b.cpu; });

  printf("--- thread CPU (top 8) ---\n");
  for (size_t i = 0; i < threads.size() && i < 8; ++i)
    printf("  tid %-6lu cpu %.3fs\n", threads[i].tid, threads[i].cpu);

  // Sample RIP of the top 8 threads 60 times each.
  for (size_t ti = 0; ti < threads.size() && ti < 8; ++ti) {
    DWORD tid = threads[ti].tid;
    struct Hit { DWORD64 rip; int count; };
    std::vector<Hit> hits;
    CONTEXT last;
    ZeroMemory(&last, sizeof(last));
    bool haveLast = false;
    for (int s = 0; s < 60; ++s) {
      CONTEXT ctx;
      ZeroMemory(&ctx, sizeof(ctx));
      if (SampleThread(tid, &ctx)) {
        last = ctx;
        haveLast = true;
        bool found = false;
        for (auto& h : hits) {
          if (h.rip == ctx.Rip) { h.count++; found = true; break; }
        }
        if (!found) hits.push_back({ctx.Rip, 1});
      }
      Sleep(10);
    }
    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b) { return a.count > b.count; });
    printf("--- tid %lu hot RIPs (cpu %.3fs) ---\n", tid, threads[ti].cpu);
    for (size_t i = 0; i < hits.size() && i < 12; ++i) {
      char sym[1024];
      SymFor(hits[i].rip, sym, sizeof(sym));
      printf("  %3d x %016llX  %s\n", hits[i].count,
             (unsigned long long)hits[i].rip, sym);
    }
    if (haveLast) WalkStack(tid, last);
  }

  TerminateProcess(g_proc, 0);
  WaitForSingleObject(pi.hProcess, 3000);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return 0;
}
