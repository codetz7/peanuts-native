#ifdef __ANDROID__

#include "../host/hle.h"
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

typedef void *EGLDisplay;
typedef uint32_t EGLBoolean;
typedef int32_t EGLint;

#define EGLH(name, ret, ...) \
    static ret h_##name(__VA_ARGS__) __attribute__((used)); \
    extern const char h_##name##_bs[]; \
    __asm__(".text\n.p2align 2\n" \
        ".globl " PLAT_ASM_SYM(h_##name##_bs) "\n" \
        PLAT_ASM_HIDDEN " " PLAT_ASM_SYM(h_##name##_bs) "\n" \
        PLAT_ASM_SYM(h_##name##_bs) ":\n" \
        "adrp x16, " PLAT_ASM_PAGE(h_##name) "\n" \
        "add x16, x16, " PLAT_ASM_PAGEOFF(h_##name) "\n" \
        "b " PLAT_ASM_SYM(dm_bigstack_enter) "\n"); \
    PLAT_TABLE(swhle) static const hle_ent e_##name = \
        {#name, (void *)h_##name##_bs}; \
    static ret h_##name(__VA_ARGS__)

static void *egl_lib;

static void *egl_symbol(const char *name)
{
    if (!egl_lib)
        egl_lib = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);

    if (!egl_lib) {
        LOG("[gl-android] dlopen EGL failed: %s", dlerror());
        return NULL;
    }

    return dlsym(egl_lib, name);
}

EGLH(eglGetDisplay, EGLDisplay, void *native)
{
    (void)native;
    EGLDisplay (*fn)(void *) = egl_symbol("eglGetDisplay");
    EGLDisplay d = fn ? fn(NULL) : NULL;
    LOG("[gl-android] eglGetDisplay = %p", d);
    return d;
}

EGLH(eglInitialize, EGLBoolean,
     EGLDisplay d, EGLint *major, EGLint *minor)
{
    EGLBoolean (*fn)(EGLDisplay, EGLint *, EGLint *) =
        egl_symbol("eglInitialize");

    EGLBoolean ok = fn ? fn(d, major, minor) : 0;
    LOG("[gl-android] eglInitialize = %u", ok);
    return ok;
}

EGLH(eglGetError, EGLint, void)
{
    EGLint (*fn)(void) = egl_symbol("eglGetError");
    return fn ? fn() : 0x3001;
}


typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;

EGLH(eglBindAPI, EGLBoolean, uint32_t api)
{
    EGLBoolean (*fn)(uint32_t) =
        egl_symbol("eglBindAPI");

    if (api == 0x30A2)
        api = 0x30A0;

    return fn ? fn(api) : 0;
}

EGLH(eglChooseConfig, EGLBoolean,
     EGLDisplay d, const EGLint *attr,
     EGLConfig *cfg, EGLint n, EGLint *num)
{
    EGLBoolean (*fn)(EGLDisplay, const EGLint *,
        EGLConfig *, EGLint, EGLint *) =
        egl_symbol("eglChooseConfig");

    const EGLint attrs[] = {
        0x3033, 1,
        0x3040, 0x40,
        0x3024, 8,
        0x3023, 8,
        0x3022, 8,
        0x3021, 8,
        0x3038
    };

    (void)attr;
    return fn ? fn(d, attrs, cfg, n, num) : 0;
}

EGLH(eglCreateContext, EGLContext,
     EGLDisplay d, EGLConfig cfg,
     EGLContext share, const EGLint *attr)
{
    EGLContext (*fn)(EGLDisplay, EGLConfig,
        EGLContext, const EGLint *) =
        egl_symbol("eglCreateContext");

    const EGLint attrs[] = {
        0x3098, 3,
        0x3038
    };

    (void)attr;
    EGLContext c = fn ?
        fn(d, cfg, share, attrs) : NULL;

    LOG("[gl-android] GLES context = %p", c);
    return c;
}

EGLH(eglCreateWindowSurface, EGLSurface,
     EGLDisplay d, EGLConfig cfg,
     void *win, const EGLint *attr)
{
    EGLSurface (*fn)(EGLDisplay, EGLConfig,
        const EGLint *) =
        egl_symbol("eglCreatePbufferSurface");

    const EGLint attrs[] = {
        0x3057, 1600,
        0x3056, 900,
        0x3038
    };

    (void)win;
    (void)attr;

    EGLSurface s = fn ?
        fn(d, cfg, attrs) : NULL;

    LOG("[gl-android] offscreen surface = %p", s);
    return s;
}

EGLH(eglMakeCurrent, EGLBoolean,
     EGLDisplay d, EGLSurface draw,
     EGLSurface read, EGLContext c)
{
    EGLBoolean (*fn)(EGLDisplay, EGLSurface,
        EGLSurface, EGLContext) =
        egl_symbol("eglMakeCurrent");

    static _Thread_local EGLSurface fallback;
    static _Thread_local EGLDisplay fallback_display;

    LOG("[gl-android] MakeCurrent ctx=%p draw=%p read=%p",
        c, draw, read);

    if (c && !draw && !read) {
        if (!fallback || fallback_display != d) {
            EGLConfig cfg = NULL;
            EGLint count = 0;

            EGLBoolean (*choose)(EGLDisplay,
                const EGLint *, EGLConfig *,
                EGLint, EGLint *) =
                egl_symbol("eglChooseConfig");

            EGLSurface (*create)(EGLDisplay,
                EGLConfig, const EGLint *) =
                egl_symbol("eglCreatePbufferSurface");

            const EGLint attrs[] = {
                0x3033, 1,
                0x3040, 0x40,
                0x3024, 8,
                0x3023, 8,
                0x3022, 8,
                0x3021, 8,
                0x3038
            };

            const EGLint surface_attrs[] = {
                0x3057, 1600,
                0x3056, 900,
                0x3038
            };

            if (choose && create &&
                choose(d, attrs, &cfg, 1, &count)
                && count > 0) {
                fallback = create(d, cfg, surface_attrs);
                fallback_display = d;
            }
        }

        if (fallback) {
            draw = fallback;
            read = fallback;
        }

        LOG("[gl-android] fallback surface=%p", fallback);
    }

    EGLBoolean ok = fn ?
        fn(d, draw, read, c) : 0;

    LOG("[gl-android] eglMakeCurrent = %u", ok);
    return ok;
}

static void *gl_symbol(const char *name)
{
    void *(*fn)(const char *) =
        egl_symbol("eglGetProcAddress");

    return fn ? fn(name) : NULL;
}


typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat, GLclampf;
typedef char GLchar;
typedef intptr_t GLintptr, GLsizeiptr;
typedef int64_t GLint64;
typedef uint64_t GLuint64;
typedef void *GLsync;


EGLH(glActiveTexture, void, GLenum texture)
{
    LOG("[gl-trace] glActiveTexture");
    void (*fn)(GLenum texture) = gl_symbol("glActiveTexture");
    if (fn) fn(texture);
}

EGLH(glAttachShader, void, GLuint program, GLuint shader)
{
    LOG("[gl-trace] glAttachShader");
    void (*fn)(GLuint program, GLuint shader) = gl_symbol("glAttachShader");
    if (fn) fn(program, shader);
}

EGLH(glBindAttribLocation, void, GLuint program, GLuint index, const GLchar *name)
{
    LOG("[gl-trace] glBindAttribLocation");
    void (*fn)(GLuint program, GLuint index, const GLchar *name) = gl_symbol("glBindAttribLocation");
    if (fn) fn(program, index, name);
}

EGLH(glBindBuffer, void, GLenum target, GLuint buffer)
{
    LOG("[gl-bind] target=0x%x buffer=%u", target, buffer);
    void (*fn)(GLenum target, GLuint buffer) = gl_symbol("glBindBuffer");
    if (fn) fn(target, buffer);
}

EGLH(glBindFramebuffer, void, GLenum target, GLuint framebuffer)
{
    LOG("[gl-trace] glBindFramebuffer");
    void (*fn)(GLenum target, GLuint framebuffer) = gl_symbol("glBindFramebuffer");
    if (fn) fn(target, framebuffer);
}

EGLH(glBindRenderbuffer, void, GLenum target, GLuint renderbuffer)
{
    LOG("[gl-trace] glBindRenderbuffer");
    void (*fn)(GLenum target, GLuint renderbuffer) = gl_symbol("glBindRenderbuffer");
    if (fn) fn(target, renderbuffer);
}

EGLH(glBindTexture, void, GLenum target, GLuint texture)
{
    LOG("[gl-trace] glBindTexture");
    void (*fn)(GLenum target, GLuint texture) = gl_symbol("glBindTexture");
    if (fn) fn(target, texture);
}

EGLH(glBlendEquation, void, GLenum mode)
{
    LOG("[gl-trace] glBlendEquation");
    void (*fn)(GLenum mode) = gl_symbol("glBlendEquation");
    if (fn) fn(mode);
}

EGLH(glBlendEquationSeparate, void, GLenum modeRGB, GLenum modeAlpha)
{
    LOG("[gl-trace] glBlendEquationSeparate");
    void (*fn)(GLenum modeRGB, GLenum modeAlpha) = gl_symbol("glBlendEquationSeparate");
    if (fn) fn(modeRGB, modeAlpha);
}

EGLH(glBlendFunc, void, GLenum sfactor, GLenum dfactor)
{
    LOG("[gl-trace] glBlendFunc");
    void (*fn)(GLenum sfactor, GLenum dfactor) = gl_symbol("glBlendFunc");
    if (fn) fn(sfactor, dfactor);
}

EGLH(glBlendFuncSeparate, void, GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)
{
    LOG("[gl-trace] glBlendFuncSeparate");
    void (*fn)(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) = gl_symbol("glBlendFuncSeparate");
    if (fn) fn(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha);
}

EGLH(glBufferData, void, GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    LOG("[gl-trace] glBufferData");
    void (*fn)(GLenum target, GLsizeiptr size, const void *data, GLenum usage) = gl_symbol("glBufferData");
    if (fn) fn(target, size, data, usage);
}

EGLH(glBufferSubData, void, GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
    LOG("[gl-trace] glBufferSubData");
    void (*fn)(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) = gl_symbol("glBufferSubData");
    if (fn) fn(target, offset, size, data);
}

EGLH(glCheckFramebufferStatus, GLenum, GLenum target)
{
    LOG("[gl-trace] glCheckFramebufferStatus");
    GLenum (*fn)(GLenum target) = gl_symbol("glCheckFramebufferStatus");
    return fn ? fn(target) : 0;
}

EGLH(glClear, void, GLbitfield mask)
{
    LOG("[gl-trace] glClear");
    void (*fn)(GLbitfield mask) = gl_symbol("glClear");
    if (fn) fn(mask);
}

EGLH(glClearColor, void, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    LOG("[gl-trace] glClearColor");
    void (*fn)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) = gl_symbol("glClearColor");
    if (fn) fn(red, green, blue, alpha);
}

EGLH(glClientWaitSync, GLenum, GLsync sync, GLbitfield flags, GLuint64 timeout)
{
    LOG("[gl-trace] glClientWaitSync");
    GLenum (*fn)(GLsync sync, GLbitfield flags, GLuint64 timeout) = gl_symbol("glClientWaitSync");
    return fn ? fn(sync, flags, timeout) : 0;
}

EGLH(glColorMask, void, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    LOG("[gl-trace] glColorMask");
    void (*fn)(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) = gl_symbol("glColorMask");
    if (fn) fn(red, green, blue, alpha);
}

EGLH(glCompileShader, void, GLuint shader)
{
    LOG("[gl-trace] glCompileShader");
    void (*fn)(GLuint shader) = gl_symbol("glCompileShader");
    if (fn) fn(shader);
}

EGLH(glCompressedTexImage2D, void, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data)
{
    LOG("[gl-trace] glCompressedTexImage2D");
    void (*fn)(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) = gl_symbol("glCompressedTexImage2D");
    if (fn) fn(target, level, internalformat, width, height, border, imageSize, data);
}

EGLH(glCreateProgram, GLuint, void)
{
    LOG("[gl-trace] glCreateProgram");
    GLuint (*fn)(void) = gl_symbol("glCreateProgram");
    return fn ? fn() : 0;
}

EGLH(glCreateShader, GLuint, GLenum type)
{
    LOG("[gl-trace] glCreateShader");
    GLuint (*fn)(GLenum type) = gl_symbol("glCreateShader");
    return fn ? fn(type) : 0;
}

EGLH(glDeleteBuffers, void, GLsizei n, const GLuint *buffers)
{
    LOG("[gl-trace] glDeleteBuffers");
    void (*fn)(GLsizei n, const GLuint *buffers) = gl_symbol("glDeleteBuffers");
    if (fn) fn(n, buffers);
}

EGLH(glDeleteFramebuffers, void, GLsizei n, const GLuint *framebuffers)
{
    LOG("[gl-trace] glDeleteFramebuffers");
    void (*fn)(GLsizei n, const GLuint *framebuffers) = gl_symbol("glDeleteFramebuffers");
    if (fn) fn(n, framebuffers);
}

EGLH(glDeleteProgram, void, GLuint program)
{
    LOG("[gl-trace] glDeleteProgram");
    void (*fn)(GLuint program) = gl_symbol("glDeleteProgram");
    if (fn) fn(program);
}

EGLH(glDeleteRenderbuffers, void, GLsizei n, const GLuint *renderbuffers)
{
    LOG("[gl-trace] glDeleteRenderbuffers");
    void (*fn)(GLsizei n, const GLuint *renderbuffers) = gl_symbol("glDeleteRenderbuffers");
    if (fn) fn(n, renderbuffers);
}

EGLH(glDeleteShader, void, GLuint shader)
{
    LOG("[gl-trace] glDeleteShader");
    void (*fn)(GLuint shader) = gl_symbol("glDeleteShader");
    if (fn) fn(shader);
}

EGLH(glDeleteSync, void, GLsync sync)
{
    LOG("[gl-trace] glDeleteSync");
    void (*fn)(GLsync sync) = gl_symbol("glDeleteSync");
    if (fn) fn(sync);
}

EGLH(glDeleteTextures, void, GLsizei n, const GLuint *textures)
{
    LOG("[gl-trace] glDeleteTextures");
    void (*fn)(GLsizei n, const GLuint *textures) = gl_symbol("glDeleteTextures");
    if (fn) fn(n, textures);
}

EGLH(glDepthFunc, void, GLenum func)
{
    LOG("[gl-trace] glDepthFunc");
    void (*fn)(GLenum func) = gl_symbol("glDepthFunc");
    if (fn) fn(func);
}

EGLH(glDepthMask, void, GLboolean flag)
{
    LOG("[gl-trace] glDepthMask");
    void (*fn)(GLboolean flag) = gl_symbol("glDepthMask");
    if (fn) fn(flag);
}

EGLH(glDisable, void, GLenum cap)
{
    LOG("[gl-trace] glDisable");
    void (*fn)(GLenum cap) = gl_symbol("glDisable");
    if (fn) fn(cap);
}

EGLH(glDisableVertexAttribArray, void, GLuint index)
{
    LOG("[gl-trace] glDisableVertexAttribArray");
    void (*fn)(GLuint index) = gl_symbol("glDisableVertexAttribArray");
    if (fn) fn(index);
}

EGLH(glDrawArrays, void, GLenum mode, GLint first, GLsizei count)
{
    LOG("[gl-trace] glDrawArrays");
    void (*fn)(GLenum mode, GLint first, GLsizei count) = gl_symbol("glDrawArrays");
    if (fn) fn(mode, first, count);
}

EGLH(glDrawElements, void, GLenum mode, GLsizei count, GLenum type, const void *indices)
{

    GLint vbo = 0, ebo = 0, fbo = 0;
    GLint program = 0, texture = 0;

    void (*get)(GLenum, GLint *) =
        gl_symbol("glGetIntegerv");

    GLenum (*err)(void) =
        gl_symbol("glGetError");

    if (get) {
        get(0x8894, &vbo);
        get(0x8895, &ebo);
        get(0x8CA6, &fbo);
        get(0x8B8D, &program);
        get(0x8069, &texture);
    }

    LOG("[gl-draw] mode=0x%x count=%d type=0x%x indices=%p",
        mode, count, type, indices);

    LOG("[gl-draw] VBO=%d EBO=%d FBO=%d program=%d texture=%d error=0x%x",
        vbo, ebo, fbo, program, texture, err ? err() : 0);

    void (*fn)(GLenum mode, GLsizei count, GLenum type, const void *indices) = gl_symbol("glDrawElements");
    if (fn) fn(mode, count, type, indices);
}

EGLH(glEnable, void, GLenum cap)
{
    LOG("[gl-trace] glEnable");
    void (*fn)(GLenum cap) = gl_symbol("glEnable");
    if (fn) fn(cap);
}

EGLH(glEnableVertexAttribArray, void, GLuint index)
{
    LOG("[gl-trace] glEnableVertexAttribArray");
    void (*fn)(GLuint index) = gl_symbol("glEnableVertexAttribArray");
    if (fn) fn(index);
}

EGLH(glFenceSync, GLsync, GLenum condition, GLbitfield flags)
{
    LOG("[gl-trace] glFenceSync");
    GLsync (*fn)(GLenum condition, GLbitfield flags) = gl_symbol("glFenceSync");
    return fn ? fn(condition, flags) : 0;
}

EGLH(glFinish, void, void)
{
    LOG("[gl-trace] glFinish");
    void (*fn)(void) = gl_symbol("glFinish");
    if (fn) fn();
}

EGLH(glFlush, void, void)
{
    extern void dm_v9_upload_ui(void);
    dm_v9_upload_ui();

    LOG("[gl-trace] glFlush");
    void (*fn)(void) = gl_symbol("glFlush");
    if (fn) fn();
}

EGLH(glFramebufferRenderbuffer, void, GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
    LOG("[gl-trace] glFramebufferRenderbuffer");
    void (*fn)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) = gl_symbol("glFramebufferRenderbuffer");
    if (fn) fn(target, attachment, renderbuffertarget, renderbuffer);
}

EGLH(glFramebufferTexture2D, void, GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    LOG("[gl-trace] glFramebufferTexture2D");
    void (*fn)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) = gl_symbol("glFramebufferTexture2D");
    if (fn) fn(target, attachment, textarget, texture, level);
}

EGLH(glGenBuffers, void, GLsizei n, GLuint *buffers)
{
    LOG("[gl-trace] glGenBuffers");
    void (*fn)(GLsizei n, GLuint *buffers) = gl_symbol("glGenBuffers");
    if (fn) fn(n, buffers);
}

EGLH(glGenFramebuffers, void, GLsizei n, GLuint *framebuffers)
{
    LOG("[gl-trace] glGenFramebuffers");
    void (*fn)(GLsizei n, GLuint *framebuffers) = gl_symbol("glGenFramebuffers");
    if (fn) fn(n, framebuffers);
}

EGLH(glGenRenderbuffers, void, GLsizei n, GLuint *renderbuffers)
{
    LOG("[gl-trace] glGenRenderbuffers");
    void (*fn)(GLsizei n, GLuint *renderbuffers) = gl_symbol("glGenRenderbuffers");
    if (fn) fn(n, renderbuffers);
}

EGLH(glGenTextures, void, GLsizei n, GLuint *textures)
{
    LOG("[gl-trace] glGenTextures");
    void (*fn)(GLsizei n, GLuint *textures) = gl_symbol("glGenTextures");
    if (fn) fn(n, textures);
}

EGLH(glGenerateMipmap, void, GLenum target)
{
    LOG("[gl-trace] glGenerateMipmap");
    void (*fn)(GLenum target) = gl_symbol("glGenerateMipmap");
    if (fn) fn(target);
}

EGLH(glGetActiveUniform, void, GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name)
{
    LOG("[gl-trace] glGetActiveUniform");
    void (*fn)(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) = gl_symbol("glGetActiveUniform");
    if (fn) fn(program, index, bufSize, length, size, type, name);
}

EGLH(glGetError, GLenum, void)
{
    LOG("[gl-trace] glGetError");
    GLenum (*fn)(void) = gl_symbol("glGetError");
    return fn ? fn() : 0;
}

EGLH(glGetFloatv, void, GLenum pname, GLfloat *data)
{
    LOG("[gl-trace] glGetFloatv");
    void (*fn)(GLenum pname, GLfloat *data) = gl_symbol("glGetFloatv");
    if (fn) fn(pname, data);
}

EGLH(glGetFramebufferAttachmentParameteriv, void, GLenum target, GLenum attachment, GLenum pname, GLint *params)
{
    LOG("[gl-trace] glGetFramebufferAttachmentParameteriv");
    void (*fn)(GLenum target, GLenum attachment, GLenum pname, GLint *params) = gl_symbol("glGetFramebufferAttachmentParameteriv");
    if (fn) fn(target, attachment, pname, params);
}

EGLH(glGetIntegerv, void, GLenum pname, GLint *data)
{
    LOG("[gl-trace] glGetIntegerv pname=0x%x data=%p", pname, data);
    void (*fn)(GLenum pname, GLint *data) = gl_symbol("glGetIntegerv");
    if (fn) fn(pname, data);
}

EGLH(glGetProgramInfoLog, void, GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    LOG("[gl-trace] glGetProgramInfoLog");
    void (*fn)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) = gl_symbol("glGetProgramInfoLog");
    if (fn) fn(program, bufSize, length, infoLog);
}

EGLH(glGetProgramiv, void, GLuint program, GLenum pname, GLint *params)
{
    LOG("[gl-trace] glGetProgramiv");
    void (*fn)(GLuint program, GLenum pname, GLint *params) = gl_symbol("glGetProgramiv");
    if (fn) fn(program, pname, params);
}

EGLH(glGetRenderbufferParameteriv, void, GLenum target, GLenum pname, GLint *params)
{
    LOG("[gl-trace] glGetRenderbufferParameteriv");
    void (*fn)(GLenum target, GLenum pname, GLint *params) = gl_symbol("glGetRenderbufferParameteriv");
    if (fn) fn(target, pname, params);
}

EGLH(glGetShaderInfoLog, void, GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    LOG("[gl-trace] glGetShaderInfoLog");
    void (*fn)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) = gl_symbol("glGetShaderInfoLog");
    if (fn) fn(shader, bufSize, length, infoLog);
}

