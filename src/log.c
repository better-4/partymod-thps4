#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <windows.h>

FILE* fp_log = NULL;


void initializeLogging() {
    AllocConsole();
    SetConsoleTitle("Better4 Debug Console");

    fopen_s(&fp_log, "better4.log", "w");

    FILE *f_dummy;
    freopen_s(&f_dummy, "CONIN$", "r", stdin);
    freopen_s(&f_dummy, "CONOUT$", "w", stderr);
    freopen_s(&f_dummy, "CONOUT$", "w", stdout);
}

// Print and log to `better4.log`
int printLog(const char* fmt, ...) {
	va_list va;
	va_start(va, fmt);
	char rendered[1024];
    int ret = vsnprintf(rendered, 1024, fmt, va);
	va_end(va);

    printf(rendered);

    if (fp_log) {
        fputs(rendered, fp_log);
        fflush(fp_log);
    }

    return ret;
}
