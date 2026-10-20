#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <stdint.h>

typedef struct { LONG status; PVOID teb; ULONG_PTR client[2]; ULONG_PTR affinity; LONG priority; LONG base_priority; } thread_basic;
typedef LONG (WINAPI *query_thread_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef LONG (WINAPI *version_fn)(OSVERSIONINFOW *);
static unsigned long long u(ULONG_PTR v) { return (unsigned long long)v; }

static void region(HANDLE process, const char *label, ULONG_PTR address) {
    MEMORY_BASIC_INFORMATION m = {0};
    if (!VirtualQueryEx(process, (void *)address, &m, sizeof(m))) {
        printf("region %s address=0x%llx error=%lu\n", label, u(address), GetLastError()); return;
    }
    printf("region %s address=0x%llx base=0x%llx allocation=0x%llx size=0x%llx state=0x%lx protect=0x%lx type=0x%lx\n",
           label, u(address), u((ULONG_PTR)m.BaseAddress), u((ULONG_PTR)m.AllocationBase),
           (unsigned long long)m.RegionSize, m.State, m.Protect, m.Type);
}
static void module_path(HANDLE file) {
    wchar_t path[32768];
    DWORD n = file ? GetFinalPathNameByHandleW(file, path, 32768, FILE_NAME_NORMALIZED) : 0;
    if (!n || n >= 32768) printf("path_error=%lu", GetLastError());
    else printf("path=\"%ls\"", path);
}
static void inspect(HANDLE process, DWORD tid, query_thread_fn query, EXCEPTION_RECORD *record) {
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!thread) { printf("OpenThread_error=%lu\n", GetLastError()); return; }
    CONTEXT c = {0}; c.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(thread, &c)) {
        printf("rip=0x%llx rsp=0x%llx rbp=0x%llx rax=0x%llx rcx=0x%llx rdx=0x%llx\n",u(c.Rip),u(c.Rsp),u(c.Rbp),u(c.Rax),u(c.Rcx),u(c.Rdx));
        region(process, "rsp", c.Rsp);
        MEMORY_BASIC_INFORMATION im = {0};
        if (VirtualQueryEx(process, (void *)c.Rip, &im, sizeof(im)))
            printf("exception_module base=0x%llx rva=0x%llx type=0x%lx\n",u((ULONG_PTR)im.AllocationBase),u(c.Rip-(ULONG_PTR)im.AllocationBase),im.Type);
        unsigned char bytes[32]; SIZE_T got = 0;
        if (ReadProcessMemory(process,(void *)c.Rip,bytes,sizeof(bytes),&got)) {
            printf("instruction_bytes="); for(SIZE_T i=0;i<got;i++) printf("%02x",bytes[i]); printf("\n");
        } else printf("instruction_bytes_error=%lu\n",GetLastError());
        ULONG_PTR words[24]; got=0;
        if (ReadProcessMemory(process,(void *)c.Rsp,words,sizeof(words),&got)) {
            printf("stack_words"); for(SIZE_T i=0;i<got/sizeof(words[0]);i++)printf(" w%llu=0x%llx",(unsigned long long)i,u(words[i])); printf("\n");
        } else printf("stack_words_error=%lu\n",GetLastError());
    } else printf("GetThreadContext_error=%lu\n", GetLastError());
    thread_basic b = {0}; NT_TIB tib = {0}; SIZE_T got=0;
    LONG status = query ? query(thread,0,&b,sizeof(b),NULL) : (LONG)0xC0000002;
    if (status>=0 && ReadProcessMemory(process,b.teb,&tib,sizeof(tib),&got)) {
        printf("teb=0x%llx stack_base=0x%llx stack_limit=0x%llx\n",u((ULONG_PTR)b.teb),u((ULONG_PTR)tib.StackBase),u((ULONG_PTR)tib.StackLimit));
        region(process,"stack_limit",(ULONG_PTR)tib.StackLimit);
        MEMORY_BASIC_INFORMATION m = {0};
        if (VirtualQueryEx(process,(void *)(c.Rsp ? c.Rsp : (ULONG_PTR)tib.StackLimit),&m,sizeof(m))) {
            ULONG_PTR bottom=(ULONG_PTR)m.AllocationBase, top=(ULONG_PTR)tib.StackBase;
            printf("stack_reservation bottom=0x%llx top=0x%llx bytes=0x%llx rsp_offset=0x%llx rbp_offset=0x%llx\n",u(bottom),u(top),u(top-bottom),u(c.Rsp-bottom),u(c.Rbp-bottom));
            // Snapshot the whole reservation, including guard and inaccessible pages.
            for (ULONG_PTR a=bottom; a<top;) {
                if (!VirtualQueryEx(process,(void *)a,&m,sizeof(m))) { printf("stack_map_error=%lu\n",GetLastError()); break; }
                printf("stack_map base=0x%llx size=0x%llx state=0x%lx protect=0x%lx guard=%d\n",u((ULONG_PTR)m.BaseAddress),(unsigned long long)m.RegionSize,m.State,m.Protect,!!(m.Protect&PAGE_GUARD));
                ULONG_PTR next=(ULONG_PTR)m.BaseAddress+m.RegionSize; if(next<=a)break; a=next;
            }
        }
    } else printf("tib_error query_status=0x%08lx read_error=%lu\n",(unsigned long)status,GetLastError());
    if (record->NumberParameters>=2) region(process,"fault",record->ExceptionInformation[1]);
    CloseHandle(thread);
}
int wmain(int argc, wchar_t **argv) {
    if (argc!=2) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SYSTEM_INFO info; GetSystemInfo(&info);
    OSVERSIONINFOW version={0}; version.dwOSVersionInfoSize=sizeof(version);
    version_fn getversion=(version_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");
    if(getversion) getversion(&version);
    printf("os major=%lu minor=%lu build=%lu page_size=%lu\n",version.dwMajorVersion,version.dwMinorVersion,version.dwBuildNumber,info.dwPageSize);
    DWORD count=GetCurrentDirectoryW(0,NULL);
    wchar_t *cwd=HeapAlloc(GetProcessHeap(),0,count*sizeof(wchar_t));
    if(!count || !cwd || !GetCurrentDirectoryW(count,cwd)) { printf("cwd_error=%lu\n",GetLastError()); return 2; }
    size_t cap=wcslen(argv[1])+3;
    wchar_t *cmd=HeapAlloc(GetProcessHeap(),0,cap*sizeof(wchar_t));
    if(!cmd) return 2;
    _snwprintf(cmd,cap,L"\"%ls\"",argv[1]);
    printf("command=%ls child_arguments=none cwd=%ls\n",cmd,cwd);
    STARTUPINFOW si={0}; si.cb=sizeof(si); PROCESS_INFORMATION pi={0};
    if(!CreateProcessW(NULL,cmd,NULL,NULL,FALSE,DEBUG_ONLY_THIS_PROCESS,NULL,cwd,&si,&pi)) { printf("CreateProcess_error=%lu\n",GetLastError()); return 2; }
    HeapFree(GetProcessHeap(),0,cmd); HeapFree(GetProcessHeap(),0,cwd);
    query_thread_fn query=(query_thread_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread");
    DEBUG_EVENT e; DWORD seq=0, terminal=STILL_ACTIVE; int done=0, fatal=0, loader=0, terminating=0;
    while(!done) {
        if(!WaitForDebugEvent(&e,terminating?5000:60000)) {
            DWORD error=GetLastError(); printf("WaitForDebugEvent_error=%lu terminating=%d\n",error,terminating); fatal=1;
            if(!terminating) { if(!TerminateProcess(pi.hProcess,0xDEAD))printf("TerminateProcess_error=%lu\n",GetLastError()); terminating=1; continue; }
            break;
        }
        DWORD disposition=DBG_CONTINUE;
        printf("event seq=%lu type=%lu pid=%lu tid=%lu\n",++seq,e.dwDebugEventCode,e.dwProcessId,e.dwThreadId);
        if(e.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT) {
            printf("image_base=0x%llx start=0x%llx ",u((ULONG_PTR)e.u.CreateProcessInfo.lpBaseOfImage),u((ULONG_PTR)e.u.CreateProcessInfo.lpStartAddress));
            module_path(e.u.CreateProcessInfo.hFile); printf("\n");
            if(e.u.CreateProcessInfo.hFile)CloseHandle(e.u.CreateProcessInfo.hFile);
        } else if(e.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT) {
            printf("module base=0x%llx ",u((ULONG_PTR)e.u.LoadDll.lpBaseOfDll)); module_path(e.u.LoadDll.hFile); printf("\n");
            if(e.u.LoadDll.hFile)CloseHandle(e.u.LoadDll.hFile);
        } else if(e.dwDebugEventCode==EXCEPTION_DEBUG_EVENT) {
            EXCEPTION_RECORD *r=&e.u.Exception.ExceptionRecord;
            printf("exception=0x%08lx first=%lu at=0x%llx flags=0x%lx parameters=%lu",r->ExceptionCode,e.u.Exception.dwFirstChance,u((ULONG_PTR)r->ExceptionAddress),r->ExceptionFlags,r->NumberParameters);
            for(DWORD i=0;i<r->NumberParameters && i<EXCEPTION_MAXIMUM_PARAMETERS;i++)printf(" p%lu=0x%llx",i,u(r->ExceptionInformation[i])); printf("\n");
            inspect(pi.hProcess,e.dwThreadId,query,r);
            if(r->ExceptionCode==EXCEPTION_BREAKPOINT && e.u.Exception.dwFirstChance && !loader) {loader=1; printf("loader_breakpoint_continue=1\n");}
            else disposition=DBG_EXCEPTION_NOT_HANDLED;
        } else if(e.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT) {
            terminal=e.u.ExitProcess.dwExitCode; printf("debugger_exit_code=0x%08lx\n",terminal); done=1;
        }
        if(!ContinueDebugEvent(e.dwProcessId,e.dwThreadId,disposition)) {
            printf("ContinueDebugEvent_error=%lu\n",GetLastError()); fatal=1;
            if(!done && !terminating) {TerminateProcess(pi.hProcess,0xDEAD); terminating=1;}
        }
        fflush(stdout);
    }
    if(!done) {TerminateProcess(pi.hProcess,0xDEAD); WaitForSingleObject(pi.hProcess,5000);}
    DWORD raw=STILL_ACTIVE;
    if(!GetExitCodeProcess(pi.hProcess,&raw)) {printf("GetExitCodeProcess_error=%lu\n",GetLastError());fatal=1;}
    else {printf("exit_validation raw=0x%08lx debug_event=0x%08lx match=%s\n",raw,terminal,raw==terminal?"yes":"no"); if(raw!=terminal)fatal=1;}
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return done&&!fatal?0:3;
}