EGLH(glGetShaderiv, void, GLuint shader, GLenum pname, GLint *params)
{
    LOG("[gl-trace] glGetShaderiv");
    void (*fn)(GLuint shader, GLenum pname, GLint *params) = gl_symbol("glGetShaderiv");
    if (fn) fn(shader, pname, params);
}

EGLH(glGetString, const GLubyte *, GLenum name)
{
    LOG("[gl-trace] glGetString");
    const GLubyte * (*fn)(GLenum name) = gl_symbol("glGetString");
    return fn ? fn(name) : 0;
}

EGLH(glGetStringi, const GLubyte *, GLenum name, GLuint index)
{
    LOG("[gl-trace] glGetStringi");
    const GLubyte * (*fn)(GLenum name, GLuint index) = gl_symbol("glGetStringi");
    return fn ? fn(name, index) : 0;
}

EGLH(glGetTexParameteriv, void, GLenum target, GLenum pname, GLint *params)
{
    LOG("[gl-trace] glGetTexParameteriv");
    void (*fn)(GLenum target, GLenum pname, GLint *params) = gl_symbol("glGetTexParameteriv");
    if (fn) fn(target, pname, params);
}

EGLH(glGetUniformLocation, GLint, GLuint program, const GLchar *name)
{
    LOG("[gl-trace] glGetUniformLocation");
    GLint (*fn)(GLuint program, const GLchar *name) = gl_symbol("glGetUniformLocation");
    return fn ? fn(program, name) : 0;
}

