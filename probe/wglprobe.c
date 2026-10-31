// Checks a software opengl32.dll the way murrtty uses one: load it by full
// path (gdi32 then routes pixel formats to it by name), create an OpenGL
// 4.3 core context on a window, clear to red and read the pixel back, then
// link and draw a program like murrtty's (gl_FragCoord.y, a uniform block).
//
//     wglprobe <full path of opengl32.dll>
//
// Prints GL_VERSION and GL_RENDERER; exits 0 only if the context is 4.3 or
// newer, the cleared pixel is red and the drawn one cyan.
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

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TRIANGLES 0x0004
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_STATIC_DRAW 0x88E4

typedef unsigned(APIENTRY *CreateShaderF)(unsigned);
typedef void(APIENTRY *ShaderSourceF)(unsigned, int, const char *const *, const int *);
typedef void(APIENTRY *CompileShaderF)(unsigned);
typedef void(APIENTRY *GetivF)(unsigned, unsigned, int *);
typedef void(APIENTRY *GetInfoLogF)(unsigned, int, int *, char *);
typedef unsigned(APIENTRY *CreateProgramF)(void);
typedef void(APIENTRY *AttachShaderF)(unsigned, unsigned);
typedef void(APIENTRY *LinkProgramF)(unsigned);
typedef void(APIENTRY *UseProgramF)(unsigned);
typedef void(APIENTRY *GenF)(int, unsigned *);
typedef void(APIENTRY *BindVertexArrayF)(unsigned);
typedef void(APIENTRY *BindBufferF)(unsigned, unsigned);
typedef void(APIENTRY *BindBufferBaseF)(unsigned, unsigned, unsigned);
typedef void(APIENTRY *BufferDataF)(unsigned, ptrdiff_t, const void *, unsigned);
typedef void(APIENTRY *DrawArraysF)(unsigned, int, int);
typedef void(APIENTRY *FinishF)(void);
typedef void(APIENTRY *ReadPixelsF)(int, int, int, int, unsigned, unsigned, void *);

static const char *vertex_source =
    "#version 330 core\n"
    "void main() {\n"
    "  gl_Position = vec4(gl_VertexID == 2 ? 3.0 : -1.0, gl_VertexID == 0 ? -3.0 : 1.0, 1.0, 1.0);\n"
    "}\n";

// Green where gl_FragCoord.y is in the window, blue from the uniform block.
static const char *fragment_source =
    "#version 430 core\n"
    "layout(origin_upper_left) in vec4 gl_FragCoord;\n"
    "layout(binding = 1, std140) uniform Globals { vec4 tint; };\n"
    "layout(location = 0) out vec4 out_FragColor;\n"
    "void main() {\n"
    "  float g = (gl_FragCoord.y >= 0.0 && gl_FragCoord.y < 4096.0) ? 1.0 : 0.0;\n"
    "  out_FragColor = vec4(0.0, g, tint.b, 1.0);\n"
    "}\n";

// fail() for drawProgram, which returns 1 when it drew.
static int bad(const char *what) {
    fail(what);
    return 0;
}

static unsigned compile(GetProc getProc, unsigned type, const char *source) {
    CreateShaderF createShader = (CreateShaderF)(void *)getProc("glCreateShader");
    ShaderSourceF shaderSource = (ShaderSourceF)(void *)getProc("glShaderSource");
    CompileShaderF compileShader = (CompileShaderF)(void *)getProc("glCompileShader");
    GetivF getShaderiv = (GetivF)(void *)getProc("glGetShaderiv");
    GetInfoLogF getShaderInfoLog = (GetInfoLogF)(void *)getProc("glGetShaderInfoLog");
    if (!createShader || !shaderSource || !compileShader || !getShaderiv || !getShaderInfoLog) return 0;
    unsigned id = createShader(type);
    shaderSource(id, 1, &source, NULL);
    compileShader(id);
    int ok = 0;
    getShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        getShaderInfoLog(id, sizeof log - 1, NULL, log);
        fprintf(stderr, "wglprobe: compiling failed: %s\n", log);
        return 0;
    }
    return id;
}

