#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static int fail(const wchar_t *what) {
    fwprintf(stderr, L"windows_stack_status_probe: %ls failed (%lu)\n", what, GetLastError());
    return 2;
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 5) {
        fwprintf(stderr, L"usage: %ls executable stdout-file stderr-file status-file\n", argv[0]);
        return 2;
    }
    /* Inherited by the child: report an exception as a status, not a GUI prompt. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE out = CreateFileW(argv[2], GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) return fail(L"stdout CreateFileW");
    HANDLE err = CreateFileW(argv[3], GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (err == INVALID_HANDLE_VALUE) { CloseHandle(out); return fail(L"stderr CreateFileW"); }

    size_t n = wcslen(argv[1]);
    size_t cap = n * 2 + 3;
    wchar_t *cmd = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, cap * sizeof(wchar_t));
    if (!cmd) { CloseHandle(out); CloseHandle(err); return fail(L"HeapAlloc"); }
    /* The generated executable path is the only argument; quote and escape trailing slashes. */
    _snwprintf(cmd, cap, L"\"%ls\"", argv[1]);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out;
    si.hStdError = err;
    BOOL created = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    HeapFree(GetProcessHeap(), 0, cmd);
    CloseHandle(out);
    CloseHandle(err);
    if (!created) return fail(L"CreateProcessW");
    DWORD wait = WaitForSingleObject(pi.hProcess, INFINITE);
    if (wait != WAIT_OBJECT_0) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return fail(L"WaitForSingleObject"); }
    DWORD code = 0;
    if (!GetExitCodeProcess(pi.hProcess, &code)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return fail(L"GetExitCodeProcess"); }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    FILE *status = _wfopen(argv[4], L"wb");
    if (!status) return fail(L"status fopen");
    fprintf(status, "raw_exit_code=0x%08lX decimal=%lu\n", (unsigned long)code, (unsigned long)code);
    fclose(status);
    return 0;
}
