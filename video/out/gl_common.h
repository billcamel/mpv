/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * You can alternatively redistribute this file and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef MPLAYER_GL_COMMON_H
#define MPLAYER_GL_COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "common/msg.h"
#include "misc/bstr.h"

#include "vo.h"
#include "video/csputils.h"

#include "video/mp_image.h"

#if HAVE_GL_COCOA
#ifdef GL_VERSION_3_0
#include <OpenGL/gl3.h>
#else
#include <OpenGL/gl.h>
#endif
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define MP_GET_GL_WORKAROUNDS
#include "video/out/gl_header_fixes.h"

struct GL;
typedef struct GL GL;

void glAdjustAlignment(GL *gl, int stride);
int glFmt2bpp(GLenum format, GLenum type);
void glUploadTex(GL *gl, GLenum target, GLenum format, GLenum type,
                 const void *dataptr, int stride,
                 int x, int y, int w, int h, int slice);
void glClearTex(GL *gl, GLenum target, GLenum format, GLenum type,
                int x, int y, int w, int h, uint8_t val, void **scratch);
void glDownloadTex(GL *gl, GLenum target, GLenum format, GLenum type,
                   void *dataptr, int stride);
void glCheckError(GL *gl, struct mp_log *log, const char *info);
mp_image_t *glGetWindowScreenshot(GL *gl);

enum {
    MPGL_CAP_GL_LEGACY          = (1 << 1),     // GL 1.1 (excluding 3.x)
    MPGL_CAP_GL21               = (1 << 3),     // GL 2.1+ (excluding legacy)
    MPGL_CAP_ROW_LENGTH         = (1 << 4),     // GL_[UN]PACK_ROW_LENGTH
    MPGL_CAP_FB                 = (1 << 5),
    MPGL_CAP_VAO                = (1 << 6),
    MPGL_CAP_SRGB_TEX           = (1 << 7),
    MPGL_CAP_SRGB_FB            = (1 << 8),
    MPGL_CAP_FLOAT_TEX          = (1 << 9),
    MPGL_CAP_TEX_RG             = (1 << 10),    // GL_ARB_texture_rg / GL 3.x
    MPGL_CAP_VDPAU              = (1 << 11),    // GL_NV_vdpau_interop
    MPGL_CAP_APPLE_RGB_422      = (1 << 12),    // GL_APPLE_rgb_422
    MPGL_CAP_1ST_CLASS_ARRAYS   = (1 << 13),
    MPGL_CAP_3D_TEX             = (1 << 14),
    MPGL_CAP_DEBUG              = (1 << 15),
    MPGL_CAP_SW                 = (1 << 30),    // indirect or sw renderer
};

// E.g. 310 means 3.1
// Code doesn't have to use the macros; they are for convenience only.
#define MPGL_VER(major, minor) (((major) * 100) + (minor) * 10)
#define MPGL_VER_GET_MAJOR(ver) ((unsigned)(ver) / 100)
#define MPGL_VER_GET_MINOR(ver) ((unsigned)(ver) % 100 / 10)

#define MPGL_VER_P(ver) MPGL_VER_GET_MAJOR(ver), MPGL_VER_GET_MINOR(ver)

typedef struct MPGLContext {
    GL *gl;
    struct vo *vo;

    // Bit size of each component in the created framebuffer. 0 if unknown.
    int depth_r, depth_g, depth_b;

    // GL version requested from config_window_gl3 backend (MPGL_VER mangled).
    // (Might be different from the actual version in gl->version.)
    int requested_gl_version;

    void (*swapGlBuffers)(struct MPGLContext *);
    int (*vo_init)(struct vo *vo);
    void (*vo_uninit)(struct vo *vo);
    int (*vo_control)(struct vo *vo, int *events, int request, void *arg);
    void (*releaseGlContext)(struct MPGLContext *);
    void (*set_current)(struct MPGLContext *, bool current);

    // Resize the window, or create a new window if there isn't one yet.
    // On the first call, it creates a GL context according to what's specified
    // in MPGLContext.requested_gl_version. This is just a hint, and if the
    // requested version is not available, it may return a completely different
    // GL context. (The caller must check if the created GL version is ok. The
    // callee must try to fall back to an older version if the requested
    // version is not available, and newer versions are incompatible.)
    bool (*config_window)(struct MPGLContext *ctx, int flags);

    // An optional function to register a resize callback in the backend that
    // can be called on separate thread to handle resize events immediately
    // (without waiting for vo_check_events, which will come later for the
    // proper resize)
    void (*register_resize_callback)(struct vo *vo,
                                     void (*cb)(struct vo *vo, int w, int h));

    // For free use by the backend.
    void *priv;
} MPGLContext;

