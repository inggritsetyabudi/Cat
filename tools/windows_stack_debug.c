#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <stdint.h>

typedef struct { LONG status; PVOID teb; ULONG_PTR client[2]; ULONG_PTR affinity; LONG priority; LONG base_priority; } thread_basic;
typedef LONG (WINAPI *query_thread_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static void region(HANDLE p, const char *label, ULONG_PTR address) {
    MEMORY_BASIC_INFORMATION m = {0};
    if (VirtualQueryEx(p, (void *)address, &m, sizeof(m)))
        printf("region %s address=0x%llx base=0x%llx allocation=0x%llx size=0x%llx state=0x%lx protect=0x%lx\n", label, (unsigned long long)address, (unsigned long long)(uintptr_t)m.BaseAddress, (unsigned long long)(uintptr_t)m.AllocationBase, (unsigned long long)m.RegionSize, m.State, m.Protect);
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    size_t cap = wcslen(argv[1]) + 3;
    wchar_t *cmd = HeapAlloc(GetProcessHeap(), 0, cap * sizeof(wchar_t));
    if (!cmd) return 2;
    _snwprintf(cmd, cap, L"\"%ls\"", argv[1]);
    STARTUPINFOW si = {0}; PROCESS_INFORMATION pi = {0}; si.cb = sizeof(si);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) { printf("CreateProcess error=%lu\n", GetLastError()); return 2; }
    HeapFree(GetProcessHeap(), 0, cmd);
    query_thread_fn query = (query_thread_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread");
    DEBUG_EVENT e; int done = 0;
    while (!done && WaitForDebugEvent(&e, 60000)) {
        DWORD disposition = DBG_CONTINUE;
        if (e.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            printf("image_base=0x%llx start=0x%llx\n", (unsigned long long)(uintptr_t)e.u.CreateProcessInfo.lpBaseOfImage, (unsigned long long)(uintptr_t)e.u.CreateProcessInfo.lpStartAddress);
            if (e.u.CreateProcessInfo.hFile) CloseHandle(e.u.CreateProcessInfo.hFile);
        } else if (e.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
            if (e.u.LoadDll.hFile) CloseHandle(e.u.LoadDll.hFile);
        } else if (e.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            EXCEPTION_RECORD *r = &e.u.Exception.ExceptionRecord;
            printf("exception=0x%08lx first=%lu at=0x%llx flags=0x%lx parameters=%lu", r->ExceptionCode, e.u.Exception.dwFirstChance, (unsigned long long)(uintptr_t)r->ExceptionAddress, r->ExceptionFlags, r->NumberParameters);
            for (DWORD i=0; i<r->NumberParameters; ++i) printf(" p%lu=0x%llx", i, (unsigned long long)r->ExceptionInformation[i]);
            printf("\n");
            HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, e.dwThreadId);
            CONTEXT c = {0}; c.ContextFlags = CONTEXT_FULL;
            if (t && GetThreadContext(t, &c)) {
                printf("rip=0x%llx rsp=0x%llx rbp=0x%llx rax=0x%llx rcx=0x%llx rdx=0x%llx\n", c.Rip,c.Rsp,c.Rbp,c.Rax,c.Rcx,c.Rdx);
                region(pi.hProcess,"rsp",c.Rsp);
                if (r->NumberParameters>=2) region(pi.hProcess,"fault",r->ExceptionInformation[1]);
                unsigned char bytes[32]; SIZE_T got=0;
                if (ReadProcessMemory(pi.hProcess,(void *)(uintptr_t)c.Rip,bytes,sizeof(bytes),&got)) { printf("instruction_bytes="); for(SIZE_T i=0;i<got;++i)printf("%02x",bytes[i]); printf("\n"); }
            }
            thread_basic b={0}; NT_TIB tib={0}; SIZE_T got=0;
            if(t && query && query(t,0,&b,sizeof(b),NULL)>=0 && ReadProcessMemory(pi.hProcess,b.teb,&tib,sizeof(tib),&got)) {
                printf("teb=0x%llx stack_base=0x%llx stack_limit=0x%llx\n",(unsigned long long)(uintptr_t)b.teb,(unsigned long long)(uintptr_t)tib.StackBase,(unsigned long long)(uintptr_t)tib.StackLimit);
                region(pi.hProcess,"stack_limit",(ULONG_PTR)tib.StackLimit);
            }
            if(t)CloseHandle(t);
            if (r->ExceptionCode != EXCEPTION_BREAKPOINT) disposition=DBG_EXCEPTION_NOT_HANDLED;
            fflush(stdout);
        } else if (e.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            printf("debugger_exit_code=0x%08lx\n", e.u.ExitProcess.dwExitCode); done=1;
        }
        ContinueDebugEvent(e.dwProcessId,e.dwThreadId,disposition);
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return done ? 0 : 3;
}