EGLH(glIsFramebuffer, GLboolean, GLuint framebuffer)
{
    LOG("[gl-trace] glIsFramebuffer");
    GLboolean (*fn)(GLuint framebuffer) = gl_symbol("glIsFramebuffer");
    return fn ? fn(framebuffer) : 0;
}

EGLH(glIsProgram, GLboolean, GLuint program)
{
    LOG("[gl-trace] glIsProgram");
    GLboolean (*fn)(GLuint program) = gl_symbol("glIsProgram");
    return fn ? fn(program) : 0;
}

EGLH(glIsRenderbuffer, GLboolean, GLuint renderbuffer)
{
    LOG("[gl-trace] glIsRenderbuffer");
    GLboolean (*fn)(GLuint renderbuffer) = gl_symbol("glIsRenderbuffer");
    return fn ? fn(renderbuffer) : 0;
}

EGLH(glLinkProgram, void, GLuint program)
{
    LOG("[gl-trace] glLinkProgram");
    void (*fn)(GLuint program) = gl_symbol("glLinkProgram");
    if (fn) fn(program);
}

EGLH(glPixelStorei, void, GLenum pname, GLint param)
{
    LOG("[gl-trace] glPixelStorei");
    void (*fn)(GLenum pname, GLint param) = gl_symbol("glPixelStorei");
    if (fn) fn(pname, param);
}