void mpgl_lock(MPGLContext *ctx);
void mpgl_unlock(MPGLContext *ctx);
void mpgl_set_context(MPGLContext *ctx);
void mpgl_unset_context(MPGLContext *ctx);
bool mpgl_is_thread_safe(MPGLContext *ctx);

// Create a VO window and create a GL context on it.
// (Calls config_window_gl3 or config_window+setGlWindow.)
// gl_flavor: 110 for legacy GL, 210 for GL 2.1 or 3.x core
// flags: passed to the backend's create window function
// Returns success.
MPGLContext *mpgl_init(struct vo *vo, const char *backend_name,
                       int gl_flavor, int vo_flags);
void mpgl_uninit(MPGLContext *ctx);

// flags: passed to the backend function
bool mpgl_reconfig_window(struct MPGLContext *ctx, int flags);

int mpgl_find_backend(const char *name);

struct m_option;
int mpgl_validate_backend_opt(struct mp_log *log, const struct m_option *opt,
                              struct bstr name, struct bstr param);

void mpgl_set_backend_cocoa(MPGLContext *ctx);
void mpgl_set_backend_w32(MPGLContext *ctx);
void mpgl_set_backend_x11(MPGLContext *ctx);
void mpgl_set_backend_x11es(MPGLContext *ctx);
void mpgl_set_backend_x11egl(MPGLContext *ctx);
void mpgl_set_backend_x11egles(MPGLContext *ctx);
void mpgl_set_backend_wayland(MPGLContext *ctx);

void mpgl_load_functions(GL *gl, void *(*getProcAddress)(const GLubyte *),
                         const char *ext2, struct mp_log *log);
void mpgl_load_functions2(GL *gl, void *(*get_fn)(void *ctx, const char *n),
                          void *fn_ctx, const char *ext2, struct mp_log *log);

// print a multi line string with line numbers (e.g. for shader sources)
// log, lev: module and log level, as in mp_msg()
void mp_log_source(struct mp_log *log, int lev, const char *src);

typedef void (GLAPIENTRY *MP_GLDEBUGPROC)(GLenum, GLenum, GLuint, GLenum,
                                          GLsizei, const GLchar *,const void *);

//function pointers loaded from the OpenGL library
struct GL {
    int version;                // MPGL_VER() mangled (e.g. 210 for 2.1)
    int es;                     // es version (e.g. 300), 0 for desktop GL
    int glsl_version;           // e.g. 130 for GLSL 1.30
    char *extensions;           // Equivalent to GL_EXTENSIONS
    int mpgl_caps;              // Bitfield of MPGL_CAP_* constants
    bool debug_context;         // use of e.g. GLX_CONTEXT_DEBUG_BIT_ARB

