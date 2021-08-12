/*
    Inspired by radeontop
*/
#include <spdlog/spdlog.h>
#include "auth.h"
#include <xcb/xcb.h>
#include <xcb/dri2.h>
#include <xf86drm.h>
#include <libdrm/amdgpu_drm.h>
#include <libdrm/amdgpu.h>
#include <cstdlib>
#include <cstdio>

/* Try to authenticate the DRM client with help from the X server. */
bool authenticate_drm_xcb(drm_magic_t magic) {
    xcb_connection_t *conn = xcb_connect(NULL, NULL);
    if (!conn) {
        return false;
    }
    if (xcb_connection_has_error(conn)) {
        xcb_disconnect(conn);
        return false;
    }

    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    xcb_window_t window = screen->root;

    /* Authenticate our client via the X server using the magic. */
    xcb_dri2_authenticate_cookie_t auth_cookie =
        xcb_dri2_authenticate(conn, window, magic);
    xcb_dri2_authenticate_reply_t *auth_reply =
        xcb_dri2_authenticate_reply(conn, auth_cookie, NULL);
    free(auth_reply);

    xcb_disconnect(conn);
    return true;
}

bool authenticate_drm(int fd) {
    drm_magic_t magic;

    /* Obtain magic for our DRM client. */
    if (drmGetMagic(fd, &magic) < 0) {
        return false;
    }

    /* Try self-authenticate (if we are somehow the master). */
    if (drmAuthMagic(fd, magic) == 0) {
        if (drmDropMaster(fd)) {
            SPDLOG_ERROR("MANGOHUD: Failed to drop DRM master: {}", strerror(errno));
            fprintf(stderr, "\n\tWARNING: other DRM clients will crash on VT switch\n");
        }
        return true;
    }

    return authenticate_drm_xcb(magic);
}
