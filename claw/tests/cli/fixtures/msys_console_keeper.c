/* Native fixture: keep a real Windows console alive during measured capture. */
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int console_stderr(void) {
    DWORD mode;
    HANDLE handle = (HANDLE)_get_osfhandle(2);
    return handle != INVALID_HANDLE_VALUE && _isatty(2) != 0
        && GetConsoleMode(handle, &mode) != 0;
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    if (strcmp(argv[1], "--silent") == 0)
        return 0;
    if (strcmp(argv[1], "--exit") == 0)
        return 23;
    if (strcmp(argv[1], "--sleep") == 0) {
        Sleep(60000);
        return 0;
    }
    if (strcmp(argv[1], "--probe") == 0) {
        fprintf(stderr, "TERMINAL-PROBE tty=%d console=%d\n",
                _isatty(2) != 0, console_stderr());
        return 0;
    }
    if (strcmp(argv[1], "--hold") != 0)
        return 2;
    if (!console_stderr())
        return 3;
    fputs("MSYS-CONSOLE-KEEPER\n", stderr);
    fflush(stderr);
    fputs("READY\n", stdout);
    fflush(stdout);
    (void)getchar();
    return 0;
}
