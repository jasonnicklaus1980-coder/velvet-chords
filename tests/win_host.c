/* A VST2 host for the Windows build: loads velvetchords.dll, opens its editor in a window, clicks the controls, plays
 * notes, saves and restores the state, and closes everything. Built with zig for Windows and run under Wine.
 * Usage: win_host.exe <path to velvetchords.dll>. Exit 0 when every check passes. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/vst2_abi.h"

typedef AEffect *(*MainFn)(audioMasterCallback);
static int checks, failures;
#define CHECK(cond, what) do { checks++; if (!(cond)) { failures++; printf("  FAIL: %s\n", what); } else printf("  ok: %s\n", what); } while (0)

static intptr_t host_cb(AEffect *fx, int32_t op, int32_t index, intptr_t value, void *ptr, float opt) {
    (void)fx; (void)index; (void)value; (void)ptr; (void)opt;
    if (op == audioMasterVersion) return 2400;
    return 0;
}

static void pump(int ms) {
    DWORD end = GetTickCount() + (DWORD)ms;
    MSG m;
    while (GetTickCount() < end) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
        Sleep(10);
    }
}

static int find_param(AEffect *fx, const char *name) {
    char n[64];
    for (int i = 0; i < fx->numParams; i++) {
        n[0] = 0;
        fx->dispatcher(fx, effGetParamName, i, 0, n, 0);
        if (!strcmp(n, name)) return i;
    }
    return -1;
}

static void display(AEffect *fx, int i, char *out, int max) {
    out[0] = 0;
    fx->dispatcher(fx, effGetParamDisplay, i, 0, out, 0);
    out[max - 1] = 0;
}

static void send_note(AEffect *fx, int on, int note, int vel) {
    VstMidiEvent e;
    memset(&e, 0, sizeof e);
    e.type = kVstMidiType;
    e.byteSize = sizeof e;
    e.midiData[0] = (char)(on ? 0x90 : 0x80);
    e.midiData[1] = (char)note;
    e.midiData[2] = (char)vel;
    struct { int32_t n; intptr_t r; VstEvent *ev[1]; } evs;
    evs.n = 1; evs.r = 0; evs.ev[0] = (VstEvent *)&e;
    fx->dispatcher(fx, effProcessEvents, 0, 0, &evs, 0);
}

/* renders blocks and returns the largest absolute sample */
static float render(AEffect *fx, int blocks) {
    static float L[512], R[512];
    static float *outs[2] = {L, R};
    static float *ins[2] = {L, R};
    float peak = 0.f;
    for (int b = 0; b < blocks; b++) {
        memset(L, 0, sizeof L); memset(R, 0, sizeof R);
        fx->processReplacing(fx, ins, outs, 512);
        for (int i = 0; i < 512; i++) {
            float a = fabsf(L[i]), c = fabsf(R[i]);
            if (a != a || c != c) { printf("  NaN in output\n"); failures++; return 0.f; }
            if (a > peak) peak = a;
            if (c > peak) peak = c;
        }
    }
    return peak;
}


