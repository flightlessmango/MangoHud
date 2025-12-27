// egl_wayland_glfw_dmabuf_overlay.c
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLFW/glfw3.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../ipc/ipc.h"
#include <thread>

class EGLWindow {
    public:
        GbmBuffer gbm;
        std::thread thread;
        EGLWindow(GbmBuffer gbm) : gbm(gbm) {
            std::thread thread(&EGLWindow::run, this);
            thread.detach();
        };

        void run();
};
