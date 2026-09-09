/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/egl.h"

#include "utils/glutil.h"
#include "utils/logger.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static int egl_ptr_is_writable(const void *ptr) {
    uintptr_t value = (uintptr_t)ptr;
    return ptr != NULL && value >= 0x10000;
}

EGLBoolean gunbros_eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    l_debug("gunbros_eglInitialize(0x%x)", (int)dpy);

    gl_init();

    if (egl_ptr_is_writable(major)) *major = 2;
    if (egl_ptr_is_writable(minor)) *minor = 2;

    return EGL_TRUE;
}

EGLBoolean gunbros_eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute,
                           EGLint *value) {
    EGLBoolean ret = EGL_TRUE;
    switch (attribute) {
        case EGL_CONFIG_ID:
            if (egl_ptr_is_writable(value)) *value = 0;
            break;
        case EGL_CONTEXT_CLIENT_TYPE:
            if (egl_ptr_is_writable(value)) *value = EGL_OPENGL_ES_API;
            break;
        case EGL_CONTEXT_CLIENT_VERSION:
            if (egl_ptr_is_writable(value)) *value = 2;
            break;
        case EGL_RENDER_BUFFER:
            if (egl_ptr_is_writable(value)) *value = EGL_BACK_BUFFER;
            break;
        default:
            l_error("eglQueryContext / EGL_BAD_ATTRIBUTE: 0x%x", attribute);
            ret = EGL_FALSE;
            break;
    }

    return ret;
}


EGLBoolean gunbros_eglQuerySurface(EGLDisplay dpy, EGLSurface eglSurface,
                           EGLint attribute, EGLint *value) {
    EGLBoolean ret = EGL_TRUE;
    switch (attribute) {
        case EGL_CONFIG_ID:
            if (egl_ptr_is_writable(value)) *value = 0;
            break;
        case EGL_WIDTH:
            if (egl_ptr_is_writable(value)) *value = 960;
            break;
        case EGL_HEIGHT:
            if (egl_ptr_is_writable(value)) *value = 544;
            break;
        case EGL_TEXTURE_FORMAT:
            if (egl_ptr_is_writable(value)) *value = EGL_TEXTURE_RGBA;
            break;
        case EGL_TEXTURE_TARGET:
            if (egl_ptr_is_writable(value)) *value = EGL_TEXTURE_2D;
            break;
        case EGL_SWAP_BEHAVIOR:
            if (egl_ptr_is_writable(value)) *value = EGL_BUFFER_PRESERVED;
            break;
        case EGL_LARGEST_PBUFFER:
        case EGL_MIPMAP_TEXTURE:
            if (egl_ptr_is_writable(value)) *value = EGL_FALSE;
            break;
        case EGL_MIPMAP_LEVEL:
            if (egl_ptr_is_writable(value)) *value = 0;
            break;
        case EGL_MULTISAMPLE_RESOLVE:
            // ignored when creating the surface, return default
            if (egl_ptr_is_writable(value)) *value = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
            break;
        case EGL_HORIZONTAL_RESOLUTION:
        case EGL_VERTICAL_RESOLUTION:
            if (egl_ptr_is_writable(value)) *value = 220 * EGL_DISPLAY_SCALING; // VITA DPI is 220
            break;
        case EGL_PIXEL_ASPECT_RATIO:
            // Please don't ask why * EGL_DISPLAY_SCALING, the document says it
            if (egl_ptr_is_writable(value)) *value = 960 / 544 * EGL_DISPLAY_SCALING;
            break;
        case EGL_RENDER_BUFFER:
            if (egl_ptr_is_writable(value)) *value = EGL_BACK_BUFFER;
            break;
        case EGL_VG_COLORSPACE:
            // ignored when creating the surface, return default
            if (egl_ptr_is_writable(value)) *value = EGL_VG_COLORSPACE_sRGB;
            break;
        case EGL_VG_ALPHA_FORMAT:
            // ignored when creating the surface, return default
            if (egl_ptr_is_writable(value)) *value = EGL_VG_ALPHA_FORMAT_NONPRE;
            break;
        case EGL_TIMESTAMPS_ANDROID:
            if (egl_ptr_is_writable(value)) *value = EGL_FALSE;
            break;
        default:
            l_error("eglQuerySurface / EGL_BAD_ATTRIBUTE: 0x%x", attribute);
            ret = EGL_FALSE;
            break;
    }

    return ret;
}


