#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_POINT_SPRITE 0x8861
#define GL_VERTEX_PROGRAM_POINT_SIZE 0x8642
#define GL_TEXTURE0 0x84C0
#endif

typedef char GLchar;
typedef GLuint(APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void(APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
typedef void(APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void(APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void(APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef GLuint(APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef void(APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void(APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void(APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void(APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void(APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);
typedef GLint(APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void(APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void(APIENTRYP PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void(APIENTRYP PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void(APIENTRYP PFNGLDELETESHADERPROC)(GLuint shader);

static PFNGLCREATESHADERPROC pglCreateShader;
static PFNGLSHADERSOURCEPROC pglShaderSource;
static PFNGLCOMPILESHADERPROC pglCompileShader;
static PFNGLGETSHADERIVPROC pglGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC pglGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC pglCreateProgram;
static PFNGLATTACHSHADERPROC pglAttachShader;
static PFNGLLINKPROGRAMPROC pglLinkProgram;
static PFNGLGETPROGRAMIVPROC pglGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC pglGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC pglUseProgram;
static PFNGLGETUNIFORMLOCATIONPROC pglGetUniformLocation;
static PFNGLUNIFORM1FPROC pglUniform1f;
static PFNGLUNIFORM2FPROC pglUniform2f;
static PFNGLUNIFORM3FPROC pglUniform3f;
static PFNGLDELETESHADERPROC pglDeleteShader;

typedef struct DesktopSearch {
    HWND shellView;
    HWND shellHost;
    HWND workerW;
} DesktopSearch;

typedef struct Vec2 {
    float x;
    float y;
} Vec2;

typedef struct Color {
    float r;
    float g;
    float b;
} Color;

typedef struct ShaderSources {
    char *fileBuffer;
    char *bgVertex;
    char *bgFragment;
    char *ribbonVertex;
    char *ribbonFragment;
    char *pointVertex;
    char *pointFragment;
} ShaderSources;

typedef struct DesktopBounds {
    int x;
    int y;
    int width;
    int height;
} DesktopBounds;

typedef struct NativeRibbon {
    HWND hwnd;
    HWND controlHwnd;
    HDC dc;
    HGLRC gl;
    NOTIFYICONDATAW trayIcon;
    int width;
    int height;
    float aspect;
    float span;
    float phase;
    float pointerWake;
    Vec2 pointer;
    Vec2 targetPointer;
    Vec2 pointerVelocity;
    Vec2 targetPointerVelocity;
    Vec2 previousPointer;
    GLuint bgProgram;
    GLuint ribbonProgram;
    GLuint pointProgram;
    const wchar_t *shaderPath;
    GLint bgResLoc;
    GLint bgTimeLoc;
    GLint ribbonTimeLoc;
    GLint ribbonGrungeLoc;
    GLint pointWakeLoc;
} NativeRibbon;

static Vec2 g_centerlineCache[128];
static float g_densityCache[128];

static NativeRibbon *g_app = NULL;

static const int SAMPLE_COUNT = 128; // No change requested for SAMPLE_COUNT
static const int PARTICLE_COUNT = 2000; // Increased particle count
static const float LINE_WIDTH = 0.0032f;
static const float GLOW_WIDTH = 0.12f;
#define WM_TRAY_ICON (WM_APP + 41)
#define ID_TRAY_EXIT 1001
#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static BOOL CALLBACK find_shell_view(HWND hwnd, LPARAM lparam) {
    DesktopSearch *search = (DesktopSearch *)lparam;
    HWND shellView = FindWindowExW(hwnd, NULL, L"SHELLDLL_DefView", NULL);
    if (shellView != NULL) {
        search->shellView = shellView;
        search->shellHost = hwnd;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK find_worker_after_shell_host(HWND hwnd, LPARAM lparam) {
    DesktopSearch *search = (DesktopSearch *)lparam;
    if (search->shellHost == NULL) {
        return FALSE;
    }

    if (hwnd == search->shellHost) {
        HWND worker = FindWindowExW(NULL, hwnd, L"WorkerW", NULL);
        if (worker != NULL) {
            search->workerW = worker;
        }
        return FALSE;
    }

    return TRUE;
}

static void locate_desktop_windows(DesktopSearch *search) {
    ZeroMemory(search, sizeof(DesktopSearch));
    EnumWindows(find_shell_view, (LPARAM)search);

    if (search->shellView != NULL) {
        EnumWindows(find_worker_after_shell_host, (LPARAM)search);
    }
}

static HWND choose_desktop_parent(DesktopSearch *search) {
    if (search->workerW != NULL) {
        return search->workerW;
    }

    if (search->shellHost != NULL) {
        return search->shellHost;
    }

    HWND fallback = FindWindowW(L"Progman", L"Program Manager");
    if (fallback == NULL) {
        fallback = FindWindowW(L"Progman", NULL);
    }
    return fallback;
}

static HWND find_desktop_parent(void) {
    DesktopSearch search;
    HWND progman = FindWindowW(L"Progman", NULL);

    if (progman != NULL) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
    }

    locate_desktop_windows(&search);
    if (search.shellView != NULL) {
        return choose_desktop_parent(&search);
    }

    locate_desktop_windows(&search);
    return choose_desktop_parent(&search);
}

static HWND find_desktop_wallpaper_parent(void) {
    DesktopSearch search;
    HWND progman = FindWindowW(L"Progman", NULL);

    if (progman != NULL) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
    }

    locate_desktop_windows(&search);
    return search.workerW;
}

static int attach_to_desktop(HWND target) {
    HWND parent = find_desktop_parent();
    if (parent == NULL) {
        fprintf(stderr, "Could not find desktop parent window. Progman=%p\n", FindWindowW(L"Progman", NULL));
        return 2;
    }

    RECT parentRect;
    if (!GetClientRect(parent, &parentRect)) {
        fprintf(stderr, "GetClientRect failed. parent=%p error=%lu\n", parent, GetLastError());
        return 4;
    }

    int width = parentRect.right - parentRect.left;
    int height = parentRect.bottom - parentRect.top;
    if (width <= 0 || height <= 0) {
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }

    HWND currentParent = GetParent(target);
    if (currentParent == parent) {
        SetWindowPos(target, HWND_TOP, 0, 0, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        printf("already-attached hwnd=%p parent=%p size=%dx%d\n", target, parent, width, height);
        return 0;
    }

    LONG_PTR style = GetWindowLongPtrW(target, GWL_STYLE);
    style &= ~WS_POPUP;
    style |= WS_CHILD | WS_VISIBLE;
    SetWindowLongPtrW(target, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtrW(target, GWL_EXSTYLE);
    exStyle &= ~WS_EX_APPWINDOW;
    exStyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    SetWindowLongPtrW(target, GWL_EXSTYLE, exStyle);

    SetLastError(0);
    HWND previous = SetParent(target, parent);
    DWORD setParentError = GetLastError();
    if (previous == NULL && setParentError != 0) {
        fprintf(stderr, "SetParent failed. target=%p parent=%p error=%lu\n", target, parent, setParentError);
        return 3;
    }

    SetWindowPos(target, HWND_TOP, 0, 0, width, height, SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    printf("attached hwnd=%p parent=%p previous=%p size=%dx%d\n", target, parent, previous, width, height);
    return 0;
}

static float clampf(float value, float minValue, float maxValue) {
    return value < minValue ? minValue : (value > maxValue ? maxValue : value);
}

static DesktopBounds get_virtual_desktop_bounds(void) {
    DesktopBounds bounds;
    bounds.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    bounds.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    bounds.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    bounds.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (bounds.width <= 0) {
        bounds.x = 0;
        bounds.width = GetSystemMetrics(SM_CXSCREEN);
    }
    if (bounds.height <= 0) {
        bounds.y = 0;
        bounds.height = GetSystemMetrics(SM_CYSCREEN);
    }

    return bounds;
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static float smoother_step(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float hash1(float n) {
    uint32_t x = (uint32_t)((int32_t)n ^ 0x9E3779B9);
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return (float)x / 4294967296.0f;
}

static float noise1(float x) {
    float i = floorf(x);
    float f = x - i;
    return lerpf(hash1(i), hash1(i + 1.0f), smoother_step(f)) * 2.0f - 1.0f;
}

static float fbm1(float x) {
    float value = 0.0f;
    float amplitude = 0.52f;
    float frequency = 1.0f;
    float norm = 0.0f;

    for (int octave = 0; octave < 4; octave += 1) {
        value += noise1(x * frequency) * amplitude;
        norm += amplitude;
        frequency *= 2.03f;
        amplitude *= 0.48f;
    }

    return value / norm;
}

static float fbm2(float x, float y) {
    return fbm1(x * 0.73f + y * 0.41f) * 0.46f +
        fbm1(x * 1.37f - y * 0.62f + 17.1f) * 0.32f +
        fbm1(x * 2.91f + y * 1.23f - 4.7f) * 0.22f;
}

static Vec2 smoke_flow(float t, float elapsed, float phase) {
    float px = t * 7.2f + phase * 0.031f;
    float py = elapsed * 0.055f - phase * 0.017f;
    float eps = 0.018f;
    float n1 = fbm2(px, py + eps);
    float n2 = fbm2(px, py - eps);
    float n3 = fbm2(px + eps, py);
    float n4 = fbm2(px - eps, py);
    Vec2 flow = { (n1 - n2) / (eps * 2.0f) * 0.005f, -(n3 - n4) / (eps * 2.0f) * 0.005f };
    return flow;
}

static float vortex_field(float t, float elapsed, float phase) {
    float value = 0.0f;

    for (int i = 0; i < 2; i += 1) {
        float fi = (float)i;
        float seed = phase * 0.013f + fi * 19.37f;
        float center = hash1(seed) * 0.86f + 0.07f + sinf(elapsed * (0.018f + fi * 0.006f) + seed) * 0.035f;
        float distance = t - center;
        float radius = 0.035f + hash1(seed + 3.1f) * 0.055f;
        float envelope = expf(-(distance * distance) / (radius * radius));
        float curl = sinf(distance * (52.0f + fi * 13.0f) - elapsed * (0.9f + fi * 0.18f) + seed);
        value += curl * envelope * (0.006f + hash1(seed + 7.4f) * 0.006f);
    }

    return value;
}

static float smoke_density(float t, float elapsed, float phase) {
    float broad = fabsf(fbm1(t * 3.2f + elapsed * 0.025f + phase * 0.11f));
    float medium = fabsf(fbm1(t * 17.0f - elapsed * 0.07f - phase * 0.05f));
    float fine = fabsf(fbm1(t * 84.0f + elapsed * 0.14f + phase * 0.013f));
    return clampf(broad * 0.54f + medium * 0.32f + fine * 0.14f, 0.0f, 0.5f);
}

static Vec2 centerline_at(float t, float elapsed, float phase, float *densityOut) {
    float centerBias = 1.0f - fabsf(t - 0.5f) * 1.6f;
    float density = smoke_density(t, elapsed, phase);
    Vec2 flow = smoke_flow(t, elapsed, phase);
    float envelope = 0.42f + centerBias * 0.5f + density * 0.42f +
        fabsf(fbm1(t * 3.4f - elapsed * 0.026f + phase * 0.13f)) * 0.24f;
    float wave =
        fbm1(t * 4.4f + elapsed * 0.032f + phase) * 0.052f +
        fbm1(t * 11.5f - elapsed * 0.052f - phase * 0.17f) * 0.021f +
        fbm1(t * 29.0f + elapsed * 0.078f + phase * 0.03f) * 0.009f +
        fbm1(t * 73.0f - elapsed * 0.11f + phase * 0.011f) * 0.004f +
        fbm1(t * 181.0f + elapsed * 0.15f - phase * 0.007f) * 0.0016f +
        vortex_field(t, elapsed * 0.72f, phase) * 0.82f +
        flow.y * 0.58f +
        sinf(t * 56.548668f + elapsed * 0.58f) * 0.0012f;
    Vec2 center = { flow.x * (0.32f + density * 0.58f), wave * envelope };
    if (densityOut != NULL) {
        *densityOut = density;
    }
    return center;
}

static Color hsl_to_rgb(float h, float s, float l) {
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float hp = h * 6.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float r1 = 0.0f;
    float g1 = 0.0f;
    float b1 = 0.0f;

    if (hp < 1.0f) {
        r1 = c; g1 = x;
    } else if (hp < 2.0f) {
        r1 = x; g1 = c;
    } else if (hp < 3.0f) {
        g1 = c; b1 = x;
    } else if (hp < 4.0f) {
        g1 = x; b1 = c;
    } else if (hp < 5.0f) {
        r1 = x; b1 = c;
    } else {
        r1 = c; b1 = x;
    }

    float m = l - c * 0.5f;
    Color color = { r1 + m, g1 + m, b1 + m };
    return color;
}

static void *load_gl_proc(const char *name) {
    void *proc = (void *)wglGetProcAddress(name);
    if (proc == NULL || proc == (void *)1 || proc == (void *)2 || proc == (void *)3 || proc == (void *)-1) {
        HMODULE module = GetModuleHandleW(L"opengl32.dll");
        proc = (void *)GetProcAddress(module, name);
    }
    return proc;
}

static int load_gl_functions(void) {
    pglCreateShader = (PFNGLCREATESHADERPROC)load_gl_proc("glCreateShader");
    pglShaderSource = (PFNGLSHADERSOURCEPROC)load_gl_proc("glShaderSource");
    pglCompileShader = (PFNGLCOMPILESHADERPROC)load_gl_proc("glCompileShader");
    pglGetShaderiv = (PFNGLGETSHADERIVPROC)load_gl_proc("glGetShaderiv");
    pglGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)load_gl_proc("glGetShaderInfoLog");
    pglCreateProgram = (PFNGLCREATEPROGRAMPROC)load_gl_proc("glCreateProgram");
    pglAttachShader = (PFNGLATTACHSHADERPROC)load_gl_proc("glAttachShader");
    pglLinkProgram = (PFNGLLINKPROGRAMPROC)load_gl_proc("glLinkProgram");
    pglGetProgramiv = (PFNGLGETPROGRAMIVPROC)load_gl_proc("glGetProgramiv");
    pglGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)load_gl_proc("glGetProgramInfoLog");
    pglUseProgram = (PFNGLUSEPROGRAMPROC)load_gl_proc("glUseProgram");
    pglGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)load_gl_proc("glGetUniformLocation");
    pglUniform1f = (PFNGLUNIFORM1FPROC)load_gl_proc("glUniform1f");
    pglUniform2f = (PFNGLUNIFORM2FPROC)load_gl_proc("glUniform2f");
    pglUniform3f = (PFNGLUNIFORM3FPROC)load_gl_proc("glUniform3f");
    pglDeleteShader = (PFNGLDELETESHADERPROC)load_gl_proc("glDeleteShader");

    return pglCreateShader && pglShaderSource && pglCompileShader && pglGetShaderiv &&
        pglGetShaderInfoLog && pglCreateProgram && pglAttachShader && pglLinkProgram &&
        pglGetProgramiv && pglGetProgramInfoLog && pglUseProgram && pglGetUniformLocation &&
        pglUniform1f && pglUniform2f && pglUniform3f && pglDeleteShader;
}

static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = pglCreateShader(type);
    GLint ok = 0;
    pglShaderSource(shader, 1, &source, NULL);
    pglCompileShader(shader);
    pglGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei length = 0;
        pglGetShaderInfoLog(shader, sizeof(log), &length, log);
        fprintf(stderr, "Shader compile failed: %.*s\n", (int)length, log);
        return 0;
    }
    return shader;
}

static GLuint create_program(const char *vertex, const char *fragment) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment);
    GLuint program;
    GLint ok = 0;

    if (vs == 0 || fs == 0) {
        return 0;
    }

    program = pglCreateProgram();
    pglAttachShader(program, vs);
    pglAttachShader(program, fs);
    pglLinkProgram(program);
    pglDeleteShader(vs);
    pglDeleteShader(fs);
    pglGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GLsizei length = 0;
        pglGetProgramInfoLog(program, sizeof(log), &length, log);
        fprintf(stderr, "Shader link failed: %.*s\n", (int)length, log);
        return 0;
    }
    return program;
}

static const char *bg_vertex_shader =
    "#version 120\n"
    "varying vec2 vUv;\n"
    "void main(){ vUv=gl_Vertex.xy*0.5+0.5; gl_Position=gl_Vertex; }\n";

static const char *bg_fragment_shader =
    "#version 120\n"
    "uniform float uTime; uniform vec2 uResolution; varying vec2 vUv;\n"
    "float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}\n"
    "float noise(vec2 p){vec2 i=floor(p);vec2 f=fract(p);vec2 u=f*f*f*(f*(f*6.0-15.0)+10.0);return mix(mix(hash(i),hash(i+vec2(1,0)),u.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),u.x),u.y);}\n"
    "float fbm(vec2 p){float v=0.0,a=0.5,n=0.0;mat2 r=mat2(0.8,-0.6,0.6,0.8);for(int i=0;i<5;i++){v+=noise(p)*a;n+=a;p=r*p*2.04+13.7;a*=0.5;}return v/n;}\n"
    "void main(){vec2 p=(vUv-0.5)*vec2(uResolution.x/max(1.0,uResolution.y),1.0);float b=fbm(p*2.1+vec2(uTime*.0025,-uTime*.0018));float g=fbm(p*4.4+vec2(uTime*.004,-uTime*.003));float br=fbm(p*1.008*2.1+vec2(uTime*.0025,-uTime*.0018));float bb=fbm(p*0.992*2.1+vec2(uTime*.0025,-uTime*.0018));float f=fbm(p*18.0-vec2(uTime*.007,uTime*.005));float thread=fbm(vec2(p.x*42.0+uTime*.011,p.y*7.0-uTime*.004));float vign=smoothstep(1.08,.16,length(p));float scan=pow(abs(sin(vUv.y*uResolution.y*1.570796+uTime*2.0)),10.0);float gr=smoothstep(.18,.85,b*.26+g*.48+f*.2+thread*.06);vec3 cold=vec3(.015,.052,.078);vec3 warm=vec3(.07,.018,.06);vec3 color=vec3(mix(cold.r,warm.r,br*.72+g*.28),mix(cold.g,warm.g,b*.72+g*.28),mix(cold.b,warm.b,bb*.72+g*.28));gl_FragColor=vec4(color+scan*.012,(gr*.75+scan*.04)*vign*.42);}\n";

static const char *ribbon_vertex_shader =
    "#version 120\n"
    "varying vec2 vUv; varying vec3 vColor;\n"
    "void main(){ vUv=gl_MultiTexCoord0.xy; vColor=gl_Color.rgb; gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex; }\n";

static const char *ribbon_fragment_shader =
    "#version 120\n"
    "uniform float uTime; uniform float uGrungeGlow; varying vec2 vUv; varying vec3 vColor;\n"
    "float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}\n" // No change here
    "float noise(vec2 p){vec2 i=floor(p);vec2 f=fract(p);vec2 u=f*f*f*(f*(f*6.0-15.0)+10.0);return mix(mix(hash(i),hash(i+vec2(1,0)),u.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),u.x),u.y);}\n"
    "float fbm(vec2 p){float v=0.0,a=.52,n=0.0;mat2 r=mat2(.82,-.57,.57,.82);for(int i=0;i<6;i++){v+=noise(p)*a;n+=a;p=r*p*2.08+vec2(11.3,-7.9);a*=.48;}return v/n;}\n"
    "void main(){float d=abs(vUv.y-.5)*2.0;float core=exp(-d*d*4.8);float halo=exp(-d*d*1.25);float feather=smoothstep(1.0,.62,d);float slow=fbm(vec2(vUv.x*3.8+uTime*.012,vUv.y*1.5-uTime*.006));float fine=fbm(vec2(vUv.x*58.0+uTime*.038,vUv.y*11.0-slow*1.35));float breath=.66+.34*fbm(vec2(vUv.x*7.0+slow*1.4,uTime*.027));float alpha=(.17*(1.0+uGrungeGlow*(.16+fine*.16)))*(halo*.42+core*(.55+fine*.18))*breath*feather;vec3 color=vColor*(1.06+core*.5+fine*.18);gl_FragColor=vec4(color*.85,alpha);}\n"; // Increased color multiplier to .85

static const char *point_vertex_shader =
    "#version 120\n"
    "uniform float uWake; varying vec3 vColor; varying float vAlpha;\n"
    "void main(){ vColor=gl_Color.rgb*(1.35+uWake*.75); vAlpha=gl_Color.a; gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex; gl_PointSize=gl_Color.a*(3.0+uWake*3.5); }\n";

static const char *point_fragment_shader =
    "#version 120\n"
    "varying vec3 vColor; varying float vAlpha;\n"
    "void main(){vec2 uv=gl_PointCoord-.5;float d=dot(uv,uv);float core=exp(-d*34.0);float halo=exp(-d*8.5);float a=(halo*.34+core*.92)*vAlpha*.38;gl_FragColor=vec4(vColor*(.42+core*.78),a);}\n";

static char *copy_range(const char *start, const char *end) {
    size_t length = (size_t)(end - start);
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

static char *extract_shader_section(const char *source, const char *name) {
    char startMarker[96];
    char endMarker[96];
    const char *start;
    const char *body;
    const char *end;

    snprintf(startMarker, sizeof(startMarker), "/// <%s>", name);
    snprintf(endMarker, sizeof(endMarker), "/// </%s>", name);
    start = strstr(source, startMarker);
    if (start == NULL) {
        return NULL;
    }

    body = strchr(start, '\n');
    if (body == NULL) {
        return NULL;
    }
    body += 1;

    end = strstr(body, endMarker);
    if (end == NULL || end <= body) {
        return NULL;
    }

    return copy_range(body, end);
}

static int get_shader_path(wchar_t *path, DWORD pathCount) {
    DWORD length = GetModuleFileNameW(NULL, path, pathCount);
    wchar_t *slash;
    if (length == 0 || length >= pathCount) {
        return 0;
    }

    slash = wcsrchr(path, L'\\');
    if (slash == NULL) {
        return 0;
    }

    slash[1] = L'\0';
    return lstrcatW(path, L"win32-desktop-layer.glsl") != NULL;
}

static char *read_entire_file_w(const wchar_t *path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;
    DWORD bytesRead = 0;
    char *buffer;

    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return NULL;
    }

    buffer = (char *)malloc((size_t)size.QuadPart + 1);
    if (buffer == NULL) {
        CloseHandle(file);
        return NULL;
    }

    if (!ReadFile(file, buffer, (DWORD)size.QuadPart, &bytesRead, NULL) || bytesRead != (DWORD)size.QuadPart) {
        free(buffer);
        CloseHandle(file);
        return NULL;
    }

    buffer[bytesRead] = '\0';
    CloseHandle(file);
    return buffer;
}

static void free_shader_sources(ShaderSources *sources) {
    free(sources->fileBuffer);
    free(sources->bgVertex);
    free(sources->bgFragment);
    free(sources->ribbonVertex);
    free(sources->ribbonFragment);
    free(sources->pointVertex);
    free(sources->pointFragment);
    ZeroMemory(sources, sizeof(*sources));
}

static int load_shader_sources_from_path(ShaderSources *sources, const wchar_t *path) {
    ZeroMemory(sources, sizeof(*sources));
    sources->fileBuffer = read_entire_file_w(path);
    if (sources->fileBuffer == NULL) {
        return 0;
    }

    sources->bgVertex = extract_shader_section(sources->fileBuffer, "bg_vertex");
    sources->bgFragment = extract_shader_section(sources->fileBuffer, "bg_fragment");
    sources->ribbonVertex = extract_shader_section(sources->fileBuffer, "ribbon_vertex");
    sources->ribbonFragment = extract_shader_section(sources->fileBuffer, "ribbon_fragment");
    sources->pointVertex = extract_shader_section(sources->fileBuffer, "point_vertex");
    sources->pointFragment = extract_shader_section(sources->fileBuffer, "point_fragment");

    if (sources->bgVertex == NULL || sources->bgFragment == NULL ||
        sources->ribbonVertex == NULL || sources->ribbonFragment == NULL ||
        sources->pointVertex == NULL || sources->pointFragment == NULL) {
        free_shader_sources(sources);
        return 0;
    }

    return 1;
}

static int load_shader_sources(ShaderSources *sources, const wchar_t *overridePath) {
    wchar_t path[MAX_PATH];

    if (overridePath != NULL && overridePath[0] != L'\0') {
        return load_shader_sources_from_path(sources, overridePath);
    }

    if (!get_shader_path(path, ARRAYSIZE(path))) {
        return 0;
    }

    return load_shader_sources_from_path(sources, path);
}

static LRESULT CALLBACK native_ribbon_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void shutdown_native_ribbon(NativeRibbon *app) {
    if (app->trayIcon.cbSize != 0) {
        Shell_NotifyIconW(NIM_DELETE, &app->trayIcon);
        ZeroMemory(&app->trayIcon, sizeof(app->trayIcon));
    }

    if (app->gl != NULL) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(app->gl);
        app->gl = NULL;
    }

    if (app->hwnd != NULL && app->dc != NULL) {
        ReleaseDC(app->hwnd, app->dc);
        app->dc = NULL;
    }

    if (app->hwnd != NULL && IsWindow(app->hwnd)) {
        DestroyWindow(app->hwnd);
        app->hwnd = NULL;
    }

    if (app->controlHwnd != NULL && IsWindow(app->controlHwnd)) {
        DestroyWindow(app->controlHwnd);
        app->controlHwnd = NULL;
    }
}

static void show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    POINT cursor;

    if (menu == NULL) {
        return;
    }

    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Shutdown Desktop Shader");
    GetCursorPos(&cursor);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, cursor.x, cursor.y, 0, hwnd, NULL);
    DestroyMenu(menu);
}

static LRESULT CALLBACK tray_control_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wparam) == ID_TRAY_EXIT) {
            PostQuitMessage(0);
            return 0;
        }
        break;
    case WM_TRAY_ICON:
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU || lparam == WM_LBUTTONUP) {
            show_tray_menu(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static int add_tray_icon(NativeRibbon *app) {
    ZeroMemory(&app->trayIcon, sizeof(app->trayIcon));
    app->trayIcon.cbSize = sizeof(app->trayIcon);
    app->trayIcon.hWnd = app->controlHwnd;
    app->trayIcon.uID = 1;
    app->trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    app->trayIcon.uCallbackMessage = WM_TRAY_ICON;
    app->trayIcon.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    lstrcpynW(app->trayIcon.szTip, L"Desktop Shader", ARRAYSIZE(app->trayIcon.szTip));

    if (!Shell_NotifyIconW(NIM_ADD, &app->trayIcon)) {
        ZeroMemory(&app->trayIcon, sizeof(app->trayIcon));
        return 0;
    }

    return 1;
}

static int init_gl(NativeRibbon *app) {
    PIXELFORMATDESCRIPTOR pfd;
    int pixelFormat;

    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 0;
    pfd.iLayerType = PFD_MAIN_PLANE;

    app->dc = GetDC(app->hwnd);
    pixelFormat = ChoosePixelFormat(app->dc, &pfd);
    if (pixelFormat == 0 || !SetPixelFormat(app->dc, pixelFormat, &pfd)) {
        fprintf(stderr, "Could not set OpenGL pixel format. error=%lu\n", GetLastError());
        return 0;
    }

    app->gl = wglCreateContext(app->dc);
    if (app->gl == NULL || !wglMakeCurrent(app->dc, app->gl)) {
        fprintf(stderr, "Could not create OpenGL context. error=%lu\n", GetLastError());
        return 0;
    }

    if (!load_gl_functions()) {
        fprintf(stderr, "OpenGL 2.0 shader functions are not available.\n");
        return 0;
    }

    ShaderSources shaderSources;
    if (load_shader_sources(&shaderSources, app->shaderPath)) {
        app->bgProgram = create_program(shaderSources.bgVertex, shaderSources.bgFragment);
        app->ribbonProgram = create_program(shaderSources.ribbonVertex, shaderSources.ribbonFragment);
        app->pointProgram = create_program(shaderSources.pointVertex, shaderSources.pointFragment);
        if (app->bgProgram == 0 || app->ribbonProgram == 0 || app->pointProgram == 0) {
            fprintf(stderr, "External shader file failed to compile; using embedded fallback shaders.\n");
            app->bgProgram = 0;
            app->ribbonProgram = 0;
            app->pointProgram = 0;
        }
        free_shader_sources(&shaderSources);
    }

    if (app->bgProgram == 0 || app->ribbonProgram == 0 || app->pointProgram == 0) {
        app->bgProgram = create_program(bg_vertex_shader, bg_fragment_shader);
        app->ribbonProgram = create_program(ribbon_vertex_shader, ribbon_fragment_shader);
        app->pointProgram = create_program(point_vertex_shader, point_fragment_shader);
    }
    if (app->bgProgram == 0 || app->ribbonProgram == 0 || app->pointProgram == 0) {
        return 0;
    }

    app->bgTimeLoc = pglGetUniformLocation(app->bgProgram, "uTime");
    app->bgResLoc = pglGetUniformLocation(app->bgProgram, "uResolution");
    app->ribbonTimeLoc = pglGetUniformLocation(app->ribbonProgram, "uTime");
    app->ribbonGrungeLoc = pglGetUniformLocation(app->ribbonProgram, "uGrungeGlow");
    app->pointWakeLoc = pglGetUniformLocation(app->pointProgram, "uWake");

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POINT_SPRITE);
    glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
    return 1;
}

static void resize_native_ribbon(NativeRibbon *app, HWND parent) {
    RECT rect;
    DesktopBounds virtualBounds = get_virtual_desktop_bounds();
    GetClientRect(parent, &rect);
    app->width = rect.right - rect.left;
    app->height = rect.bottom - rect.top;
    if (virtualBounds.width > app->width) app->width = virtualBounds.width;
    if (virtualBounds.height > app->height) app->height = virtualBounds.height;
    if (app->width <= 0) app->width = virtualBounds.width;
    if (app->height <= 0) app->height = virtualBounds.height;
    app->aspect = (float)app->width / (float)(app->height > 0 ? app->height : 1);
    app->span = app->aspect * 2.82f;
    SetWindowPos(app->hwnd, HWND_TOP, virtualBounds.x, virtualBounds.y, app->width, app->height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    glViewport(0, 0, app->width, app->height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-app->aspect, app->aspect, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void update_pointer(NativeRibbon *app, float dt) {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(app->hwnd, &pt);

    app->targetPointer.x = lerpf(-app->aspect, app->aspect, (float)pt.x / (float)(app->width > 0 ? app->width : 1));
    app->targetPointer.y = lerpf(1.0f, -1.0f, (float)pt.y / (float)(app->height > 0 ? app->height : 1));

    if (fabsf(app->previousPointer.x) < 10.0f && fabsf(app->previousPointer.y) < 10.0f) {
        float dx = app->targetPointer.x - app->previousPointer.x;
        float dy = app->targetPointer.y - app->previousPointer.y;
        app->targetPointerVelocity.x = clampf(dx * 9.5f, -1.1f, 1.1f);
        app->targetPointerVelocity.y = clampf(dy * 9.5f, -1.1f, 1.1f);
    }
    app->previousPointer = app->targetPointer;

    float pointerBlend = 1.0f - expf(-dt * 2.4f);
    float velocityBlend = 1.0f - expf(-dt * 8.5f);
    app->pointer.x = lerpf(app->pointer.x, app->targetPointer.x, pointerBlend);
    app->pointer.y = lerpf(app->pointer.y, app->targetPointer.y, pointerBlend);
    app->pointerVelocity.x = lerpf(app->pointerVelocity.x, app->targetPointerVelocity.x, velocityBlend);
    app->pointerVelocity.y = lerpf(app->pointerVelocity.y, app->targetPointerVelocity.y, velocityBlend);
    app->targetPointerVelocity.x *= expf(-dt * 4.4f);
    app->targetPointerVelocity.y *= expf(-dt * 4.4f);

    float motion = sqrtf(app->pointerVelocity.x * app->pointerVelocity.x + app->pointerVelocity.y * app->pointerVelocity.y);
    float targetWake = clampf(motion * 8.5f, 0.0f, 1.0f);
    app->pointerWake = lerpf(app->pointerWake, targetWake, 1.0f - expf(-dt * 7.2f));

    float nearLine = expf(-(app->targetPointer.y * app->targetPointer.y) * 28.0f);
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) && nearLine > 0.18f) {
        app->phase += 17.31f;
        app->pointerWake = 1.0f;
    }
}

static void draw_background(NativeRibbon *app, float elapsed) {
    pglUseProgram(app->bgProgram);
    if (app->bgResLoc >= 0) {
        pglUniform2f(app->bgResLoc, (float)app->width, (float)app->height);
    }
    pglUniform1f(app->bgTimeLoc, elapsed);
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();
}

static void draw_ribbon(NativeRibbon *app, float elapsed, float width, float alphaScale) {
    float grunge = 0.5f + 0.5f * fbm2(elapsed * 0.028f + app->phase, elapsed * -0.021f);
    pglUseProgram(app->ribbonProgram);
    pglUniform1f(app->ribbonTimeLoc, elapsed);
    pglUniform1f(app->ribbonGrungeLoc, grunge + app->pointerWake * 0.45f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_TRIANGLE_STRIP);

    float halfSpan = app->span * 0.5f;
    for (int i = 0; i < SAMPLE_COUNT; i += 1) {
        float t = (float)i / (float)(SAMPLE_COUNT - 1);
        Vec2 center = g_centerlineCache[i];
        float centerBias = 1.0f - fabsf(t - 0.5f) * 1.6f;
        float x = lerpf(-halfSpan, halfSpan, t);
        float dx = app->pointer.x - x;
        float dy = app->pointer.y;
        float distSq = dx * dx + dy * dy;
        float pointerInfluence = expf(-distSq * 9.0f) * app->pointerWake;
        float pointerLift = expf(-distSq * 10.5f) * clampf(app->pointer.y, -0.32f, 0.32f) * 0.045f;
        float directionalWake = (app->pointerVelocity.y * 0.032f +
            sinf(t * 75.398224f - elapsed * 1.2f) * app->pointerVelocity.x * 0.009f) * pointerInfluence;
        float edge = fabsf(fbm1(t * 34.0f + elapsed * 0.09f + app->phase * 0.05f)) * 0.18f +
            fabsf(fbm1(t * 119.0f - elapsed * 0.14f)) * 0.08f +
            fabsf(vortex_field(t, elapsed * 0.8f, app->phase + 41.0f)) * 2.6f;
        float localWidth = width * (0.46f + centerBias * 0.11f + edge + pointerInfluence * 0.72f);
        float y = center.y + pointerLift + directionalWake;
        float centerX = x + center.x;
        float spectrum = t * 680.0f + elapsed * 18.0f + fbm1(t * 10.8f + elapsed * 0.06f + app->phase) * 92.0f;
        float moodBand = lerpf(198.0f, 314.0f, 0.5f + 0.5f * sinf(t * 6.283185f - elapsed * 0.18f));
        float hue = fmodf(moodBand * 0.38f + spectrum * 0.62f, 360.0f) / 360.0f;
        float shimmer = 0.5f + 0.5f * sinf(t * 131.9469f - elapsed * 3.2f + fbm1(t * 27.0f) * 2.0f); // No change here
        Color color = hsl_to_rgb(hue, 1.0f, (0.7f + shimmer * 0.045f) * alphaScale); // Increased saturation to 1.0f and base lightness to 0.7f

        glColor4f(color.r, color.g, color.b, alphaScale);
        glTexCoord2f(t, 1.0f);
        glVertex2f(centerX, y + localWidth);
        glTexCoord2f(t, 0.0f);
        glVertex2f(centerX, y - localWidth);
    }

    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static Vec2 get_cached_centerline(float t, float *densityOut) {
    float indexF = t * (float)(SAMPLE_COUNT - 1);
    int i = (int)indexF;
    if (i < 0) i = 0;
    if (i >= SAMPLE_COUNT - 1) i = SAMPLE_COUNT - 2;
    float f = indexF - (float)i;
    if (densityOut) *densityOut = lerpf(g_densityCache[i], g_densityCache[i+1], f);
    Vec2 res = { lerpf(g_centerlineCache[i].x, g_centerlineCache[i+1].x, f), lerpf(g_centerlineCache[i].y, g_centerlineCache[i+1].y, f) };
    return res;
}

static void draw_particles(NativeRibbon *app, float elapsed) {
    const float golden = 0.61803398875f;
    pglUseProgram(app->pointProgram);
    pglUniform1f(app->pointWakeLoc, app->pointerWake);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBegin(GL_POINTS);

    float halfSpan = app->span * 0.5f;
    for (int i = 0; i < PARTICLE_COUNT; i += 1) {
        float seed = (float)i * 17.17f + app->phase;
        float direction = hash1(seed + 21.7f) > 0.5f ? 1.0f : -1.0f;
        float base = fmodf((float)i * golden, 1.0f);
        float t = fmodf(base + elapsed * direction * (0.001f + hash1(seed + 31.0f) * 0.0026f) + 1.0f, 1.0f);
        float density = 0.0f;
        Vec2 center = get_cached_centerline(t, &density);
        float x = lerpf(-halfSpan, halfSpan, t) + center.x;
        float y = center.y;
        float pointerDx = x - app->pointer.x;
        float pointerDy = y - app->pointer.y;
        float influence = expf(-(pointerDx * pointerDx + pointerDy * pointerDy) * 22.0f) * app->pointerWake;
        float orbit = elapsed * (0.028f + hash1(seed + 8.9f) * 0.052f) * direction + seed;
        float radius = 0.001f + hash1(seed + 5.2f) * 0.0032f + influence * 0.005f;
        float micro = noise1(seed * 0.11f + elapsed * 0.04f + influence * 0.6f) * (0.0016f + influence * 0.0026f);
        float h = 0.1375f + (t * 2.1111f + elapsed * 0.0333f + seed * 0.0025f);
        float hue = h - floorf(h);
        Color color = hsl_to_rgb(hue, 0.98f, clampf(0.62f * (0.82f + hash1(seed + 4.1f) * 0.22f + influence * 0.42f), 0.0f, 0.95f));
        float brightness = 1.35f + influence * 1.65f;
        glColor4f(color.r * brightness, color.g * brightness, color.b * brightness, 0.32f + density * 0.6f + influence * 0.9f);
        glVertex3f(
            x + cosf(orbit) * radius * 0.45f + app->pointerVelocity.x * influence * 0.05f,
            y + sinf(orbit) * radius + micro + app->pointerVelocity.y * influence * 0.05f,
            0.0f
        );
    }

    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void render_native_ribbon(NativeRibbon *app, float elapsed) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        float t = (float)i / (float)(SAMPLE_COUNT - 1);
        g_centerlineCache[i] = centerline_at(t, elapsed, app->phase, &g_densityCache[i]);
    }

    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    draw_background(app, elapsed);
    draw_ribbon(app, elapsed, GLOW_WIDTH, 0.64f);
    draw_ribbon(app, elapsed, LINE_WIDTH, 1.0f);
    draw_particles(app, elapsed);
    pglUseProgram(0);
    SwapBuffers(app->dc);
}

static int run_native_ribbon(const wchar_t *shaderPath) {
    HWND parent = find_desktop_wallpaper_parent();
    WNDCLASSW wc;
    WNDCLASSW trayWc;
    NativeRibbon app;
    RECT rect;
    DesktopBounds virtualBounds;
    LARGE_INTEGER freq;
    LARGE_INTEGER start;
    LARGE_INTEGER last;
    MSG msg;

    if (parent == NULL) {
        fprintf(stderr, "Could not find behind-icons WorkerW desktop parent for native ribbon.\n");
        return 2;
    }

    ZeroMemory(&app, sizeof(app));
    g_app = &app;
    app.shaderPath = shaderPath;
    app.pointer.x = app.targetPointer.x = app.previousPointer.x = 999.0f;
    app.pointer.y = app.targetPointer.y = app.previousPointer.y = 999.0f;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = native_ribbon_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DesktopPetNativeRibbon";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    ZeroMemory(&trayWc, sizeof(trayWc));
    trayWc.lpfnWndProc = tray_control_proc;
    trayWc.hInstance = GetModuleHandleW(NULL);
    trayWc.lpszClassName = L"DesktopPetNativeRibbonTray";
    RegisterClassW(&trayWc);

    app.controlHwnd = CreateWindowExW(
        0,
        trayWc.lpszClassName,
        L"Desktop Pet Native Ribbon Control",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        NULL,
        trayWc.hInstance,
        NULL
    );
    if (app.controlHwnd == NULL) {
        fprintf(stderr, "Could not create tray control window. error=%lu\n", GetLastError());
        return 5;
    }

    virtualBounds = get_virtual_desktop_bounds();
    GetClientRect(parent, &rect);
    app.width = rect.right - rect.left;
    app.height = rect.bottom - rect.top;
    if (virtualBounds.width > app.width) app.width = virtualBounds.width;
    if (virtualBounds.height > app.height) app.height = virtualBounds.height;
    if (app.width <= 0) app.width = virtualBounds.width;
    if (app.height <= 0) app.height = virtualBounds.height;

    app.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName,
        L"Desktop Pet Native Ribbon",
        WS_CHILD | WS_VISIBLE,
        virtualBounds.x,
        virtualBounds.y,
        app.width,
        app.height,
        parent,
        NULL,
        wc.hInstance,
        NULL
    );
    if (app.hwnd == NULL) {
        fprintf(stderr, "Could not create native ribbon window. error=%lu\n", GetLastError());
        shutdown_native_ribbon(&app);
        return 5;
    }

    if (!init_gl(&app)) {
        shutdown_native_ribbon(&app);
        return 6;
    }
    if (!add_tray_icon(&app)) {
        fprintf(stderr, "Could not add notification area icon. error=%lu\n", GetLastError());
    }
    resize_native_ribbon(&app, parent);
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    last = start;
    printf("native-ribbon hwnd=%p parent=%p size=%dx%d\n", app.hwnd, parent, app.width, app.height);

    for (;;) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                shutdown_native_ribbon(&app);
                g_app = NULL;
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float elapsed = (float)(now.QuadPart - start.QuadPart) / (float)freq.QuadPart * 0.26f;
        float dt = (float)(now.QuadPart - last.QuadPart) / (float)freq.QuadPart;
        if (dt < 0.016f) {
            Sleep(5);
            continue;
        }
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        update_pointer(&app, dt);
        render_native_ribbon(&app, elapsed);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return run_native_ribbon(NULL);
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        fprintf(stderr, "Usage: win32-desktop-layer.exe [--shader <file.glsl>] [hwnd]\n");
        fprintf(stderr, "  no args:              run the native GLSL desktop ribbon\n");
        fprintf(stderr, "  --shader <file.glsl>: run native ribbon with an explicit shader file\n");
        fprintf(stderr, "  --shader=<file.glsl>: same as --shader <file.glsl>\n");
        fprintf(stderr, "  hwnd:                 attach an existing window to the desktop layer\n");
        return 0;
    }

    if (strncmp(argv[1], "--shader=", 9) == 0) {
        wchar_t shaderPath[MAX_PATH];
        const char *pathArg = argv[1] + 9;
        if (pathArg[0] == '\0') {
            fprintf(stderr, "--shader requires a file path.\n");
            return 1;
        }

        if (MultiByteToWideChar(CP_ACP, 0, pathArg, -1, shaderPath, ARRAYSIZE(shaderPath)) == 0) {
            fprintf(stderr, "Could not parse shader path: %s\n", pathArg);
            return 1;
        }

        return run_native_ribbon(shaderPath);
    }

    if (strcmp(argv[1], "--shader") == 0 || strcmp(argv[1], "-s") == 0) {
        wchar_t shaderPath[MAX_PATH];
        if (argc < 3) {
            fprintf(stderr, "--shader requires a file path.\n");
            return 1;
        }

        if (MultiByteToWideChar(CP_ACP, 0, argv[2], -1, shaderPath, ARRAYSIZE(shaderPath)) == 0) {
            fprintf(stderr, "Could not parse shader path: %s\n", argv[2]);
            return 1;
        }

        return run_native_ribbon(shaderPath);
    }

    uintptr_t value = (uintptr_t)strtoull(argv[1], NULL, 0);
    HWND target = (HWND)value;
    if (target == NULL || !IsWindow(target)) {
        fprintf(stderr, "Invalid HWND: %s\n", argv[1]);
        return 1;
    }

    return attach_to_desktop(target);
}
