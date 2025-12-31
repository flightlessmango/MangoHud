#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <dlfcn.h>
#include <thread>
#include <unordered_map>
#include "../ipc/ipc_client.h"
#include "file_utils.h"
#include "../render/shared.h"

struct CtxRes {
    GLuint tex = 0;
    GLuint prog = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint cache_tex = 0;
    GLuint cache_fbo = 0;
    int cache_w = 0;
    int cache_h = 0;

    GLint uTexLoc = -1;
    GLint uFlipYLoc = -1;
    GLint uDecodeSRGBLoc = -1;
    GLint uSwapRBLoc = -1;

    bool inited = false;
};
struct GLState {
    struct state {
        GLint program = 0;
        GLint vao = 0;
        GLint arrayBuf = 0;
        GLint elemBuf = 0;
        GLint activeTex = 0;
        GLint tex2D = 0;
        GLint fbo = 0;
        GLint viewport[4] = {0,0,0,0};

        GLboolean blend = GL_FALSE;
        GLint blendSrcRGB=0, blendDstRGB=0, blendSrcA=0, blendDstA=0;
        GLint pbo = 0, align = 0, rowLen = 0, sampler = 0;

    } saved;

    GLState() {
        saved = save_state();
    }

    ~GLState() {
        restore_state(saved);
    }

    state save_state() {
        state s{};
        glGetIntegerv(GL_CURRENT_PROGRAM, &s.program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.arrayBuf);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &s.elemBuf);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTex);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex2D);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s.fbo);
        glGetIntegerv(GL_VIEWPORT, s.viewport);

        s.blend = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_BLEND_SRC_RGB, &s.blendSrcRGB);
        glGetIntegerv(GL_BLEND_DST_RGB, &s.blendDstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.blendSrcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &s.blendDstA);

        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &s.pbo);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &s.align);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &s.rowLen);

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glGetIntegeri_v(GL_SAMPLER_BINDING, 0, &s.sampler);
        glBindSampler(0, 0);
        return s;
    }

    void restore_state(const state& s) {
        glUseProgram(s.program);
        glBindVertexArray(s.vao);
        glBindBuffer(GL_ARRAY_BUFFER, s.arrayBuf);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.elemBuf);
        glActiveTexture(s.activeTex);
        glBindTexture(GL_TEXTURE_2D, s.tex2D);
        glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, s.rowLen);
        glPixelStorei(GL_UNPACK_ALIGNMENT, s.align);
        glBindSampler(0, s.sampler);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, s.pbo);
        glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);


        glBlendFuncSeparate(s.blendSrcRGB, s.blendDstRGB, s.blendSrcA, s.blendDstA);
        if (s.blend) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }
};

class EGL {
public:
    EGL();

    int64_t renderer();
    void import_dmabuf(const Fdinfo& fdinfo, GLuint& tex);

private:
    PFNEGLGETPROCADDRESSPROC            real_eglGetProcAddress          = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC           p_eglDestroyImageKHR            = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC p_glEGLImageTargetTexture2DOES  = nullptr;
    PFNEGLCREATEIMAGEKHRPROC            p_eglCreateImageKHR             = nullptr;
    PFNEGLQUERYDISPLAYATTRIBEXTPROC     p_eglQueryDisplayAttribEXT      = nullptr;
    PFNEGLQUERYDEVICESTRINGEXTPROC      p_eglQueryDeviceStringEXT       = nullptr;
};

class GLX {
public:
    GLX();

    bool import_dmabuf(const Fdinfo& fdinfo, GLuint& tex);
    bool import_opaque_fd(GLuint tex, const Fdinfo& fdinfo);

    CtxRes& ctx() {
        GLXContext c = glXGetCurrentContext();
        std::lock_guard<std::mutex> lk(ctx_m);
        return contexts[c];
    }

private:
    std::mutex ctx_m;
    std::unordered_map<GLXContext, CtxRes> contexts;
    PFNGLCREATEMEMORYOBJECTSEXTPROC     p_glCreateMemoryObjectsEXT = nullptr;
    PFNGLDELETEMEMORYOBJECTSEXTPROC     p_glDeleteMemoryObjectsEXT = nullptr;
    PFNGLIMPORTMEMORYFDEXTPROC          p_glImportMemoryFdEXT      = nullptr;
    PFNGLTEXSTORAGEMEM2DEXTPROC         p_glTexStorageMem2DEXT     = nullptr;

    GLuint memobj = 0;

    static const char* gl_err_str(GLenum e);
    static bool drain_gl_errors(const char* where);

    void* glx_gp(const char* name) {
        return (void*)glXGetProcAddress((const GLubyte*)name);
    }
};

class OverlayGL {
public:
    std::unique_ptr<IPCClient> ipc;
    Display* xdpy;

    OverlayGL(Display* xdpy_ = nullptr);
    void init(CtxRes& r);
    void draw();

    ~OverlayGL() {
        if (egl_thread.joinable())
            egl_thread.join();
    }

private:
    std::unique_ptr<GLX> glx;
    std::unique_ptr<EGL> egl;
    std::thread egl_thread;
    Fdinfo fdinfo;

    static GLuint compile_shader(GLenum type, const char *src);
    static GLuint link_program(GLuint vs, GLuint fs);

    void program(CtxRes& r);
    void vao_vbo(CtxRes& r) {
        if (!r.vao) glGenVertexArrays(1, &r.vao);
        if (!r.vbo) glGenBuffers(1, &r.vbo);

        glBindVertexArray(r.vao);
        glBindBuffer(GL_ARRAY_BUFFER, r.vbo);
        glBufferData(GL_ARRAY_BUFFER, 4 * 4 * (GLsizeiptr)sizeof(float), nullptr, GL_STREAM_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (void*)(2 * sizeof(float)));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void bind_texture(CtxRes& r);
    void create_cache(CtxRes& r, int w, int h);
    void sample_dmabuf(CtxRes& r, const Fdinfo& fdinfo, const GLState::state& saved);
    void draw_dmabuf(CtxRes& r);
    int release_fence(IPCClient* ipc, int dmabuf_fd, bool write = false);
};