EGLH(glReadPixels, void, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)
{
    LOG("[gl-trace] glReadPixels");
    void (*fn)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels) = gl_symbol("glReadPixels");
    if (fn) fn(x, y, width, height, format, type, pixels);
}

EGLH(glReleaseShaderCompiler, void, void)
{
    LOG("[gl-trace] glReleaseShaderCompiler");
    void (*fn)(void) = gl_symbol("glReleaseShaderCompiler");
    if (fn) fn();
}

EGLH(glRenderbufferStorage, void, GLenum target, GLenum internalformat, GLsizei width, GLsizei height)
{
    LOG("[gl-trace] glRenderbufferStorage");
    void (*fn)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) = gl_symbol("glRenderbufferStorage");
    if (fn) fn(target, internalformat, width, height);
}

EGLH(glScissor, void, GLint x, GLint y, GLsizei width, GLsizei height)
{
    LOG("[gl-trace] glScissor");
    void (*fn)(GLint x, GLint y, GLsizei width, GLsizei height) = gl_symbol("glScissor");
    if (fn) fn(x, y, width, height);
}

EGLH(glShaderSource, void, GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length)
{
    LOG("[gl-trace] glShaderSource");
    void (*fn)(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length) = gl_symbol("glShaderSource");
    if (fn) fn(shader, count, string, length);
}

