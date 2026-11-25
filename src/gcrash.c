/*
 * GrooveGoat - crash report (Windows)
 *
 * When the program crashes on Windows, a short report is written to
 * crash.txt in the configuration folder (%APPDATA%\C64 GrooveGoat) and a
 * message box says where it is. The report has the exception, the program
 * addresses of the call stack (relative to the program, so they can be
 * looked up in the build) and the last steps of the program (start, dialogs).
 * On other systems the functions do nothing but keep the last steps.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "gcrash.h"

#define NOTES 24
#define NOTELEN 96

static char notes[NOTES][NOTELEN];
static int notepos = 0;
static const char *crashversion = "";

void crash_note(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(notes[notepos % NOTES], NOTELEN, fmt, ap);
  va_end(ap);
  notepos++;
}

#if defined(__WIN32__) || defined(_WIN32)

#include <windows.h>
#include <shlobj.h>
#include <signal.h>

static wchar_t reportpath[MAX_PATH + 64];

static const char *exceptionname(DWORD code)
{
  switch (code)
  {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction (CPU too old?)";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer division by zero";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_IN_PAGE_ERROR: return "in page error";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case 0xc0000409: return "stack buffer overrun / fast fail";
    case 0xc0000374: return "heap corruption";
    case 0x40000015: return "abort()";
  }
  return "unknown";
}

static void writeaddr(FILE *f, int n, DWORD64 addr)
{
  HMODULE mod = NULL;
  wchar_t name[MAX_PATH];
  const wchar_t *base;
  if ((!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
    (LPCWSTR)(ULONG_PTR)addr, &mod)) || (!mod))
  {
    fprintf(f, "  #%-2d %016llx\n", n, (unsigned long long)addr);
    return;
  }
  name[0] = 0;
  GetModuleFileNameW(mod, name, MAX_PATH);
  base = wcsrchr(name, L'\\');
  base = base ? base + 1 : name;
  fprintf(f, "  #%-2d %ls+0x%llx\n", n, base, (unsigned long long)(addr - (DWORD64)(ULONG_PTR)mod));
}

static void writereport(EXCEPTION_POINTERS *ep)
{
  FILE *f = _wfopen(reportpath, L"w");
  int c;
  if (!f) return;
  fprintf(f, "C64 GrooveGoat %s crashed\n\n", crashversion);
  {
    // the version of Windows (RtlGetVersion tells the truth)
    typedef LONG (WINAPI *RTLGETVERSION)(OSVERSIONINFOW *);
    RTLGETVERSION rgv = (RTLGETVERSION)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOW vi;
    memset(&vi, 0, sizeof vi);
    vi.dwOSVersionInfoSize = sizeof vi;
    if ((rgv) && (rgv(&vi) == 0))
      fprintf(f, "Windows %lu.%lu build %lu\n", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
  }
  if (ep)
  {
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    fprintf(f, "Exception %08lx (%s)\n", er->ExceptionCode, exceptionname(er->ExceptionCode));
    if ((er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) && (er->NumberParameters >= 2))
      fprintf(f, "  %s address %016llx\n", er->ExceptionInformation[0] == 0 ? "reading" :
        (er->ExceptionInformation[0] == 1 ? "writing" : "executing"), (unsigned long long)er->ExceptionInformation[1]);
    fprintf(f, "Thread %lu\n\nCall stack:\n", GetCurrentThreadId());
#ifdef _WIN64
    {
      // walk the stack with the unwind data of the program (no dbghelp needed)
      CONTEXT ctx = *ep->ContextRecord;
      for (c = 0; (c < 40) && (ctx.Rip); c++)
      {
        DWORD64 imagebase = 0;
        PRUNTIME_FUNCTION fn;
        writeaddr(f, c, ctx.Rip);
        fn = RtlLookupFunctionEntry(ctx.Rip, &imagebase, NULL);
        if (fn)
        {
          PVOID handlerdata;
          DWORD64 establisher;
          RtlVirtualUnwind(UNW_FLAG_NHANDLER, imagebase, ctx.Rip, fn, &ctx, &handlerdata, &establisher, NULL);
        }
        else
        {
          // a leaf function: the return address is on top of the stack
          if (IsBadReadPtr((void *)(ULONG_PTR)ctx.Rsp, 8)) break;
          ctx.Rip = *(DWORD64 *)(ULONG_PTR)ctx.Rsp;
          ctx.Rsp += 8;
        }
      }
    }
#else
    writeaddr(f, 0, (DWORD64)(ULONG_PTR)er->ExceptionAddress);
#endif
  }
  fprintf(f, "\nLast steps:\n");
  for (c = 0; c < NOTES; c++)
  {
    int i = notepos - NOTES + c;
    if (i < 0) continue;
    fprintf(f, "  %s\n", notes[i % NOTES]);
  }
  fclose(f);
}

static void showmessage(void)
{
  wchar_t msg[MAX_PATH + 300];
  _snwprintf(msg, sizeof msg / sizeof msg[0],
    L"Sorry, C64 GrooveGoat crashed.\n\nA report was written to:\n%ls\n\n"
    L"Please send this file to the developer, it helps to find the bug.", reportpath);
  msg[sizeof msg / sizeof msg[0] - 1] = 0;
  MessageBoxW(NULL, msg, L"C64 GrooveGoat", MB_OK | MB_ICONERROR | MB_TOPMOST);
}

static volatile LONG crashed = 0;

static LONG WINAPI crashfilter(EXCEPTION_POINTERS *ep)
{
  if (InterlockedExchange(&crashed, 1)) return EXCEPTION_EXECUTE_HANDLER;
  writereport(ep);
  showmessage();
  return EXCEPTION_EXECUTE_HANDLER;
}

static void aborthandler(int sig)
{
  (void)sig;
  if (InterlockedExchange(&crashed, 1)) return;
  crash_note("abort()");
  writereport(NULL);
  showmessage();
  TerminateProcess(GetCurrentProcess(), 3);
}

void crash_install(const char *version)
{
  wchar_t path[MAX_PATH];
  crashversion = version;
  if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path)))
  {
    _snwprintf(reportpath, sizeof reportpath / sizeof reportpath[0], L"%ls\\C64 GrooveGoat", path);
    CreateDirectoryW(reportpath, NULL);
    wcscat(reportpath, L"\\crash.txt");
  }
  else
  {
    // next to the program
    wchar_t *p;
    GetModuleFileNameW(NULL, reportpath, MAX_PATH);
    p = wcsrchr(reportpath, L'\\');
    if (p) wcscpy(p + 1, L"crash.txt");
  }
  SetUnhandledExceptionFilter(crashfilter);
  signal(SIGABRT, aborthandler);
  crash_note("start");
}

#else

void crash_install(const char *version)
{
  crashversion = version;
  crash_note("start");
}

#endif