    void (GLAPIENTRY *Begin)(GLenum);
    void (GLAPIENTRY *End)(void);
    void (GLAPIENTRY *Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (GLAPIENTRY *MatrixMode)(GLenum);
    void (GLAPIENTRY *LoadIdentity)(void);
    void (GLAPIENTRY *Translated)(double, double, double);
    void (GLAPIENTRY *Scaled)(double, double, double);
    void (GLAPIENTRY *Ortho)(double, double, double, double, double,double);
    void (GLAPIENTRY *PushMatrix)(void);
    void (GLAPIENTRY *PopMatrix)(void);
    void (GLAPIENTRY *Clear)(GLbitfield);
    GLuint (GLAPIENTRY *GenLists)(GLsizei);
    void (GLAPIENTRY *DeleteLists)(GLuint, GLsizei);
    void (GLAPIENTRY *NewList)(GLuint, GLenum);
    void (GLAPIENTRY *EndList)(void);
    void (GLAPIENTRY *CallList)(GLuint);
    void (GLAPIENTRY *CallLists)(GLsizei, GLenum, const GLvoid *);
    void (GLAPIENTRY *GenTextures)(GLsizei, GLuint *);
    void (GLAPIENTRY *DeleteTextures)(GLsizei, const GLuint *);
    void (GLAPIENTRY *TexEnvi)(GLenum, GLenum, GLint);
    void (GLAPIENTRY *Color4ub)(GLubyte, GLubyte, GLubyte, GLubyte);
    void (GLAPIENTRY *Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (GLAPIENTRY *ClearColor)(GLclampf, GLclampf, GLclampf, GLclampf);
    void (GLAPIENTRY *Enable)(GLenum);
    void (GLAPIENTRY *Disable)(GLenum);
    const GLubyte *(GLAPIENTRY * GetString)(GLenum);
    void (GLAPIENTRY *DrawBuffer)(GLenum);
    void (GLAPIENTRY *DepthMask)(GLboolean);
    void (GLAPIENTRY *BlendFunc)(GLenum, GLenum);
    void (GLAPIENTRY *BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void (GLAPIENTRY *Flush)(void);
    void (GLAPIENTRY *Finish)(void);
    void (GLAPIENTRY *PixelStorei)(GLenum, GLint);
    void (GLAPIENTRY *TexImage1D)(GLenum, GLint, GLint, GLsizei, GLint,
                                  GLenum, GLenum, const GLvoid *);
    void (GLAPIENTRY *TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                  GLint, GLenum, GLenum, const GLvoid *);
    void (GLAPIENTRY *TexSubImage2D)(GLenum, GLint, GLint, GLint,
                                     GLsizei, GLsizei, GLenum, GLenum,
                                     const GLvoid *);
    void (GLAPIENTRY *GetTexImage)(GLenum, GLint, GLenum, GLenum, GLvoid *);
    void (GLAPIENTRY *TexParameteri)(GLenum, GLenum, GLint);
    void (GLAPIENTRY *TexParameterf)(GLenum, GLenum, GLfloat);
    void (GLAPIENTRY *TexParameterfv)(GLenum, GLenum, const GLfloat *);
    void (GLAPIENTRY *TexCoord2f)(GLfloat, GLfloat);
    void (GLAPIENTRY *TexCoord2fv)(const GLfloat *);
    void (GLAPIENTRY *Vertex2f)(GLfloat, GLfloat);
    void (GLAPIENTRY *GetIntegerv)(GLenum, GLint *);
    void (GLAPIENTRY *GetBooleanv)(GLenum, GLboolean *);
    void (GLAPIENTRY *ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (GLAPIENTRY *ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum,
                                  GLenum, GLvoid *);
    void (GLAPIENTRY *ReadBuffer)(GLenum);
    void (GLAPIENTRY *VertexPointer)(GLint, GLenum, GLsizei, const GLvoid *);
    void (GLAPIENTRY *ColorPointer)(GLint, GLenum, GLsizei, const GLvoid *);
    void (GLAPIENTRY *TexCoordPointer)(GLint, GLenum, GLsizei, const GLvoid *);
    void (GLAPIENTRY *DrawArrays)(GLenum, GLint, GLsizei);
    void (GLAPIENTRY *EnableClientState)(GLenum);
    void (GLAPIENTRY *DisableClientState)(GLenum);
    GLenum (GLAPIENTRY *GetError)(void);
    void (GLAPIENTRY *GetTexLevelParameteriv)(GLenum, GLint, GLenum, GLint *);


    void (GLAPIENTRY *GenBuffers)(GLsizei, GLuint *);
    void (GLAPIENTRY *DeleteBuffers)(GLsizei, const GLuint *);
    void (GLAPIENTRY *BindBuffer)(GLenum, GLuint);
    GLvoid * (GLAPIENTRY * MapBuffer)(GLenum, GLenum);
    GLboolean (GLAPIENTRY *UnmapBuffer)(GLenum);
    void (GLAPIENTRY *BufferData)(GLenum, intptr_t, const GLvoid *, GLenum);
    void (GLAPIENTRY *ActiveTexture)(GLenum);
    void (GLAPIENTRY *BindTexture)(GLenum, GLuint);
    void (GLAPIENTRY *MultiTexCoord2f)(GLenum, GLfloat, GLfloat);
    void (GLAPIENTRY *GenPrograms)(GLsizei, GLuint *);
    void (GLAPIENTRY *DeletePrograms)(GLsizei, const GLuint *);
    void (GLAPIENTRY *BindProgram)(GLenum, GLuint);
    void (GLAPIENTRY *ProgramString)(GLenum, GLenum, GLsizei, const GLvoid *);
    void (GLAPIENTRY *GetProgramivARB)(GLenum, GLenum, GLint *);
    void (GLAPIENTRY *ProgramEnvParameter4f)(GLenum, GLuint, GLfloat, GLfloat,
                                             GLfloat, GLfloat);
    int (GLAPIENTRY *SwapInterval)(int);
    void (GLAPIENTRY *TexImage3D)(GLenum, GLint, GLenum, GLsizei, GLsizei,
                                  GLsizei, GLint, GLenum, GLenum,
                                  const GLvoid *);

    void (GLAPIENTRY *BeginFragmentShader)(void);
    void (GLAPIENTRY *EndFragmentShader)(void);
    void (GLAPIENTRY *SampleMap)(GLuint, GLuint, GLenum);
    void (GLAPIENTRY *ColorFragmentOp2)(GLenum, GLuint, GLuint, GLuint, GLuint,
                                        GLuint, GLuint, GLuint, GLuint, GLuint);
    void (GLAPIENTRY *ColorFragmentOp3)(GLenum, GLuint, GLuint, GLuint, GLuint,
                                        GLuint, GLuint, GLuint, GLuint, GLuint,
                                        GLuint, GLuint, GLuint);
    void (GLAPIENTRY *SetFragmentShaderConstant)(GLuint, const GLfloat *);

    void (GLAPIENTRY *GenVertexArrays)(GLsizei, GLuint *);
    void (GLAPIENTRY *BindVertexArray)(GLuint);
    GLint (GLAPIENTRY *GetAttribLocation)(GLuint, const GLchar *);
    void (GLAPIENTRY *EnableVertexAttribArray)(GLuint);
    void (GLAPIENTRY *DisableVertexAttribArray)(GLuint);
    void (GLAPIENTRY *VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                           GLsizei, const GLvoid *);
    void (GLAPIENTRY *DeleteVertexArrays)(GLsizei, const GLuint *);
    void (GLAPIENTRY *UseProgram)(GLuint);
    GLint (GLAPIENTRY *GetUniformLocation)(GLuint, const GLchar *);
    void (GLAPIENTRY *CompileShader)(GLuint);
    GLuint (GLAPIENTRY *CreateProgram)(void);
    GLuint (GLAPIENTRY *CreateShader)(GLenum);
    void (GLAPIENTRY *ShaderSource)(GLuint, GLsizei, const GLchar **,
                                    const GLint *);
    void (GLAPIENTRY *LinkProgram)(GLuint);
    void (GLAPIENTRY *AttachShader)(GLuint, GLuint);
    void (GLAPIENTRY *DeleteShader)(GLuint);
    void (GLAPIENTRY *DeleteProgram)(GLuint);
    void (GLAPIENTRY *GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (GLAPIENTRY *GetShaderiv)(GLuint, GLenum, GLint *);
    void (GLAPIENTRY *GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
    void (GLAPIENTRY *GetProgramiv)(GLenum, GLenum, GLint *);
    const GLubyte* (GLAPIENTRY *GetStringi)(GLenum, GLuint);
    void (GLAPIENTRY *BindAttribLocation)(GLuint, GLuint, const GLchar *);
    void (GLAPIENTRY *BindFramebuffer)(GLenum, GLuint);
    void (GLAPIENTRY *GenFramebuffers)(GLsizei, GLuint *);
    void (GLAPIENTRY *DeleteFramebuffers)(GLsizei, const GLuint *);
    GLenum (GLAPIENTRY *CheckFramebufferStatus)(GLenum);
    void (GLAPIENTRY *FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint,
                                            GLint);

    void (GLAPIENTRY *Uniform1f)(GLint, GLfloat);
    void (GLAPIENTRY *Uniform2f)(GLint, GLfloat, GLfloat);
    void (GLAPIENTRY *Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void (GLAPIENTRY *Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (GLAPIENTRY *Uniform1i)(GLint, GLint);
    void (GLAPIENTRY *UniformMatrix2fv)(GLint, GLsizei, GLboolean,
                                        const GLfloat *);
    void (GLAPIENTRY *UniformMatrix3fv)(GLint, GLsizei, GLboolean,
                                        const GLfloat *);

    void (GLAPIENTRY *VDPAUInitNV)(const GLvoid *, const GLvoid *);
    void (GLAPIENTRY *VDPAUFiniNV)(void);
    GLvdpauSurfaceNV (GLAPIENTRY *VDPAURegisterOutputSurfaceNV)
        (GLvoid *, GLenum, GLsizei, const GLuint *);
    void (GLAPIENTRY *VDPAUUnregisterSurfaceNV)(GLvdpauSurfaceNV);
    void (GLAPIENTRY *VDPAUSurfaceAccessNV)(GLvdpauSurfaceNV, GLenum);
    void (GLAPIENTRY *VDPAUMapSurfacesNV)(GLsizei, const GLvdpauSurfaceNV *);
    void (GLAPIENTRY *VDPAUUnmapSurfacesNV)(GLsizei, const GLvdpauSurfaceNV *);

    GLint (GLAPIENTRY *GetVideoSync)(GLuint *);
    GLint (GLAPIENTRY *WaitVideoSync)(GLint, GLint, unsigned int *);

    void (GLAPIENTRY *DebugMessageCallback)(MP_GLDEBUGPROC callback,
                                            const void *userParam);
};

#endif /* MPLAYER_GL_COMMON_H */