EGLH(glStencilFunc, void, GLenum func, GLint ref, GLuint mask)
{
    LOG("[gl-trace] glStencilFunc");
    void (*fn)(GLenum func, GLint ref, GLuint mask) = gl_symbol("glStencilFunc");
    if (fn) fn(func, ref, mask);
}

EGLH(glStencilMask, void, GLuint mask)
{
    LOG("[gl-trace] glStencilMask");
    void (*fn)(GLuint mask) = gl_symbol("glStencilMask");
    if (fn) fn(mask);
}

EGLH(glStencilOp, void, GLenum fail, GLenum zfail, GLenum zpass)
{
    LOG("[gl-trace] glStencilOp");
    void (*fn)(GLenum fail, GLenum zfail, GLenum zpass) = gl_symbol("glStencilOp");
    if (fn) fn(fail, zfail, zpass);
}

EGLH(glTexImage2D, void, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)
{
    LOG("[gl-trace] glTexImage2D");
    void (*fn)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) = gl_symbol("glTexImage2D");
    if (fn) fn(target, level, internalformat, width, height, border, format, type, pixels);
}

EGLH(glTexParameteri, void, GLenum target, GLenum pname, GLint param)
{
    LOG("[gl-trace] glTexParameteri");
    void (*fn)(GLenum target, GLenum pname, GLint param) = gl_symbol("glTexParameteri");
    if (fn) fn(target, pname, param);
}

EGLH(glTexStorage2D, void, GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height)
{
    LOG("[gl-trace] glTexStorage2D");
    void (*fn)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height) = gl_symbol("glTexStorage2D");
    if (fn) fn(target, levels, internalformat, width, height);
}

EGLH(glTexSubImage2D, void, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
    LOG("[gl-trace] glTexSubImage2D");
    void (*fn)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) = gl_symbol("glTexSubImage2D");
    if (fn) fn(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

EGLH(glUniform1f, void, GLint location, GLfloat v0)
{
    LOG("[gl-trace] glUniform1f");
    void (*fn)(GLint location, GLfloat v0) = gl_symbol("glUniform1f");
    if (fn) fn(location, v0);
}

EGLH(glUniform1fv, void, GLint location, GLsizei count, const GLfloat *value)
{
    LOG("[gl-trace] glUniform1fv");
    void (*fn)(GLint location, GLsizei count, const GLfloat *value) = gl_symbol("glUniform1fv");
    if (fn) fn(location, count, value);
}

EGLH(glUniform1i, void, GLint location, GLint v0)
{
    LOG("[gl-trace] glUniform1i");
    void (*fn)(GLint location, GLint v0) = gl_symbol("glUniform1i");
    if (fn) fn(location, v0);
}

EGLH(glUniform1iv, void, GLint location, GLsizei count, const GLint *value)
{
    LOG("[gl-trace] glUniform1iv");
    void (*fn)(GLint location, GLsizei count, const GLint *value) = gl_symbol("glUniform1iv");
    if (fn) fn(location, count, value);
}

EGLH(glUniform2f, void, GLint location, GLfloat v0, GLfloat v1)
{
    LOG("[gl-trace] glUniform2f");
    void (*fn)(GLint location, GLfloat v0, GLfloat v1) = gl_symbol("glUniform2f");
    if (fn) fn(location, v0, v1);
}

EGLH(glUniform2fv, void, GLint location, GLsizei count, const GLfloat *value)
{
    LOG("[gl-trace] glUniform2fv");
    void (*fn)(GLint location, GLsizei count, const GLfloat *value) = gl_symbol("glUniform2fv");
    if (fn) fn(location, count, value);
}

EGLH(glUniform3fv, void, GLint location, GLsizei count, const GLfloat *value)
{
    LOG("[gl-trace] glUniform3fv");
    void (*fn)(GLint location, GLsizei count, const GLfloat *value) = gl_symbol("glUniform3fv");
    if (fn) fn(location, count, value);
}

EGLH(glUniform4fv, void, GLint location, GLsizei count, const GLfloat *value)
{
    LOG("[gl-trace] glUniform4fv");
    void (*fn)(GLint location, GLsizei count, const GLfloat *value) = gl_symbol("glUniform4fv");
    if (fn) fn(location, count, value);
}

EGLH(glUniformMatrix4fv, void, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    LOG("[gl-trace] glUniformMatrix4fv");
    void (*fn)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) = gl_symbol("glUniformMatrix4fv");
    if (fn) fn(location, count, transpose, value);
}

EGLH(glUseProgram, void, GLuint program)
{
    LOG("[gl-trace] glUseProgram");
    void (*fn)(GLuint program) = gl_symbol("glUseProgram");
    if (fn) fn(program);
}

EGLH(glVertexAttribPointer, void, GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
{
    LOG("[gl-trace] glVertexAttribPointer");
    void (*fn)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) = gl_symbol("glVertexAttribPointer");
    if (fn) fn(index, size, type, normalized, stride, pointer);
}

EGLH(glViewport, void, GLint x, GLint y, GLsizei width, GLsizei height)
{
    LOG("[gl-trace] glViewport");
    void (*fn)(GLint x, GLint y, GLsizei width, GLsizei height) = gl_symbol("glViewport");
    if (fn) fn(x, y, width, height);
}

EGLH(glWaitSync, void, GLsync sync, GLbitfield flags, GLuint64 timeout)
{
    LOG("[gl-trace] glWaitSync");
    void (*fn)(GLsync sync, GLbitfield flags, GLuint64 timeout) = gl_symbol("glWaitSync");
    if (fn) fn(sync, flags, timeout);
}


