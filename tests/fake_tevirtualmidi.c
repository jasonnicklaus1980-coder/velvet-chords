/* A stand-in for the teVirtualMIDI driver's DLL, for testing the Windows virtual port without the driver. It keeps the
 * same three entry points and logs every message sent to the port (as hex) to the file named by VC_FAKE_VMIDI_LOG. */
#include <windows.h>
#include <stdio.h>

static FILE *log_file(void) {
    static FILE *f;
    if (!f) {
        const char *path = getenv("VC_FAKE_VMIDI_LOG");
        if (path) f = fopen(path, "w");
    }
    return f;
}

__declspec(dllexport) void *virtualMIDICreatePortEx2(const wchar_t *name, void *callback, void *instance, DWORD max_sysex, DWORD flags) {
    (void)callback; (void)instance; (void)max_sysex;
    FILE *f = log_file();
    if (f) { fprintf(f, "create %ls flags %lu\n", name, (unsigned long)flags); fflush(f); }
    return (void *)&log_file;   /* any non-null handle */
}

__declspec(dllexport) BOOL virtualMIDISendData(void *port, unsigned char *data, DWORD length) {
    FILE *f = log_file();
    (void)port;
    if (f) {
        for (DWORD i = 0; i < length; i++) fprintf(f, "%02X ", data[i]);
        fprintf(f, "\n");
        fflush(f);
    }
    return TRUE;
}

__declspec(dllexport) void virtualMIDIClosePort(void *port) {
    (void)port;
    FILE *f = log_file();
    if (f) { fprintf(f, "close\n"); fflush(f); }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) { (void)inst; (void)reason; (void)reserved; return TRUE; }