EGLBoolean gunbros_eglGetConfigAttrib(EGLDisplay display, EGLConfig config,
                              EGLint attribute, EGLint * value) {
    if (!egl_ptr_is_writable(value)) {
        l_debug("gunbros_eglGetConfigAttrib(attr=0x%x) ignored invalid out ptr 0x%x",
                attribute, (unsigned int)(uintptr_t)value);
        return EGL_TRUE;
    }

    switch (attribute) {
        case EGL_ALPHA_SIZE: {
            *value = 8;
            break;
        }
        case EGL_ALPHA_MASK_SIZE: {
            *value = 8;
            break;
        }
        case EGL_BIND_TO_TEXTURE_RGB: {
            *value = EGL_TRUE;
            break;
        }
        case EGL_BIND_TO_TEXTURE_RGBA: {
            *value = EGL_TRUE;
            break;
        }
        case EGL_BLUE_SIZE: {
            *value = 8;
            break;
        }
        case EGL_BUFFER_SIZE: {
            *value = 32;
            break;
        }
        case EGL_COLOR_BUFFER_TYPE: {
            *value = EGL_RGB_BUFFER;
            break;
        }
        case EGL_CONFIG_CAVEAT: {
            *value = EGL_NONE;
            break;
        }
        case EGL_CONFIG_ID: {
            *value = 0;
            break;
        }
        case EGL_CONFORMANT: {
            *value = 0;
            break;
        }
        case EGL_DEPTH_SIZE: {
            *value = 24;
            break;
        }
        case EGL_GREEN_SIZE: {
            *value = 8;
            break;
        }
        case EGL_LEVEL: {
            *value = 0;
            break;
        }
        case EGL_LUMINANCE_SIZE: {
            *value = 0;
            break;
        }
        case EGL_MAX_PBUFFER_WIDTH: {
            *value = 0;
            break;
        }
        case EGL_MAX_PBUFFER_HEIGHT: {
            *value = 0;
            break;
        }
        case EGL_MAX_PBUFFER_PIXELS: {
            *value = 0;
            break;
        }
        case EGL_MAX_SWAP_INTERVAL: {
            *value = 0;
            break;
        }
        case EGL_MIN_SWAP_INTERVAL: {
            *value = 0;
            break;
        }
        case EGL_NATIVE_RENDERABLE: {
            *value = 0;
            break;
        }
        case EGL_NATIVE_VISUAL_ID: {
            *value = 0;
            break;
        }
        case EGL_NATIVE_VISUAL_TYPE: {
            *value = 0;
            break;
        }
        case EGL_RED_SIZE: {
            *value = 8;
            break;
        }
        case EGL_RENDERABLE_TYPE: {
            *value = EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_BIT;
            break;
        }
        case EGL_SAMPLE_BUFFERS: {
            *value = 0;
            break;
        }
        case EGL_SAMPLES: {
            *value = 0;
            break;
        }
        case EGL_STENCIL_SIZE: {
            *value = 8;
            break;
        }
        case EGL_SURFACE_TYPE: {
            *value = 0 | EGL_WINDOW_BIT;
            break;
        }
        case EGL_TRANSPARENT_TYPE: {
            *value = 0;
            break;
        }
        case EGL_TRANSPARENT_RED_VALUE: {
            *value = 0;
            break;
        }
        case EGL_TRANSPARENT_GREEN_VALUE: {
            *value = 0;
            break;
        }
        case EGL_TRANSPARENT_BLUE_VALUE: {
            *value = 0;
            break;
        }
        default:
            l_error("eglGetConfigAttrib / EGL_BAD_ATTRIBUTE: 0x%x", attribute);
            return EGL_FALSE;
    }
    return EGL_TRUE;
}

EGLBoolean gunbros_eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config) {
    if (!egl_ptr_is_writable(num_config)) {
        l_debug("eglChooseConfig ignored invalid num_config ptr 0x%x",
                (unsigned int)(uintptr_t)num_config);
        return EGL_TRUE;
    }

    if (!egl_ptr_is_writable(configs) || config_size <= 0) {
        *num_config = 1;
        return EGL_TRUE;
    }

    *configs = strdup("conf");
    *num_config = 1;

    return EGL_TRUE;
}

EGLContext gunbros_eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context,
                            const EGLint *attrib_list) {
    // Just something that is a valid pointer which can be freed later
    return strdup("ctx");
}

EGLSurface gunbros_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  void * win, const EGLint *attrib_list) {
    // Just something that is a valid pointer which can be freed later
    return strdup("surface");
}

EGLBoolean gunbros_eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx) {
    return EGL_TRUE;
}

EGLBoolean gunbros_eglDestroyContext (EGLDisplay dpy, EGLContext ctx) {
    if (ctx) free(ctx);
    return EGL_TRUE;
}

EGLBoolean gunbros_eglDestroySurface (EGLDisplay dpy, EGLSurface surface) {
    if (surface) free(surface);
    return EGL_TRUE;
}

EGLBoolean gunbros_eglTerminate(EGLDisplay dpy) {
    return EGL_TRUE;
}

EGLContext gunbros_eglGetCurrentContext (void) {
    return strdup("ctx");
}

char const * gunbros_eglQueryString(EGLDisplay display, EGLint name) {
    switch (name) {
    case EGL_CLIENT_APIS:
        return "OpenGL OpenGL_ES";
    case EGL_VENDOR:
        return "Rinnegatamante";
    case EGL_VERSION:
        return "2.2 VitaGL";
    case EGL_EXTENSIONS:
        return "EGL_KHR_image "
               "EGL_KHR_image_base "
               "EGL_KHR_image_pixmap "
               "EGL_KHR_gl_texture_2D_image "
               "EGL_KHR_gl_texture_cubemap_image "
               "EGL_KHR_gl_renderbuffer_image "
               "EGL_KHR_fence_sync "
               "EGL_NV_system_time "
               "EGL_ANDROID_image_native_buffer ";
    default:
        return NULL;
    }
}

EGLBoolean gunbros_eglGetConfigs(EGLDisplay display, EGLConfig * configs,
                         EGLint config_size, EGLint * num_config) {
    if (!egl_ptr_is_writable(num_config)) {
        l_debug("eglGetConfigs ignored invalid num_config ptr 0x%x",
                (unsigned int)(uintptr_t)num_config);
        return EGL_TRUE;
    }

    if (egl_ptr_is_writable(configs) && config_size > 0) {
        *configs = strdup("conf");
    }

    *num_config = 1;

    return EGL_TRUE;
}