EGLH(glMapBufferRange, void *, GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
    LOG("[gl-buffer] glMapBufferRange");
    void * (*fn)(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) = gl_symbol("glMapBufferRange");
    void * result = fn ? fn(target, offset, length, access) : 0;
    LOG("[gl-buffer] glMapBufferRange result=%p", (void *)(uintptr_t)result);
    return result;
}

EGLH(glMapBufferOES, void *, GLenum target, GLenum access)
{
    LOG("[gl-buffer] glMapBufferOES");
    void * (*fn)(GLenum target, GLenum access) = gl_symbol("glMapBufferOES");
    void * result = fn ? fn(target, access) : 0;
    LOG("[gl-buffer] glMapBufferOES result=%p", (void *)(uintptr_t)result);
    return result;
}

EGLH(glUnmapBuffer, GLboolean, GLenum target)
{
    LOG("[gl-buffer] glUnmapBuffer");
    GLboolean (*fn)(GLenum target) = gl_symbol("glUnmapBuffer");
    GLboolean result = fn ? fn(target) : 0;
    LOG("[gl-buffer] glUnmapBuffer result=%p", (void *)(uintptr_t)result);
    return result;
}

EGLH(glFlushMappedBufferRange, void, GLenum target, GLintptr offset, GLsizeiptr length)
{
    LOG("[gl-buffer] glFlushMappedBufferRange");
    void (*fn)(GLenum target, GLintptr offset, GLsizeiptr length) = gl_symbol("glFlushMappedBufferRange");
    if (fn) fn(target, offset, length);
}

EGLH(glGetBufferParameteriv, void, GLenum target, GLenum pname, GLint *params)
{
    LOG("[gl-buffer] glGetBufferParameteriv");
    void (*fn)(GLenum target, GLenum pname, GLint *params) = gl_symbol("glGetBufferParameteriv");
    if (fn) fn(target, pname, params);
}

EGLH(glGetBufferParameteri64v, void, GLenum target, GLenum pname, GLint64 *params)
{
    LOG("[gl-buffer] glGetBufferParameteri64v");
    void (*fn)(GLenum target, GLenum pname, GLint64 *params) = gl_symbol("glGetBufferParameteri64v");
    if (fn) fn(target, pname, params);
}

EGLH(glGetBufferPointerv, void, GLenum target, GLenum pname, void **params)
{
    LOG("[gl-buffer] glGetBufferPointerv");
    void (*fn)(GLenum target, GLenum pname, void **params) = gl_symbol("glGetBufferPointerv");
    if (fn) fn(target, pname, params);
}

EGLH(glBindBufferBase, void, GLenum target, GLuint index, GLuint buffer)
{
    LOG("[gl-buffer] glBindBufferBase");
    void (*fn)(GLenum target, GLuint index, GLuint buffer) = gl_symbol("glBindBufferBase");
    if (fn) fn(target, index, buffer);
}

EGLH(glBindBufferRange, void, GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size)
{
    LOG("[gl-buffer] glBindBufferRange");
    void (*fn)(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) = gl_symbol("glBindBufferRange");
    if (fn) fn(target, index, buffer, offset, size);
}


EGLH(glBindVertexArray, void, GLuint array)
{
    LOG("[gl-vao] BindVertexArray=%u", array);
    void (*fn)(GLuint) = gl_symbol("glBindVertexArray");
    if (fn) fn(array);
}

EGLH(glGenVertexArrays, void, GLsizei n, GLuint *arrays)
{
    void (*fn)(GLsizei, GLuint *) =
        gl_symbol("glGenVertexArrays");
    if (fn) fn(n, arrays);
    LOG("[gl-vao] GenVertexArrays n=%d", n);
}

EGLH(glDeleteVertexArrays, void, GLsizei n, const GLuint *arrays)
{
    void (*fn)(GLsizei, const GLuint *) =
        gl_symbol("glDeleteVertexArrays");
    if (fn) fn(n, arrays);
}

EGLH(glIsVertexArray, GLboolean, GLuint array)
{
    GLboolean (*fn)(GLuint) =
        gl_symbol("glIsVertexArray");
    return fn ? fn(array) : 0;
}