static int drawProgram(GetProc getProc, HMODULE gl) {
    CreateProgramF createProgram = (CreateProgramF)(void *)getProc("glCreateProgram");
    AttachShaderF attachShader = (AttachShaderF)(void *)getProc("glAttachShader");
    LinkProgramF linkProgram = (LinkProgramF)(void *)getProc("glLinkProgram");
    GetivF getProgramiv = (GetivF)(void *)getProc("glGetProgramiv");
    GetInfoLogF getProgramInfoLog = (GetInfoLogF)(void *)getProc("glGetProgramInfoLog");
    UseProgramF useProgram = (UseProgramF)(void *)getProc("glUseProgram");
    GenF genVertexArrays = (GenF)(void *)getProc("glGenVertexArrays");
    BindVertexArrayF bindVertexArray = (BindVertexArrayF)(void *)getProc("glBindVertexArray");
    GenF genBuffers = (GenF)(void *)getProc("glGenBuffers");
    BindBufferF bindBuffer = (BindBufferF)(void *)getProc("glBindBuffer");
    BindBufferBaseF bindBufferBase = (BindBufferBaseF)(void *)getProc("glBindBufferBase");
    BufferDataF bufferData = (BufferDataF)(void *)getProc("glBufferData");
    DrawArraysF drawArrays = (DrawArraysF)(void *)GetProcAddress(gl, "glDrawArrays");
    FinishF finish = (FinishF)(void *)GetProcAddress(gl, "glFinish");
    ReadPixelsF readPixels = (ReadPixelsF)(void *)GetProcAddress(gl, "glReadPixels");
    if (!createProgram || !attachShader || !linkProgram || !getProgramiv || !getProgramInfoLog || !useProgram ||
        !genVertexArrays || !bindVertexArray || !genBuffers || !bindBuffer || !bindBufferBase || !bufferData ||
        !drawArrays || !finish || !readPixels)
        return bad("a GL 4.3 function is missing");

    unsigned vs = compile(getProc, GL_VERTEX_SHADER, vertex_source);
    unsigned fs = compile(getProc, GL_FRAGMENT_SHADER, fragment_source);
    if (!vs || !fs) return bad("a shader didn't compile");
    unsigned program = createProgram();
    attachShader(program, vs);
    attachShader(program, fs);
    linkProgram(program);
    int ok = 0;
    getProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        getProgramInfoLog(program, sizeof log - 1, NULL, log);
        fprintf(stderr, "wglprobe: linking failed: %s\n", log);
        return bad("the program didn't link");
    }
    printf("linked a program reading gl_FragCoord.y and a uniform block\n");

    unsigned vao, ubo;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    const float tint[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    genBuffers(1, &ubo);
    bindBuffer(GL_UNIFORM_BUFFER, ubo);
    bufferData(GL_UNIFORM_BUFFER, sizeof tint, tint, GL_STATIC_DRAW);
    bindBufferBase(GL_UNIFORM_BUFFER, 1, ubo);
    useProgram(program);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    unsigned char pixel[4] = {0};
    readPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("drawn: %u %u %u %u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    if (pixel[0] != 0 || pixel[1] != 255 || pixel[2] != 255) return bad("the drawn pixel isn't cyan");
    return 1;
}

int main(int argc, char **argv) {
    // Unbuffered, so what it printed survives a crash.
    setvbuf(stdout, NULL, _IONBF, 0);
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

    // A program like murrtty's: a full-screen triangle from gl_VertexID,
    // and a fragment shader reading gl_FragCoord.y (which a window's
    // framebuffer makes Mesa flip with a state uniform) and a uniform block.
    if (!drawProgram(getProc, gl)) return 1;
    printf("wglprobe: ok\n");
    return 0;
}