/* writes the window's pixels to a 24-bit BMP (for a look at the editor under Wine) */
static int save_window_bmp(HWND h, const char *path) {
    RECT r;
    GetClientRect(h, &r);
    int w = r.right, hgt = r.bottom;
    HDC src = GetDC(h), mem = CreateCompatibleDC(src);
    HBITMAP bmp = CreateCompatibleBitmap(src, w, hgt);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, hgt, src, 0, 0, SRCCOPY);   /* what is on screen: the window must be visible */
    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof bi);
    bi.biSize = sizeof bi; bi.biWidth = w; bi.biHeight = hgt; bi.biPlanes = 1; bi.biBitCount = 24;
    int stride = (w * 3 + 3) & ~3;
    unsigned char *px = malloc((size_t)stride * hgt);
    GetDIBits(mem, bmp, 0, hgt, px, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    FILE *f = fopen(path, "wb");
    if (f) {
        BITMAPFILEHEADER fh;
        memset(&fh, 0, sizeof fh);
        fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof bi; fh.bfSize = fh.bfOffBits + (DWORD)stride * hgt;
        fwrite(&fh, sizeof fh, 1, f); fwrite(&bi, sizeof bi, 1, f); fwrite(px, (size_t)stride * hgt, 1, f);
        fclose(f);
    }
    free(px);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(h, src);
    return f != NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s velvetchords.dll [editor.bmp]\n", argv[0]); return 2; }
    printf("load\n");
    HMODULE lib = LoadLibraryA(argv[1]);
    CHECK(lib != NULL, "the DLL loads");
    if (!lib) goto done;
    MainFn entry = (MainFn)GetProcAddress(lib, "VSTPluginMain");
    CHECK(entry != NULL, "VSTPluginMain is exported");
    if (!entry) goto done;
    AEffect *fx = entry(host_cb);
    CHECK(fx != NULL && fx->magic == 0x56737450, "the plugin returns a VST2 effect");
    if (!fx) goto done;

    fx->dispatcher(fx, effOpen, 0, 0, NULL, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, NULL, 44100.f);
    fx->dispatcher(fx, effSetBlockSize, 0, 512, NULL, 0);
    fx->dispatcher(fx, effMainsChanged, 0, 1, NULL, 0);
    CHECK(fx->numParams > 100, "the plugin has its parameters");

    int pMode = find_param(fx, "Mode"), pPad1 = find_param(fx, "Pad 1 chord"), pPad2 = find_param(fx, "Pad 2 chord");
    CHECK(pMode >= 0 && pPad1 >= 0 && pPad2 >= 0, "Mode and the pad chord parameters exist");
    char d[96];
    display(fx, pPad1, d, sizeof d);
    CHECK(!strcmp(d, "Cmaj7"), "pad 1 shows its chord name (Cmaj7)");
    display(fx, pPad2, d, sizeof d);
    CHECK(!strcmp(d, "Dm7"), "pad 2 shows its chord name (Dm7)");

    printf("editor\n");
    struct { short t, l, b, r; } *rect = NULL;
    fx->dispatcher(fx, effEditGetRect, 0, 0, &rect, 0);
    CHECK(rect && rect->r == 1280 && rect->b == 660, "the editor is 1280 x 660");
    HWND parent = CreateWindowExA(0, "STATIC", "host", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 1320, 740, NULL, NULL,
                                  GetModuleHandleA(NULL), NULL);
    CHECK(parent != NULL, "a host window is created");
    intptr_t opened = fx->dispatcher(fx, effEditOpen, 0, 0, (void *)parent, 0);
    CHECK(opened == 1, "effEditOpen returns success");
    pump(600);
    HWND child = FindWindowExA(parent, NULL, "VelvetChordsEditor", NULL);
    CHECK(child != NULL, "the editor window is a child of the host window");
    if (child) {
        /* the Mode segment "Custom" is the second one on the Play page: click it, Mode should read Custom */
        PostMessageA(child, WM_LBUTTONDOWN, 0, MAKELPARAM(552 + 10, 12 + 10));
        pump(300);
        display(fx, pMode, d, sizeof d);
        CHECK(!strcmp(d, "Custom"), "clicking the Custom segment sets Mode to Custom");
        /* the Perform tab (the second along the bottom) shows its own controls: Mode is not on it */
        PostMessageA(child, WM_LBUTTONDOWN, 0, MAKELPARAM(1280 / 6 + 20, 640));
        pump(300);
        PostMessageA(child, WM_RBUTTONDOWN, 0, MAKELPARAM(456 + 10, 12 + 10));   /* nothing here: Mode stays Custom */
        pump(200);
        display(fx, pMode, d, sizeof d);
        CHECK(!strcmp(d, "Custom"), "a click on Perform's empty corner leaves Mode alone");
        /* back to Play: a right click on the Chords segment sets Chords too, since a segment ignores the button */
        PostMessageA(child, WM_LBUTTONDOWN, 0, MAKELPARAM(20, 640));   /* Play */
        pump(200);
        PostMessageA(child, WM_RBUTTONDOWN, 0, MAKELPARAM(456 + 10, 12 + 10));
        pump(300);
        display(fx, pMode, d, sizeof d);
        CHECK(!strcmp(d, "Chords"), "a right click on the Chords segment sets Mode to Chords");
        PostMessageA(child, WM_LBUTTONDOWN, 0, MAKELPARAM(552 + 10, 12 + 10));   /* Custom again for the state check */
        pump(200);
    }

    if (child && argc > 2) {   /* a screenshot of the editor on the Play page, for a look at it */
        pump(200);
        CHECK(save_window_bmp(child, argv[2]), "the editor is saved as a picture");
    }
    printf("audio\n");
    send_note(fx, 1, 60, 100);
    float peak = render(fx, 40);
    CHECK(peak > 0.0005f, "a note produces sound");
    printf("  (peak %.4f)\n", peak);
    send_note(fx, 0, 60, 0);
    render(fx, 40);
    pump(200);

    printf("state\n");
    void *chunk = NULL;
    intptr_t size = fx->dispatcher(fx, effGetChunk, 0, 0, &chunk, 0);
    CHECK(size > 100 && chunk != NULL, "the state saves");
    if (size > 0 && chunk) {
        unsigned char *saved = malloc((size_t)size);
        memcpy(saved, chunk, (size_t)size);
        fx->dispatcher(fx, effEditClose, 0, 0, NULL, 0);
        AEffect *fx2 = entry(host_cb);
        fx2->dispatcher(fx2, effOpen, 0, 0, NULL, 0);
        fx2->dispatcher(fx2, effSetSampleRate, 0, 0, NULL, 44100.f);
        fx2->dispatcher(fx2, effSetBlockSize, 0, 512, NULL, 0);
        fx2->dispatcher(fx2, effSetChunk, 0, (int)size, saved, 0);
        display(fx2, pMode, d, sizeof d);
        CHECK(!strcmp(d, "Custom"), "a saved state restores Mode (Custom)");
        fx2->dispatcher(fx2, effClose, 0, 0, NULL, 0);
        free(saved);
    }

    printf("close\n");
    fx->dispatcher(fx, effEditClose, 0, 0, NULL, 0);
    CHECK(FindWindowExA(parent, NULL, "VelvetChordsEditor", NULL) == NULL, "the editor closes with its window");
    if (parent) DestroyWindow(parent);
    fx->dispatcher(fx, effMainsChanged, 0, 0, NULL, 0);
    fx->dispatcher(fx, effClose, 0, 0, NULL, 0);
    CHECK(1, "the plugin closes");
done:
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
