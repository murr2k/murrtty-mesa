// Checks a software opengl32.dll the way murrtty uses one: load it by full
// path (gdi32 then routes pixel formats to it by name), create an OpenGL
// 4.3 core context on a window, clear to red, and read the pixel back.
//
//     wglprobe <full path of opengl32.dll>
//
// Prints GL_VERSION and GL_RENDERER; exits 0 only if the context is 4.3 or
// newer and the read-back pixel is red.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

typedef HGLRC(WINAPI *CreateContext)(HDC);
typedef BOOL(WINAPI *MakeCurrent)(HDC, HGLRC);
typedef PROC(WINAPI *GetProc)(LPCSTR);
typedef HGLRC(WINAPI *CreateContextAttribs)(HDC, HGLRC, const int *);
typedef const unsigned char *(WINAPI *GetString)(unsigned);
typedef void(WINAPI *ClearColor)(float, float, float, float);
typedef void(WINAPI *Clear)(unsigned);
typedef void(WINAPI *Finish)(void);
typedef void(WINAPI *ReadPixels)(int, int, int, int, unsigned, unsigned, void *);

static int fail(const char *what) {
    fprintf(stderr, "wglprobe: %s (error %lu)\n", what, GetLastError());
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) return fail("usage: wglprobe <full path of opengl32.dll>");
    HMODULE gl = LoadLibraryExA(argv[1], NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!gl) return fail("could not load the opengl32.dll given");

    CreateContext createContext = (CreateContext)(void *)GetProcAddress(gl, "wglCreateContext");
    MakeCurrent makeCurrent = (MakeCurrent)(void *)GetProcAddress(gl, "wglMakeCurrent");
    GetProc getProc = (GetProc)(void *)GetProcAddress(gl, "wglGetProcAddress");
    GetString getString = (GetString)(void *)GetProcAddress(gl, "glGetString");
    ClearColor clearColor = (ClearColor)(void *)GetProcAddress(gl, "glClearColor");
    Clear clear = (Clear)(void *)GetProcAddress(gl, "glClear");
    Finish finish = (Finish)(void *)GetProcAddress(gl, "glFinish");
    ReadPixels readPixels = (ReadPixels)(void *)GetProcAddress(gl, "glReadPixels");
    if (!createContext || !makeCurrent || !getProc || !getString || !clearColor || !clear || !finish || !readPixels)
        return fail("the opengl32.dll lacks a WGL or GL 1.1 export");

    WNDCLASSA wc = {0};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "wglprobe";
    if (!RegisterClassA(&wc)) return fail("RegisterClass");
    HWND window = CreateWindowA("wglprobe", "wglprobe", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    if (!window) return fail("CreateWindow");
    HDC dc = GetDC(window);

    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int format = ChoosePixelFormat(dc, &pfd);
    if (!format || !SetPixelFormat(dc, format, &pfd)) return fail("no pixel format");

    HGLRC legacy = createContext(dc);
    if (!legacy || !makeCurrent(dc, legacy)) return fail("no legacy context");
    CreateContextAttribs createAttribs = (CreateContextAttribs)(void *)getProc("wglCreateContextAttribsARB");
    if (!createAttribs) return fail("no wglCreateContextAttribsARB");
    const int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0,
    };
    HGLRC core = createAttribs(dc, NULL, attribs);
    if (!core) return fail("no OpenGL 4.3 core context");
    makeCurrent(NULL, NULL);
    if (!makeCurrent(dc, core)) return fail("could not make the 4.3 context current");

    const char *version = (const char *)getString(GL_VERSION);
    const char *renderer = (const char *)getString(GL_RENDERER);
    printf("GL_VERSION: %s\nGL_RENDERER: %s\n", version ? version : "(none)", renderer ? renderer : "(none)");
    int major = 0, minor = 0;
    if (!version || sscanf(version, "%d.%d", &major, &minor) != 2 || major * 10 + minor < 43)
        return fail("the context is older than OpenGL 4.3");

    clearColor(1.0f, 0.0f, 0.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    finish();
    unsigned char pixel[4] = {0};
    readPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("read back: %u %u %u %u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    if (pixel[0] != 255 || pixel[1] != 0 || pixel[2] != 0) return fail("the cleared pixel isn't red");
    printf("wglprobe: ok\n");
    return 0;
}