EGLH(eglGetProcAddress, void *, const char *name)
{
    if (!name) return NULL;
    if (!strcmp(name, "glBindVertexArray") || !strcmp(name, "glBindVertexArrayOES")) return (void *)h_glBindVertexArray_bs;
    if (!strcmp(name, "glGenVertexArrays") || !strcmp(name, "glGenVertexArraysOES")) return (void *)h_glGenVertexArrays_bs;
    if (!strcmp(name, "glDeleteVertexArrays") || !strcmp(name, "glDeleteVertexArraysOES")) return (void *)h_glDeleteVertexArrays_bs;
    if (!strcmp(name, "glIsVertexArray") || !strcmp(name, "glIsVertexArrayOES")) return (void *)h_glIsVertexArray_bs;

    if (!name) return NULL;
    if (!strcmp(name, "glMapBufferRange")) return (void *)h_glMapBufferRange_bs;
    if (!strcmp(name, "glMapBufferOES")) return (void *)h_glMapBufferOES_bs;
    if (!strcmp(name, "glUnmapBuffer")) return (void *)h_glUnmapBuffer_bs;
    if (!strcmp(name, "glFlushMappedBufferRange")) return (void *)h_glFlushMappedBufferRange_bs;
    if (!strcmp(name, "glGetBufferParameteriv")) return (void *)h_glGetBufferParameteriv_bs;
    if (!strcmp(name, "glGetBufferParameteri64v")) return (void *)h_glGetBufferParameteri64v_bs;
    if (!strcmp(name, "glGetBufferPointerv")) return (void *)h_glGetBufferPointerv_bs;
    if (!strcmp(name, "glBindBufferBase")) return (void *)h_glBindBufferBase_bs;
    if (!strcmp(name, "glBindBufferRange")) return (void *)h_glBindBufferRange_bs;
    if (!strcmp(name, "glMapBufferRangeEXT")) return (void *)h_glMapBufferRange_bs;
    if (!strcmp(name, "glUnmapBufferOES")) return (void *)h_glUnmapBuffer_bs;
    if (!strcmp(name, "glFlushMappedBufferRangeEXT")) return (void *)h_glFlushMappedBufferRange_bs;

    if (!name) return NULL;
    if (!strcmp(name, "glActiveTexture")) return (void *)h_glActiveTexture_bs;
    if (!strcmp(name, "glAttachShader")) return (void *)h_glAttachShader_bs;
    if (!strcmp(name, "glBindAttribLocation")) return (void *)h_glBindAttribLocation_bs;
    if (!strcmp(name, "glBindBuffer")) return (void *)h_glBindBuffer_bs;
    if (!strcmp(name, "glBindFramebuffer")) return (void *)h_glBindFramebuffer_bs;
    if (!strcmp(name, "glBindRenderbuffer")) return (void *)h_glBindRenderbuffer_bs;
    if (!strcmp(name, "glBindTexture")) return (void *)h_glBindTexture_bs;
    if (!strcmp(name, "glBlendEquation")) return (void *)h_glBlendEquation_bs;
    if (!strcmp(name, "glBlendEquationSeparate")) return (void *)h_glBlendEquationSeparate_bs;
    if (!strcmp(name, "glBlendFunc")) return (void *)h_glBlendFunc_bs;
    if (!strcmp(name, "glBlendFuncSeparate")) return (void *)h_glBlendFuncSeparate_bs;
    if (!strcmp(name, "glBufferData")) return (void *)h_glBufferData_bs;
    if (!strcmp(name, "glBufferSubData")) return (void *)h_glBufferSubData_bs;
    if (!strcmp(name, "glCheckFramebufferStatus")) return (void *)h_glCheckFramebufferStatus_bs;
    if (!strcmp(name, "glClear")) return (void *)h_glClear_bs;
    if (!strcmp(name, "glClearColor")) return (void *)h_glClearColor_bs;
    if (!strcmp(name, "glClientWaitSync")) return (void *)h_glClientWaitSync_bs;
    if (!strcmp(name, "glColorMask")) return (void *)h_glColorMask_bs;
    if (!strcmp(name, "glCompileShader")) return (void *)h_glCompileShader_bs;
    if (!strcmp(name, "glCompressedTexImage2D")) return (void *)h_glCompressedTexImage2D_bs;
    if (!strcmp(name, "glCreateProgram")) return (void *)h_glCreateProgram_bs;
    if (!strcmp(name, "glCreateShader")) return (void *)h_glCreateShader_bs;
    if (!strcmp(name, "glDeleteBuffers")) return (void *)h_glDeleteBuffers_bs;
    if (!strcmp(name, "glDeleteFramebuffers")) return (void *)h_glDeleteFramebuffers_bs;
    if (!strcmp(name, "glDeleteProgram")) return (void *)h_glDeleteProgram_bs;
    if (!strcmp(name, "glDeleteRenderbuffers")) return (void *)h_glDeleteRenderbuffers_bs;
    if (!strcmp(name, "glDeleteShader")) return (void *)h_glDeleteShader_bs;
    if (!strcmp(name, "glDeleteSync")) return (void *)h_glDeleteSync_bs;
    if (!strcmp(name, "glDeleteTextures")) return (void *)h_glDeleteTextures_bs;
    if (!strcmp(name, "glDepthFunc")) return (void *)h_glDepthFunc_bs;
    if (!strcmp(name, "glDepthMask")) return (void *)h_glDepthMask_bs;
    if (!strcmp(name, "glDisable")) return (void *)h_glDisable_bs;
    if (!strcmp(name, "glDisableVertexAttribArray")) return (void *)h_glDisableVertexAttribArray_bs;
    if (!strcmp(name, "glDrawArrays")) return (void *)h_glDrawArrays_bs;
    if (!strcmp(name, "glDrawElements")) return (void *)h_glDrawElements_bs;
    if (!strcmp(name, "glEnable")) return (void *)h_glEnable_bs;
    if (!strcmp(name, "glEnableVertexAttribArray")) return (void *)h_glEnableVertexAttribArray_bs;
    if (!strcmp(name, "glFenceSync")) return (void *)h_glFenceSync_bs;
    if (!strcmp(name, "glFinish")) return (void *)h_glFinish_bs;
    if (!strcmp(name, "glFlush")) return (void *)h_glFlush_bs;
    if (!strcmp(name, "glFramebufferRenderbuffer")) return (void *)h_glFramebufferRenderbuffer_bs;
    if (!strcmp(name, "glFramebufferTexture2D")) return (void *)h_glFramebufferTexture2D_bs;
    if (!strcmp(name, "glGenBuffers")) return (void *)h_glGenBuffers_bs;
    if (!strcmp(name, "glGenFramebuffers")) return (void *)h_glGenFramebuffers_bs;
    if (!strcmp(name, "glGenRenderbuffers")) return (void *)h_glGenRenderbuffers_bs;
    if (!strcmp(name, "glGenTextures")) return (void *)h_glGenTextures_bs;
    if (!strcmp(name, "glGenerateMipmap")) return (void *)h_glGenerateMipmap_bs;
    if (!strcmp(name, "glGetActiveUniform")) return (void *)h_glGetActiveUniform_bs;
    if (!strcmp(name, "glGetError")) return (void *)h_glGetError_bs;
    if (!strcmp(name, "glGetFloatv")) return (void *)h_glGetFloatv_bs;
    if (!strcmp(name, "glGetFramebufferAttachmentParameteriv")) return (void *)h_glGetFramebufferAttachmentParameteriv_bs;
    if (!strcmp(name, "glGetIntegerv")) return (void *)h_glGetIntegerv_bs;
    if (!strcmp(name, "glGetProgramInfoLog")) return (void *)h_glGetProgramInfoLog_bs;
    if (!strcmp(name, "glGetProgramiv")) return (void *)h_glGetProgramiv_bs;
    if (!strcmp(name, "glGetRenderbufferParameteriv")) return (void *)h_glGetRenderbufferParameteriv_bs;
    if (!strcmp(name, "glGetShaderInfoLog")) return (void *)h_glGetShaderInfoLog_bs;
    if (!strcmp(name, "glGetShaderiv")) return (void *)h_glGetShaderiv_bs;
    if (!strcmp(name, "glGetString")) return (void *)h_glGetString_bs;
    if (!strcmp(name, "glGetStringi")) return (void *)h_glGetStringi_bs;
    if (!strcmp(name, "glGetTexParameteriv")) return (void *)h_glGetTexParameteriv_bs;
    if (!strcmp(name, "glGetUniformLocation")) return (void *)h_glGetUniformLocation_bs;
    if (!strcmp(name, "glIsFramebuffer")) return (void *)h_glIsFramebuffer_bs;
    if (!strcmp(name, "glIsProgram")) return (void *)h_glIsProgram_bs;
    if (!strcmp(name, "glIsRenderbuffer")) return (void *)h_glIsRenderbuffer_bs;
    if (!strcmp(name, "glLinkProgram")) return (void *)h_glLinkProgram_bs;
    if (!strcmp(name, "glPixelStorei")) return (void *)h_glPixelStorei_bs;
    if (!strcmp(name, "glReadPixels")) return (void *)h_glReadPixels_bs;
    if (!strcmp(name, "glReleaseShaderCompiler")) return (void *)h_glReleaseShaderCompiler_bs;
    if (!strcmp(name, "glRenderbufferStorage")) return (void *)h_glRenderbufferStorage_bs;
    if (!strcmp(name, "glScissor")) return (void *)h_glScissor_bs;
    if (!strcmp(name, "glShaderSource")) return (void *)h_glShaderSource_bs;
    if (!strcmp(name, "glStencilFunc")) return (void *)h_glStencilFunc_bs;
    if (!strcmp(name, "glStencilMask")) return (void *)h_glStencilMask_bs;
    if (!strcmp(name, "glStencilOp")) return (void *)h_glStencilOp_bs;
    if (!strcmp(name, "glTexImage2D")) return (void *)h_glTexImage2D_bs;
    if (!strcmp(name, "glTexParameteri")) return (void *)h_glTexParameteri_bs;
    if (!strcmp(name, "glTexStorage2D")) return (void *)h_glTexStorage2D_bs;
    if (!strcmp(name, "glTexSubImage2D")) return (void *)h_glTexSubImage2D_bs;
    if (!strcmp(name, "glUniform1f")) return (void *)h_glUniform1f_bs;
    if (!strcmp(name, "glUniform1fv")) return (void *)h_glUniform1fv_bs;
    if (!strcmp(name, "glUniform1i")) return (void *)h_glUniform1i_bs;
    if (!strcmp(name, "glUniform1iv")) return (void *)h_glUniform1iv_bs;
    if (!strcmp(name, "glUniform2f")) return (void *)h_glUniform2f_bs;
    if (!strcmp(name, "glUniform2fv")) return (void *)h_glUniform2fv_bs;
    if (!strcmp(name, "glUniform3fv")) return (void *)h_glUniform3fv_bs;
    if (!strcmp(name, "glUniform4fv")) return (void *)h_glUniform4fv_bs;
    if (!strcmp(name, "glUniformMatrix4fv")) return (void *)h_glUniformMatrix4fv_bs;
    if (!strcmp(name, "glUseProgram")) return (void *)h_glUseProgram_bs;
    if (!strcmp(name, "glVertexAttribPointer")) return (void *)h_glVertexAttribPointer_bs;
    if (!strcmp(name, "glViewport")) return (void *)h_glViewport_bs;
    if (!strcmp(name, "glWaitSync")) return (void *)h_glWaitSync_bs;

    return NULL;
}

#endif


#ifdef __ANDROID__
int dm_gl_texture_size(unsigned name, int *width, int *height) {
    void (*get)(unsigned,int*) =
        gl_symbol("glGetIntegerv");
    void (*bind)(unsigned,unsigned) =
        gl_symbol("glBindTexture");
    void (*level)(unsigned,int,unsigned,int*) =
        gl_symbol("glGetTexLevelParameteriv");
    void *(*current)(void) =
        egl_symbol("eglGetCurrentContext");

    if (!get || !bind || !level ||
        !current || !current()) {
        LOG("[V7-UI] No current GLES context");
        return 0;
    }

    int old=0,w=0,h=0,fmt=0;
    get(0x8069,&old);
    bind(0x0DE1,name);
    level(0x0DE1,0,0x1000,&w);
    level(0x0DE1,0,0x1001,&h);
    level(0x0DE1,0,0x1003,&fmt);
    bind(0x0DE1,old);

    LOG("[V7-UI] GLES texture=%u %dx%d format=%#x",
        name,w,h,fmt);
    *width = w; *height = h;
    return w > 0 && h > 0 && w <= 4096 && h <= 4096;
}
#endif


#ifdef __ANDROID__
int dm_gl_read_ui(unsigned name, int w, int h, void *pixels)
{
    void (*get)(unsigned,int*) =
        gl_symbol("glGetIntegerv");
    void (*gen)(int,unsigned*) =
        gl_symbol("glGenFramebuffers");
    void (*bind)(unsigned,unsigned) =
        gl_symbol("glBindFramebuffer");
    void (*attach)(unsigned,unsigned,unsigned,unsigned,int) =
        gl_symbol("glFramebufferTexture2D");
    unsigned (*check)(unsigned) =
        gl_symbol("glCheckFramebufferStatus");
    void (*read)(int,int,int,int,unsigned,unsigned,void*) =
        gl_symbol("glReadPixels");
    void (*del)(int,const unsigned*) =
        gl_symbol("glDeleteFramebuffers");
    void (*finish)(void) =
        gl_symbol("glFinish");
    void *(*current)(void) =
        egl_symbol("eglGetCurrentContext");

    if (!get || !gen || !bind || !attach ||
        !check || !read || !del || !finish ||
        !current || !current() || !pixels)
        return 0;

    int actual_w=0, actual_h=0;
    if (!dm_gl_texture_size(name, &actual_w, &actual_h) || actual_w != w || actual_h != h) return 0;
    int old = 0;
    unsigned fbo = 0;

    get(0x8CAA, &old);
    gen(1, &fbo);
    bind(0x8CA8, fbo);
    attach(0x8CA8, 0x8CE0, 0x0DE1, name, 0);

    int ok = check(0x8CA8) == 0x8CD5;

    if (ok) {
        void (*bb)(unsigned,unsigned) =
            gl_symbol("glBindBuffer");
        void (*ps)(unsigned,int) =
            gl_symbol("glPixelStorei");

        int pbo = 0, align = 4, row = 0;
        int skiprow = 0, skippix = 0;

        if (!bb || !ps) {
            ok = 0;
        } else {
            get(0x88ED, &pbo);
            get(0x0D05, &align);
            get(0x0D02, &row);
            get(0x0D03, &skiprow);
            get(0x0D04, &skippix);

            bb(0x88EB, 0);
            ps(0x0D05, 1);
            ps(0x0D02, 0);
            ps(0x0D03, 0);
            ps(0x0D04, 0);

            finish();
            read(0, 0, w, h, 0x1908, 0x1401, pixels);

            ps(0x0D05, align);
            ps(0x0D02, row);
            ps(0x0D03, skiprow);
            ps(0x0D04, skippix);
            bb(0x88EB, pbo);
        }
    }

    bind(0x8CA8, old);
    del(1, &fbo);

    return ok;
}
#endif
