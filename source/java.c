#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_Logger.h>

#include <errno.h>
#include <fcntl.h>
#include <psp2/kernel/clib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "reimpl/io.h"
#include "audio.h"
#include "utils/data_paths.h"

#define ENV ((JNIEnv *)&jni)
// Keep these as macros so va_arg advances the callback's real va_list.
#define int_arg(args) va_arg(args, int)
#define long_arg(args) va_arg(args, long long)

#ifdef GUNBROS_QUIET_LOGS
#define GUNBROS_JAVA_AUDIO_LOG(...) ((void)0)
#else
#define GUNBROS_JAVA_AUDIO_LOG(...) (sceClibPrintf)(__VA_ARGS__)
#endif

enum {
    MID_SetTickRate = 100,
    MID_FinishApp,
    MID_LaunchURL,
    MID_OpenAPKFile,
    MID_APKExistsDir,
    MID_GetAndroidMinBufferSize,
    MID_InitialiseSoundEngine,
    MID_DestroySoundEngine,
    MID_InitialiseVibrationEngine,
    MID_DestroyVibrationEngine,
    MID_IsPSP,
    MID_SetVolume,
    MID_StartAudioStream,
    MID_StopAudioStream,
    MID_PlayVibration,
    MID_StopVibration,
    MID_InitialiseSoundEvent,
    MID_SetDeviceOrientation,
    MID_GetDeviceOrientation,
    MID_StartMovieActivity,
    MID_EnumInit,
    MID_EnumNext,
    MID_GetDeviceAvailableMemory,
    MID_SystemMessageBox,
    MID_CreateEGL,
    MID_SetAutoRotationValues,
    MID_StartAutoRotation,
    MID_StopAutoRotation,
    MID_StartLocalNotificationService,
    MID_AddLocalNotification,
    MID_RemoveLocalNotification,
    MID_CancelAllLocalNotifications,
    MID_EnablePushNotifications,
    MID_EnableMultipleTouch,
    MID_IsUserOnWiFi,
    MID_mp3Event,
    MID_eglGetDisplay,
    MID_eglGetCurrentDisplay,
    MID_eglInitialize,
    MID_eglSwapBuffers,
    MID_eglCreateContext,
    MID_eglDestroyContext,
    MID_eglMakeCurrent,
    MID_eglGetCurrentContext,
    MID_eglGetConfigs,
    MID_eglGetConfigAttrib,
    MID_eglChooseConfig,
    MID_eglCreateWindowSurface,
    MID_eglCreatePbufferSurface,
    MID_eglDestroySurface,
    MID_eglTerminate,
    MID_eglGetError,
    MID_eglQueryString,
    MID_getFileDescriptor,
    MID_getLength,
    MID_getStartOffset,
    MID_close,
    MID_getDefault,
    MID_getID,
    MID_getActiveNetworkInfo,
    MID_getSystemService,
    MID_isAvailable,
    MID_start,
    MID_pause,
    MID_stop,
    MID_seekTo,
    MID_getCurrentPosition,
    MID_setLooping,
    MID_setVolume,
    MID_release,
    MID_nfcEnabled,
    MID_resdlEvent,
    MID_resdlStringEvent,
    MID_GluAdMarvel_ctor,
    MID_showAd,
    MID_hideAd,
    MID_isVisible,
    MID_tick,
    MID_iapEvent,
    MID_facebookEvent,
    MID_onStartSession,
    MID_HashMap_ctor,
    MID_HashMap_put,
    MID_getContentResolver,
    MID_Settings_getString,
    MID_onEvent,
    MID_onresume,
    MID_getExternalStorageDirectory,
    MID_getExternalStorageState,
    MID_File_ctor,
    MID_File_toString,
    MID_File_exists,
    MID_canDisplayInterface,
    MID_queryFeaturedApp,
    MID_interfaceIsOpen,
};

enum {
    FID_WINDOW_SERVICE = 1,
    FID_INSTANCE,
    FID_SDK_INT,
    FID_MODEL,
    FID_EGL_NO_CONTEXT,
    FID_EGL_NO_DISPLAY,
    FID_EGL_NO_SURFACE,
    FID_DESCRIPTOR,
    FID_ASSET_FD,
    FID_ASSET_START_OFFSET,
    FID_ASSET_LENGTH,
    FID_ENUM_FILENAME,
    FID_ENUM_LENGTH,
    FID_MEDIA_MOUNTED,
};

enum {
    FIELD_INT_SDK_INT = 0,
    FIELD_INT_DESCRIPTOR,
};

enum {
    FIELD_OBJECT_WINDOW_SERVICE = 0,
    FIELD_OBJECT_INSTANCE,
    FIELD_OBJECT_MODEL,
    FIELD_OBJECT_EGL_NO_CONTEXT,
    FIELD_OBJECT_EGL_NO_DISPLAY,
    FIELD_OBJECT_EGL_NO_SURFACE,
    FIELD_OBJECT_ASSET_FD,
    FIELD_OBJECT_ENUM_FILENAME,
    FIELD_OBJECT_MEDIA_MOUNTED,
};

enum {
    FIELD_LONG_ASSET_START_OFFSET = 0,
    FIELD_LONG_ASSET_LENGTH,
    FIELD_LONG_ENUM_LENGTH,
};

enum {
    JNI_MISSING_ENTRY_MAX = 64,
    JNI_MISSING_NAME_MAX = 120,
};

int gunbros_tick_rate_ms = 16;
int gunbros_finish_requested = 0;

static jobject g_window_service = NULL;
static jobject g_model = NULL;
static jobject g_egl_no_context = NULL;
static jobject g_egl_no_display = NULL;
static jobject g_egl_no_surface = NULL;
static jobject g_enum_filename = NULL;
static jobject g_external_storage_path = NULL;
static jobject g_external_storage_state = NULL;

static DIR *g_enum_dir = NULL;
static bool g_enum_dirs = false;
static char g_enum_path[512];

static int g_asset_fd = -1;
static jlong g_asset_length = 0;
static jint g_asset_descriptor = -1;

enum { APK_RESOURCE_PATH_CACHE_MAX = 32 };
typedef struct ApkResourcePathCacheEntry {
    bool used;
    char request[256];
    char resolved[512];
    struct stat st;
    GunBrosDataLocation location;
} ApkResourcePathCacheEntry;

static ApkResourcePathCacheEntry g_apk_resource_path_cache[APK_RESOURCE_PATH_CACHE_MAX];
static unsigned int g_apk_resource_path_cache_replace;

static const jobject DUMMY_EGL = (jobject)0x10000100;
static const jobject DUMMY_EGL_DISPLAY = (jobject)0x10000110;
static const jobject DUMMY_EGL_CONTEXT = (jobject)0x10000120;
static const jobject DUMMY_EGL_SURFACE = (jobject)0x10000130;
static const jobject DUMMY_EGL_CONFIG = (jobject)0x10000140;
static const jobject DUMMY_ENUM_RESULT = (jobject)0x10000150;
static const jobject DUMMY_MEDIA_PLAYER = (jobject)0x10000160;
static const jobject DUMMY_FILE_DESCRIPTOR = (jobject)0x10000170;
static const jobject DUMMY_TIMEZONE = (jobject)0x10000180;
static const jobject DUMMY_NETWORK_INFO = (jobject)0x10000190;
static const jobject DUMMY_ACTIVITY = (jobject)0x100001A0;
static const jobject DUMMY_ASSET_FILE_DESCRIPTOR = (jobject)0x100001B0;
static const jobject DUMMY_SYSTEM_SERVICE = (jobject)0x100001C0;
static const jobject DUMMY_CONNECTIVITY_MANAGER = (jobject)0x100001D0;
static const jobject DUMMY_ADMARVEL = (jobject)0x100001E0;
static const jobject DUMMY_HASH_MAP = (jobject)0x100001F0;
static const jobject DUMMY_CONTENT_RESOLVER = (jobject)0x10000200;
static const jobject DUMMY_EXTERNAL_STORAGE_FILE = (jobject)0x10000210;

static char g_missing_methods[JNI_MISSING_ENTRY_MAX][JNI_MISSING_NAME_MAX];
static char g_missing_fields[JNI_MISSING_ENTRY_MAX][JNI_MISSING_NAME_MAX];
static char g_missing_classes[JNI_MISSING_ENTRY_MAX][JNI_MISSING_NAME_MAX];

static void report_missing_jni_once(char entries[][JNI_MISSING_NAME_MAX], const char *kind, const char *name) {
    int i;

    if (!kind || !name || name[0] == '\0') {
        return;
    }

    for (i = 0; i < JNI_MISSING_ENTRY_MAX; ++i) {
        if (entries[i][0] == '\0') {
            snprintf(entries[i], JNI_MISSING_NAME_MAX, "%s", name);
            sceClibPrintf("[JNI-MISS] %s %s\n", kind, entries[i]);
            return;
        }

        if (strcmp(entries[i], name) == 0) {
            return;
        }
    }

    sceClibPrintf("[JNI-MISS] %s %s\n", kind, name);
}

void gunbros_report_missing_method(const char *name) {
    report_missing_jni_once(g_missing_methods, "method", name);
}

void gunbros_report_missing_field(const char *name) {
    report_missing_jni_once(g_missing_fields, "field", name);
}

void gunbros_report_missing_class(const char *name) {
    report_missing_jni_once(g_missing_classes, "class", name);
}

static void log_optional_jni_once(const char *name) {
    static int log_count = 0;

    if (log_count < 16) {
        sceClibPrintf("[JNI-OPT] %s -> stub\n", name ? name : "(null)");
        log_count++;
    }
}

static void log_network_once(const char *name, const char *result) {
    static int log_count = 0;

    if (log_count < 16) {
        sceClibPrintf("[JNI-NET] %s -> %s\n",
                      name ? name : "(null)",
                      result ? result : "(null)");
        log_count++;
    }
}

static jstring jstr_new(const char *text) {
    if (!text) {
        text = "";
    }

    return jni->NewStringUTF(ENV, text);
}

static const char *jstr_cstr(jstring str) {
    if (!str) {
        return NULL;
    }

    return jni->GetStringUTFChars(ENV, str, NULL);
}

static void jstr_release(jstring str, const char *text) {
    if (str && text) {
        jni->ReleaseStringUTFChars(ENV, str, text);
    }
}

static bool resolve_data_path(char *dst, size_t dst_size, const char *name, bool want_dir) {
    return gunbros_resolve_data_path(dst, dst_size, name, want_dir);
}

static void close_asset_fd(void) {
    if (g_asset_fd >= 0) {
        gunbros_forget_fd_path(g_asset_fd);
        close(g_asset_fd);
        g_asset_fd = -1;
    }

    g_asset_length = 0;
    g_asset_descriptor = -1;
    fieldsInt[FIELD_INT_DESCRIPTOR].value = -1;
    fieldsLong[FIELD_LONG_ASSET_START_OFFSET].value = 0;
    fieldsLong[FIELD_LONG_ASSET_LENGTH].value = 0;
    fieldsObject[FIELD_OBJECT_ASSET_FD].value = NULL;
}

static void close_enum_dir(void) {
    if (g_enum_dir) {
        closedir(g_enum_dir);
        g_enum_dir = NULL;
    }
}

static bool is_probable_java_array_ref(const void *ptr) {
    return jda_is_dyn_array((JavaDynArray *)ptr) == JNI_TRUE;
}

static bool is_probable_c_string_ptr(const void *ptr) {
    const char *text = (const char *)ptr;
    uintptr_t value = (uintptr_t)ptr;
    size_t i;

    if (!ptr) {
        return false;
    }

    if (value < 0x81000000u || value >= 0xA0000000u) {
        return false;
    }

    for (i = 0; i < 512; ++i) {
        if (text[i] == '\0') {
            return true;
        }
    }

    return false;
}

static void set_int_array_first(jintArray array, jint value) {
    if (!is_probable_java_array_ref(array) || jni->GetArrayLength(ENV, array) <= 0) {
        return;
    }

    jni->SetIntArrayRegion(ENV, array, 0, 1, &value);
}

static void set_int_array_pair(jintArray array, jint a, jint b) {
    jint values[2] = { a, b };
    jsize len;

    if (!is_probable_java_array_ref(array)) {
        return;
    }

    len = jni->GetArrayLength(ENV, array);
    if (len <= 0) {
        return;
    }

    jni->SetIntArrayRegion(ENV, array, 0, len > 1 ? 2 : 1, values);
}

static void set_object_array_first(jobjectArray array, jobject value) {
    if (!is_probable_java_array_ref(array) || jni->GetArrayLength(ENV, array) <= 0) {
        return;
    }

    jni->SetObjectArrayElement(ENV, array, 0, value);
}

static jbyteArray byte_array_from_cstr(const char *text) {
    size_t len;
    jbyteArray array;

    if (!text) {
        return NULL;
    }

    len = strlen(text) + 1;
    array = jni->NewByteArray(ENV, (jsize)len);
    if (!array) {
        return NULL;
    }

    jni->SetByteArrayRegion(ENV, array, 0, (jsize)len, (const jbyte *)text);
    return array;
}

static void byte_array_to_cstr(jbyteArray array, char *dst, size_t dst_size) {
    jbyte *bytes;
    jsize len;

    if (!dst || dst_size == 0) {
        return;
    }

    dst[0] = '\0';
    if (!is_probable_java_array_ref(array)) {
        if (is_probable_c_string_ptr(array)) {
            snprintf(dst, dst_size, "%s", (const char *)array);
            sceClibPrintf("[JNI-BYTES] raw-cstr 0x%x -> '%s'\n",
                          (unsigned int)(uintptr_t)array, dst);
        } else if (array != NULL) {
            sceClibPrintf("[JNI-BYTES] unexpected ref 0x%x (not byte array)\n",
                          (unsigned int)(uintptr_t)array);
        }
        return;
    }

    len = jni->GetArrayLength(ENV, array);
    if (len <= 0) {
        sceClibPrintf("[JNI-BYTES] array 0x%x has len=%d\n",
                      (unsigned int)(uintptr_t)array, (int)len);
        return;
    }

    bytes = jni->GetByteArrayElements(ENV, array, NULL);
    if (!bytes) {
        return;
    }

    if ((size_t)len >= dst_size) {
        len = (jsize)(dst_size - 1);
    }

    memcpy(dst, bytes, (size_t)len);
    dst[len] = '\0';
    sceClibPrintf("[JNI-BYTES] jbyteArray 0x%x len=%d -> '%s'\n",
                  (unsigned int)(uintptr_t)array, (int)len, dst);
    jni->ReleaseByteArrayElements(ENV, array, bytes, JNI_ABORT);
}

static bool has_suffix(const char *text, const char *suffix) {
    size_t text_len;
    size_t suffix_len;

    if (!text || !suffix) {
        return false;
    }

    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return false;
    }

    return strcmp(text + text_len - suffix_len, suffix) == 0;
}

static const char *path_basename(const char *path) {
    const char *slash;
    const char *backslash;
    const char *base;

    if (!path) {
        return "";
    }

    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    base = slash;
    if (!base || (backslash && backslash > base)) {
        base = backslash;
    }
    return base ? base + 1 : path;
}

static bool is_resdl_pack_file(const char *path) {
    const char *name = path_basename(path);

    return has_suffix(name, ".big") || strstr(name, "packTOC_") != NULL;
}

static bool resolve_apk_file_cached(char *dst, size_t dst_size,
                                    const char *request, struct stat *out_st,
                                    GunBrosDataLocation *out_location) {
    bool cacheable = request && strlen(request) < sizeof(g_apk_resource_path_cache[0].request) &&
                     is_resdl_pack_file(request);
    size_t i;

    if (cacheable) {
        for (i = 0; i < APK_RESOURCE_PATH_CACHE_MAX; ++i) {
            const ApkResourcePathCacheEntry *entry = &g_apk_resource_path_cache[i];
            if (entry->used && strcmp(entry->request, request) == 0) {
                if (dst && dst_size) {
                    snprintf(dst, dst_size, "%s", entry->resolved);
                }
                if (out_st) {
                    *out_st = entry->st;
                }
                if (out_location) {
                    *out_location = entry->location;
                }
                return true;
            }
        }
    }

    {
        struct stat st;
        GunBrosDataLocation location = GUNBROS_DATA_LOCATION_MISSING;
        bool resolved = gunbros_resolve_data_path_ex(dst, dst_size, request, false,
                                                     &st, &location);

        if (out_st) {
            *out_st = st;
        }
        if (out_location) {
            *out_location = location;
        }

        /* Cache successful immutable pack paths only. Missing/bootstrap/save
         * files may legitimately be created later and must remain observable. */
        if (resolved && cacheable) {
            ApkResourcePathCacheEntry *entry = NULL;
            for (i = 0; i < APK_RESOURCE_PATH_CACHE_MAX; ++i) {
                if (!g_apk_resource_path_cache[i].used) {
                    entry = &g_apk_resource_path_cache[i];
                    break;
                }
            }
            if (!entry) {
                entry = &g_apk_resource_path_cache[
                    g_apk_resource_path_cache_replace++ % APK_RESOURCE_PATH_CACHE_MAX];
            }
            entry->used = true;
            snprintf(entry->request, sizeof(entry->request), "%s", request);
            snprintf(entry->resolved, sizeof(entry->resolved), "%s", dst ? dst : "");
            entry->st = st;
            entry->location = location;
        }
        return resolved;
    }
}

static bool is_resdl_online_bootstrap_file(const char *path) {
    const char *name = path_basename(path);

    return strcmp(name, "contentTrackerPackData") == 0 ||
           strcmp(name, "Credentials.dat") == 0 ||
           strcmp(name, "Session.dat") == 0;
}

static void ensure_online_bootstrap_file(const char *path) {
    const char *name = path_basename(path);
    char full_path[512];
    struct stat st;
    int fd;
    static const char placeholder[] = "{}\n";

    if (strcmp(name, "Credentials.dat") != 0 && strcmp(name, "Session.dat") != 0) {
        return;
    }

    snprintf(full_path, sizeof(full_path), DATA_PATH "%s", name);
    if (stat(full_path, &st) == 0) {
        return;
    }

    fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        sceClibPrintf("[JNI-CB] bootstrap create '%s' failed errno=%d\n", full_path, errno);
        return;
    }

    (void)write(fd, placeholder, sizeof(placeholder) - 1);
    close(fd);
    sceClibPrintf("[JNI-CB] bootstrap created '%s' (%u bytes)\n",
                  full_path, (unsigned int)(sizeof(placeholder) - 1));
}

static const char *get_resdl_base_path(void) {
    return gunbros_get_preferred_resource_base_path();
}

static void ensure_java_state(void) {
    if (!fieldsObject[FIELD_OBJECT_INSTANCE].value) {
        fieldsObject[FIELD_OBJECT_INSTANCE].value = DUMMY_ACTIVITY;
    }

    if (!g_window_service) {
        g_window_service = jstr_new("window");
        fieldsObject[FIELD_OBJECT_WINDOW_SERVICE].value = g_window_service;
    }

    if (!g_model) {
        g_model = jstr_new("PlayStation Vita");
        fieldsObject[FIELD_OBJECT_MODEL].value = g_model;
    }

    if (!g_egl_no_context) {
        g_egl_no_context = (jobject)0x10001000;
        g_egl_no_display = (jobject)0x10001010;
        g_egl_no_surface = (jobject)0x10001020;
        fieldsObject[FIELD_OBJECT_EGL_NO_CONTEXT].value = g_egl_no_context;
        fieldsObject[FIELD_OBJECT_EGL_NO_DISPLAY].value = g_egl_no_display;
        fieldsObject[FIELD_OBJECT_EGL_NO_SURFACE].value = g_egl_no_surface;
    }
    if (!g_external_storage_path) {
        g_external_storage_path = jstr_new(DATA_PATH);
    }
    if (!g_external_storage_state) {
        g_external_storage_state = jstr_new("mounted");
    }
    fieldsObject[FIELD_OBJECT_MEDIA_MOUNTED].value = g_external_storage_state;
}

void gunbros_java_init(void) {
    ensure_java_state();
    fieldsInt[FIELD_INT_SDK_INT].value = 19;
    fieldsInt[FIELD_INT_DESCRIPTOR].value = -1;
    fieldsLong[FIELD_LONG_ASSET_START_OFFSET].value = 0;
    fieldsLong[FIELD_LONG_ASSET_LENGTH].value = 0;
    fieldsLong[FIELD_LONG_ENUM_LENGTH].value = 0;
    fieldsObject[FIELD_OBJECT_ASSET_FD].value = NULL;
    fieldsObject[FIELD_OBJECT_ENUM_FILENAME].value = NULL;
}

static void SetTickRate(jmethodID id, va_list args) {
    (void)id;

    gunbros_tick_rate_ms = int_arg(args);
    if (gunbros_tick_rate_ms <= 0) {
        gunbros_tick_rate_ms = 16;
    }

    sceClibPrintf("[JNI-CB] SetTickRate(%d)\n", gunbros_tick_rate_ms);
}

static void FinishApp(jmethodID id, va_list args) {
    (void)id;
    (void)args;

    gunbros_finish_requested = 1;
    sceClibPrintf("[JNI-CB] FinishApp()\n");
}

static jboolean LaunchURL(jmethodID id, va_list args) {
    (void)id;

    jstring url = va_arg(args, jstring);
    jint mode = int_arg(args);
    const char *text = jstr_cstr(url);

    sceClibPrintf("[JNI-CB] LaunchURL('%s', %d)\n", text ? text : "(null)", mode);
    jstr_release(url, text);
    return JNI_TRUE;
}

static jobject OpenAPKFile(jmethodID id, va_list args) {
    (void)id;

    jstring path = va_arg(args, jstring);
    const char *text = jstr_cstr(path);
    char full_path[512];
    struct stat st;
    GunBrosDataLocation location;
    bool resolved;

    close_asset_fd();
    memset(&st, 0, sizeof(st));
    location = GUNBROS_DATA_LOCATION_MISSING;
    resolved = resolve_apk_file_cached(full_path, sizeof(full_path), text, &st, &location);

    if (text && resolved && S_ISREG(st.st_mode)) {
        g_asset_fd = open(full_path, O_RDONLY);
        if (g_asset_fd >= 0) {
            gunbros_track_fd_path(g_asset_fd, full_path);
            g_asset_length = (jlong)st.st_size;
            g_asset_descriptor = g_asset_fd;
            fieldsInt[FIELD_INT_DESCRIPTOR].value = g_asset_descriptor;
            fieldsLong[FIELD_LONG_ASSET_START_OFFSET].value = 0;
            fieldsLong[FIELD_LONG_ASSET_LENGTH].value = g_asset_length;
            fieldsObject[FIELD_OBJECT_ASSET_FD].value = DUMMY_FILE_DESCRIPTOR;
            sceClibPrintf("[JNI-CB] OpenAPKFile('%s') -> fd=%d len=%lld (%s)\n",
                          full_path, g_asset_fd, (long long)g_asset_length,
                          gunbros_data_location_name(location));
            if (g_asset_length <= 0) {
                sceClibPrintf("[ASSET-JNI] OpenAPKFile zero-length requested='%s' resolved='%s' location=%s\n",
                              text ? text : "(null)",
                              full_path,
                              gunbros_data_location_name(location));
            }
            jstr_release(path, text);
            return DUMMY_ASSET_FILE_DESCRIPTOR;
        }

        sceClibPrintf("[ASSET-JNI] OpenAPKFile open failed requested='%s' resolved='%s' location=%s size=%lld errno=%d\n",
                      text ? text : "(null)",
                      full_path,
                      gunbros_data_location_name(location),
                      (long long)st.st_size,
                      errno);
    } else {
        sceClibPrintf("[ASSET-JNI] OpenAPKFile resolve miss requested='%s' resolved='%s' location=%s resolved_ok=%d mode=0x%08x\n",
                      text ? text : "(null)",
                      full_path,
                      gunbros_data_location_name(location),
                      resolved ? 1 : 0,
                      (unsigned int)st.st_mode);
    }

    sceClibPrintf("[JNI-CB] OpenAPKFile('%s') -> NULL\n", text ? text : "(null)");
    jstr_release(path, text);
    return NULL;
}

static jboolean APKExistsDir(jmethodID id, va_list args) {
    (void)id;

    jstring path = va_arg(args, jstring);
    const char *text = jstr_cstr(path);
    char full_path[512];
    bool exists;

    exists = resolve_data_path(full_path, sizeof(full_path), text, true);

    sceClibPrintf("[JNI-CB] APKExistsDir('%s') -> %d\n", full_path, exists ? 1 : 0);
    jstr_release(path, text);
    return exists ? JNI_TRUE : JNI_FALSE;
}

static jint GetAndroidMinBufferSize(jmethodID id, va_list args) {
    (void)id;

    jint sample_rate = int_arg(args);
    jint channels = int_arg(args);
    jint format = int_arg(args);

    gunbros_audio_configure_pcm((unsigned int)sample_rate,
                                (unsigned int)channels,
                                (unsigned int)format);

    sceClibPrintf("[JNI-CB] GetAndroidMinBufferSize(%d,%d,%d)\n",
                  sample_rate, channels, format);
    return 4096;
}

static jboolean InitialiseSoundEngine(jmethodID id, va_list args) {
    jint init_flag;
    jint sample_rate;
    jint channels;
    jint sample_bits;
    jint buffer_size;
    jint buffer_count;
    int started;

    (void)id;
    /* (ZIIIII)Z: media description flag, sample rate, channel count, sample
     * bits, then the two Android buffer-description values. */
    init_flag = int_arg(args);
    sample_rate = int_arg(args);
    channels = int_arg(args);
    sample_bits = int_arg(args);
    buffer_size = int_arg(args);
    buffer_count = int_arg(args);

    gunbros_audio_configure_pcm((unsigned int)sample_rate,
                                (unsigned int)channels,
                                (unsigned int)sample_bits);
    started = gunbros_audio_start_pcm();
    GUNBROS_JAVA_AUDIO_LOG("[AUDIO-PCM][JNI-INIT] flag=%d rate=%d channels=%d "
                    "bits=%d buffer=%d/%d result=%d\n",
                    init_flag, sample_rate, channels, sample_bits,
                    buffer_size, buffer_count, started);
    return started ? JNI_TRUE : JNI_FALSE;
}

static void DestroySoundEngine(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    gunbros_audio_stop_pcm();
    sceClibPrintf("[JNI-CB] DestroySoundEngine()\n");
}

static jboolean InitialiseVibrationEngine(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return JNI_TRUE;
}

static void DestroyVibrationEngine(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

static jboolean IsPSP(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return JNI_FALSE;
}

static void SetVolume(jmethodID id, va_list args) {
    jint volume;

    (void)id;
    /* Consume the JNI argument even when runtime diagnostics are compiled out. */
    volume = int_arg(args);
    sceClibPrintf("[JNI-CB] SetVolume(%d)\n", volume);
}

static jboolean StartAudioStream(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return gunbros_audio_start_pcm() ? JNI_TRUE : JNI_FALSE;
}

static void StopAudioStream(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    gunbros_audio_stop_pcm();
}

static jboolean PlayVibration(jmethodID id, va_list args) {
    (void)id;
    (void)long_arg(args);
    return JNI_TRUE;
}

static jboolean StopVibration(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return JNI_TRUE;
}

static jobject InitialiseSoundEvent(jmethodID id, va_list args) {
    (void)id;

    jstring path = va_arg(args, jstring);
    jlong event_id = (jlong)long_arg(args);
    const char *text = jstr_cstr(path);

    sceClibPrintf("[JNI-CB] InitialiseSoundEvent('%s', %lld)\n",
                  text ? text : "(null)", (long long)event_id);
    jstr_release(path, text);
    return DUMMY_MEDIA_PLAYER;
}

static jboolean SetDeviceOrientation(jmethodID id, va_list args) {
    (void)id;
    (void)int_arg(args);
    return JNI_TRUE;
}

static jint GetDeviceOrientation(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return 0;
}

static jboolean StartMovieActivity(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jstring);
    (void)long_arg(args);
    return JNI_FALSE;
}

static jboolean EnumInit(jmethodID id, va_list args) {
    (void)id;

    jstring path = va_arg(args, jstring);
    const char *text = jstr_cstr(path);

    close_enum_dir();
    resolve_data_path(g_enum_path, sizeof(g_enum_path), text, true);
    g_enum_dirs = int_arg(args) != 0;
    g_enum_dir = opendir(g_enum_path);

    sceClibPrintf("[JNI-CB] EnumInit('%s', dirs=%d) -> %s\n",
                  g_enum_path, g_enum_dirs ? 1 : 0, g_enum_dir ? "ok" : "fail");

    jstr_release(path, text);
    return g_enum_dir ? JNI_TRUE : JNI_FALSE;
}

static jobject EnumNext(jmethodID id, va_list args) {
    (void)id;
    (void)args;

    struct dirent *entry;

    if (!g_enum_dir) {
        return NULL;
    }

    while ((entry = readdir(g_enum_dir)) != NULL) {
        char full_path[512];
        struct stat st;
        bool is_dir;

        if (entry->d_name[0] == '.') {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", g_enum_path, entry->d_name);
        if (stat(full_path, &st) < 0) {
            continue;
        }

        is_dir = S_ISDIR(st.st_mode) != 0;
        if (g_enum_dirs != is_dir) {
            continue;
        }

        g_enum_filename = jstr_new(entry->d_name);
        fieldsObject[FIELD_OBJECT_ENUM_FILENAME].value = g_enum_filename;
        fieldsLong[FIELD_LONG_ENUM_LENGTH].value = is_dir ? 0 : (jlong)st.st_size;
        return DUMMY_ENUM_RESULT;
    }

    close_enum_dir();
    return NULL;
}

static jlong GetDeviceAvailableMemory(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return (jlong)(96 * 1024 * 1024);
}

static void SystemMessageBox(jmethodID id, va_list args) {
    (void)id;

    jstring title = va_arg(args, jstring);
    jstring message = va_arg(args, jstring);
    const char *title_text = jstr_cstr(title);
    const char *message_text = jstr_cstr(message);

    sceClibPrintf("[JNI-CB] SystemMessageBox('%s', '%s')\n",
                  title_text ? title_text : "(null)",
                  message_text ? message_text : "(null)");

    jstr_release(title, title_text);
    jstr_release(message, message_text);
}

static jobject CreateEGL(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return DUMMY_EGL;
}

static void SetAutoRotationValues(jmethodID id, va_list args) {
    (void)id;
    (void)long_arg(args);
    (void)long_arg(args);
    (void)long_arg(args);
    (void)long_arg(args);
}

static jboolean StartAutoRotation(jmethodID id, va_list args) {
    (void)id;
    (void)long_arg(args);
    return JNI_TRUE;
}

static void StopAutoRotation(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

static void StartLocalNotificationService(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

static void AddLocalNotification(jmethodID id, va_list args) {
    (void)id;
    (void)long_arg(args);
    (void)long_arg(args);
    (void)va_arg(args, jstring);
    (void)va_arg(args, jstring);
}

static void RemoveLocalNotification(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jstring);
    (void)va_arg(args, jstring);
}

static void CancelAllLocalNotifications(jmethodID id, va_list args) {
    (void)id;
    (void)args;
}

static void EnablePushNotifications(jmethodID id, va_list args) {
    (void)id;
    (void)int_arg(args);
}

static void EnableMultipleTouch(jmethodID id, va_list args) {
    (void)id;
    (void)int_arg(args);
}

static jboolean IsUserOnWiFi(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    log_network_once("IsUserOnWiFi()", "false");
    return JNI_FALSE;
}

static jint mp3Event(jmethodID id, va_list args) {
    static unsigned int passive_log_count;
    static jint passive_last_event = -1;
    static jint passive_last_result = -1;
    jint event;
    jint value;
    jstring path;
    const char *text;
    jint result = 1;

    (void)id;
    event = int_arg(args);
    value = int_arg(args);
    path = va_arg(args, jstring);
    text = jstr_cstr(path);
    switch (event) {
        case 1:
            result = gunbros_music_load(text);
            gunbros_music_set_looping(value != 0);
            if (result) {
                gunbros_music_start();
            }
            result = gunbros_music_is_playing();
            break;
        case 2:
            gunbros_music_pause();
            result = 1;
            break;
        case 3:
            gunbros_music_start();
            result = gunbros_music_is_playing();
            break;
        case 4:
            gunbros_music_stop();
            result = 1;
            break;
        case 5:
            result = gunbros_music_is_playing();
            break;
        case 15: {
            const float volume = value <= 0 ? 0.0f :
                                 value >= 100 ? 1.0f : value / 100.0f;
            gunbros_music_set_volume(volume, volume);
            result = 1;
            break;
        }
        default:
            result = 0;
            break;
    }

    if ((event != 5 && event != 15) || passive_log_count < 8 ||
        event != passive_last_event || result != passive_last_result) {
        GUNBROS_JAVA_AUDIO_LOG("[AUDIO-MUSIC][JNI] event=%d value=%d request='%s' "
                        "result=%d\n",
                        event, value, text ? text : "", result);
        if (event == 5 || event == 15) {
            ++passive_log_count;
            passive_last_event = event;
            passive_last_result = result;
        }
    }
    jstr_release(path, text);
    return result;
}

static jint resdlEvent_impl(jint event, jint arg1, jbyteArray array) {
    char path[512];

    sceClibPrintf("[JNI-CB] resdlEvent raw(event=%d, arg1=%d, ref=0x%x)\n",
                  event, arg1, (unsigned int)(uintptr_t)array);
    byte_array_to_cstr(array, path, sizeof(path));

    sceClibPrintf("[JNI-CB] resdlEvent(event=%d, '%s')\n",
                  event, path[0] ? path : "(null)");

    if (event != 2) {
        sceClibPrintf("[JNI-CB] resdlEvent result=0 kind=ignored-event\n");
        return 0;
    }

    if (path[0] == '\0') {
        sceClibPrintf("[JNI-CB] resdlEvent fallback -> 0 (missing filename)\n");
        return 0;
    }

    if (is_resdl_pack_file(path)) {
        sceClibPrintf("[JNI-CB] resdlEvent result=1 kind=pack-file\n");
        return 1;
    }

    if (is_resdl_online_bootstrap_file(path)) {
        ensure_online_bootstrap_file(path);
        sceClibPrintf("[JNI-CB] resdlEvent result=0 kind=online-bootstrap-local\n");
        return 0;
    }

    sceClibPrintf("[JNI-CB] resdlEvent result=0 kind=normal-file\n");
    return 0;
}

static jint resdlEvent(jmethodID id, va_list args) {
    jint event = int_arg(args);
    jint arg1 = int_arg(args);
    jbyteArray array = va_arg(args, jbyteArray);

    (void)id;
    return resdlEvent_impl(event, arg1, array);
}

static jobject resdlStringEvent_impl(jint event, jint arg1, jbyteArray array) {
    char path[512];
    const char *base_path;

    if (event != 1) {
        sceClibPrintf("[JNI-CB] resdlStringEvent ignored(event=%d, arg1=%d, ref=0x%x)\n",
                      event, arg1, (unsigned int)(uintptr_t)array);
        return NULL;
    }

    sceClibPrintf("[JNI-CB] resdlStringEvent raw(event=%d, arg1=%d, ref=0x%x)\n",
                  event, arg1, (unsigned int)(uintptr_t)array);
    path[0] = '\0';
    if (is_probable_java_array_ref(array)) {
        byte_array_to_cstr(array, path, sizeof(path));
    }
    base_path = get_resdl_base_path();

    sceClibPrintf("[JNI-CB] resdlStringEvent('%s') -> '%s'\n",
                  path[0] ? path : "(null)", base_path);
    return byte_array_from_cstr(base_path);
}

static jobject resdlStringEvent(jmethodID id, va_list args) {
    jint event = int_arg(args);
    jint arg1 = int_arg(args);
    jbyteArray array = va_arg(args, jbyteArray);

    (void)id;
    return resdlStringEvent_impl(event, arg1, array);
}

static jobject eglGetDisplay(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    return DUMMY_EGL_DISPLAY;
}

static jobject eglGetCurrentDisplay(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return DUMMY_EGL_DISPLAY;
}

static jboolean eglInitialize(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    set_int_array_pair(va_arg(args, jintArray), 1, 4);
    return JNI_TRUE;
}

static jboolean eglSwapBuffers(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    return JNI_TRUE;
}

static jobject eglCreateContext(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    (void)va_arg(args, jintArray);
    return DUMMY_EGL_CONTEXT;
}

static jboolean eglDestroyContext(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    return JNI_TRUE;
}

static jboolean eglMakeCurrent(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    return JNI_TRUE;
}

static jobject eglGetCurrentContext(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return DUMMY_EGL_CONTEXT;
}

static jboolean eglGetConfigs(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    set_object_array_first(va_arg(args, jobjectArray), DUMMY_EGL_CONFIG);
    (void)int_arg(args);
    set_int_array_first(va_arg(args, jintArray), 1);
    return JNI_TRUE;
}

static jboolean eglGetConfigAttrib(jmethodID id, va_list args) {
    (void)id;

    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    jint attr = int_arg(args);
    jintArray out = va_arg(args, jintArray);
    jint value = 8;

    switch (attr) {
        case 0x3020: value = 32; break; /* EGL_BUFFER_SIZE */
        case 0x3021: value = 8; break;  /* EGL_ALPHA_SIZE */
        case 0x3022: value = 8; break;  /* EGL_BLUE_SIZE */
        case 0x3023: value = 8; break;  /* EGL_GREEN_SIZE */
        case 0x3024: value = 8; break;  /* EGL_RED_SIZE */
        case 0x3025: value = 16; break; /* EGL_DEPTH_SIZE */
        case 0x3026: value = 0; break;  /* EGL_STENCIL_SIZE */
        case 0x3031: value = 4; break;  /* EGL_SAMPLES */
        case 0x3032: value = 1; break;  /* EGL_SAMPLE_BUFFERS */
        default: break;
    }

    set_int_array_first(out, value);
    return JNI_TRUE;
}

static jboolean eglChooseConfig(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    (void)va_arg(args, jintArray);
    set_object_array_first(va_arg(args, jobjectArray), DUMMY_EGL_CONFIG);
    (void)int_arg(args);
    set_int_array_first(va_arg(args, jintArray), 1);
    return JNI_TRUE;
}

static jobject eglCreateWindowSurface(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    (void)va_arg(args, jintArray);
    return DUMMY_EGL_SURFACE;
}

static jobject eglCreatePbufferSurface(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    (void)va_arg(args, jintArray);
    return DUMMY_EGL_SURFACE;
}

static jboolean eglDestroySurface(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    (void)va_arg(args, jobject);
    return JNI_TRUE;
}

static jboolean eglTerminate(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    return JNI_TRUE;
}

static jint eglGetError(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return 0x3000;
}

static jobject eglQueryString(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jobject);
    (void)int_arg(args);
    return jstr_new("");
}

static jobject getFileDescriptor(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sceClibPrintf("[JNI-CB] getFileDescriptor() -> %s\n",
                  g_asset_fd >= 0 ? "fd" : "NULL");
    return g_asset_fd >= 0 ? DUMMY_FILE_DESCRIPTOR : NULL;
}

static jlong getLength(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sceClibPrintf("[JNI-CB] getLength() -> %lld\n", (long long)g_asset_length);
    return g_asset_length;
}

static jlong getStartOffset(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sceClibPrintf("[JNI-CB] getStartOffset() -> 0\n");
    return 0;
}

jboolean gunbros_new_object_for_class(jclass clazz, jobject *out_value) {
    const char *class_name = (const char *)clazz;

    if (!out_value || !class_name) {
        return JNI_FALSE;
    }

    if (strcmp(class_name, "android/content/res/AssetFileDescriptor") == 0) {
        *out_value = DUMMY_ASSET_FILE_DESCRIPTOR;
        return JNI_TRUE;
    }

    if (strcmp(class_name, "java/io/FileDescriptor") == 0) {
        *out_value = DUMMY_FILE_DESCRIPTOR;
        return JNI_TRUE;
    }

    if (strcmp(class_name, "java/util/TimeZone") == 0) {
        *out_value = DUMMY_TIMEZONE;
        return JNI_TRUE;
    }

    if (strcmp(class_name, "android/net/NetworkInfo") == 0) {
        *out_value = DUMMY_NETWORK_INFO;
        return JNI_TRUE;
    }

    if (strcmp(class_name, "com/glu/android/admarvel/GluAdMarvel") == 0) {
        log_optional_jni_once("GluAdMarvel.<init>");
        *out_value = DUMMY_ADMARVEL;
        return JNI_TRUE;
    }

    if (strcmp(class_name, "java/util/HashMap") == 0) {
        log_optional_jni_once("HashMap.<init>");
        *out_value = DUMMY_HASH_MAP;
        return JNI_TRUE;
    }

    if (strcmp(class_name, "android/content/ContentResolver") == 0) {
        log_optional_jni_once("ContentResolver.<init>");
        *out_value = DUMMY_CONTENT_RESOLVER;
        return JNI_TRUE;
    }

    if (strcmp(class_name, "android/media/MediaPlayer") == 0) {
        *out_value = DUMMY_MEDIA_PLAYER;
        return JNI_TRUE;
    }

    if (strcmp(class_name, "javax/microedition/khronos/egl/EGLDisplay") == 0) {
        *out_value = DUMMY_EGL_DISPLAY;
        return JNI_TRUE;
    }

    if (strcmp(class_name, "javax/microedition/khronos/egl/EGLContext") == 0) {
        *out_value = DUMMY_EGL_CONTEXT;
        return JNI_TRUE;
    }

    if (strcmp(class_name, "javax/microedition/khronos/egl/EGLSurface") == 0) {
        *out_value = DUMMY_EGL_SURFACE;
        return JNI_TRUE;
    }

    return JNI_FALSE;
}

jboolean gunbros_long_method_call_noargs(jmethodID id, jlong *out_value) {
    if (!out_value) {
        return JNI_FALSE;
    }

    switch ((int)(intptr_t)id) {
        case MID_GetDeviceAvailableMemory:
            *out_value = (jlong)(96 * 1024 * 1024);
            return JNI_TRUE;
        case MID_getLength:
            *out_value = g_asset_length;
            return JNI_TRUE;
        case MID_getStartOffset:
            *out_value = 0;
            return JNI_TRUE;
        default:
            return JNI_FALSE;
    }
}

jboolean gunbros_object_method_call_noargs(jmethodID id, jobject *out_value) {
    struct dirent *entry;

    if (!out_value) {
        return JNI_FALSE;
    }

    switch ((int)(intptr_t)id) {
        case MID_EnumNext:
            if (!g_enum_dir) {
                *out_value = NULL;
                return JNI_TRUE;
            }

            while ((entry = readdir(g_enum_dir)) != NULL) {
                char full_path[512];
                struct stat st;
                bool is_dir;

                if (entry->d_name[0] == '.') {
                    continue;
                }

                snprintf(full_path, sizeof(full_path), "%s/%s", g_enum_path, entry->d_name);
                if (stat(full_path, &st) < 0) {
                    continue;
                }

                is_dir = S_ISDIR(st.st_mode) != 0;
                if (g_enum_dirs != is_dir) {
                    continue;
                }

                g_enum_filename = jstr_new(entry->d_name);
                fieldsObject[FIELD_OBJECT_ENUM_FILENAME].value = g_enum_filename;
                fieldsLong[FIELD_LONG_ENUM_LENGTH].value = is_dir ? 0 : (jlong)st.st_size;
                *out_value = DUMMY_ENUM_RESULT;
                return JNI_TRUE;
            }

            close_enum_dir();
            *out_value = NULL;
            return JNI_TRUE;
        case MID_CreateEGL:
            *out_value = DUMMY_EGL;
            return JNI_TRUE;
        case MID_eglGetCurrentDisplay:
            *out_value = DUMMY_EGL_DISPLAY;
            return JNI_TRUE;
        case MID_eglGetCurrentContext:
            *out_value = DUMMY_EGL_CONTEXT;
            return JNI_TRUE;
        case MID_getFileDescriptor:
            sceClibPrintf("[JNI-CB] getFileDescriptor() -> %s\n",
                          g_asset_fd >= 0 ? "fd" : "NULL");
            *out_value = g_asset_fd >= 0 ? DUMMY_FILE_DESCRIPTOR : NULL;
            return JNI_TRUE;
        case MID_getDefault:
            *out_value = DUMMY_TIMEZONE;
            return JNI_TRUE;
        case MID_getID:
            *out_value = jstr_new("UTC");
            return JNI_TRUE;
        case MID_getActiveNetworkInfo:
            log_network_once("getActiveNetworkInfo()", "NULL (offline)");
            *out_value = NULL;
            return JNI_TRUE;
        case MID_GluAdMarvel_ctor:
            log_optional_jni_once("GluAdMarvel.<init>");
            *out_value = DUMMY_ADMARVEL;
            return JNI_TRUE;
        case MID_HashMap_ctor:
            log_optional_jni_once("HashMap.<init>");
            *out_value = DUMMY_HASH_MAP;
            return JNI_TRUE;
        case MID_HashMap_put:
            log_optional_jni_once("HashMap.put");
            *out_value = NULL;
            return JNI_TRUE;
        case MID_getContentResolver:
            log_optional_jni_once("getContentResolver");
            *out_value = DUMMY_CONTENT_RESOLVER;
            return JNI_TRUE;
        case MID_Settings_getString:
            log_optional_jni_once("Settings.Secure.getString");
            *out_value = jstr_new("vita-gunbros-android-id");
            return JNI_TRUE;
        case MID_getExternalStorageDirectory:
            ensure_java_state();
            *out_value = DUMMY_EXTERNAL_STORAGE_FILE;
            return JNI_TRUE;
        case MID_getExternalStorageState:
            ensure_java_state();
            *out_value = g_external_storage_state;
            return JNI_TRUE;
        case MID_File_toString:
            ensure_java_state();
            *out_value = g_external_storage_path;
            return JNI_TRUE;
        default:
            return JNI_FALSE;
    }
}

jboolean gunbros_boolean_method_call_noargs(jmethodID id, jboolean *out_value) {
    if (!out_value) {
        return JNI_FALSE;
    }

    switch ((int)(intptr_t)id) {
        case MID_IsPSP:
            *out_value = JNI_FALSE;
            return JNI_TRUE;
        case MID_StartAudioStream:
            *out_value = gunbros_audio_start_pcm() ? JNI_TRUE : JNI_FALSE;
            return JNI_TRUE;
        case MID_StopVibration:
            *out_value = JNI_TRUE;
            return JNI_TRUE;
        case MID_IsUserOnWiFi:
            log_network_once("IsUserOnWiFi()", "false");
            *out_value = JNI_FALSE;
            return JNI_TRUE;
        case MID_isAvailable:
            log_network_once("NetworkInfo.isAvailable()", "false");
            *out_value = JNI_FALSE;
            return JNI_TRUE;
        case MID_interfaceIsOpen:
            log_network_once("interfaceIsOpen()", "false (offline)");
            *out_value = JNI_FALSE;
            return JNI_TRUE;
        case MID_isVisible:
            log_optional_jni_once("GluAdMarvel.isVisible");
            *out_value = JNI_FALSE;
            return JNI_TRUE;
        case MID_canDisplayInterface:
            log_optional_jni_once("canDisplayInterface() -> false (Tapjoy disabled)");
            *out_value = JNI_FALSE;
            return JNI_TRUE;
        case MID_File_exists:
            *out_value = JNI_TRUE;
            return JNI_TRUE;
        default:
            return JNI_FALSE;
    }
}

jboolean gunbros_int_method_call_noargs(jmethodID id, jint *out_value) {
    if (!out_value) {
        return JNI_FALSE;
    }

    switch ((int)(intptr_t)id) {
        case MID_GetDeviceOrientation:
            *out_value = 0;
            return JNI_TRUE;
        case MID_eglGetError:
            *out_value = 0x3000;
            return JNI_TRUE;
        case MID_getCurrentPosition:
            *out_value = gunbros_music_get_position_ms();
            return JNI_TRUE;
        case MID_nfcEnabled:
            *out_value = 0;
            return JNI_TRUE;
        case MID_iapEvent:
            log_optional_jni_once("iapEvent");
            *out_value = 0;
            return JNI_TRUE;
        default:
            return JNI_FALSE;
    }
}

jboolean gunbros_int_method_call_arrayargs(jmethodID id, const jvalue *args, jint *out_value) {
    if (!args || !out_value) {
        return JNI_FALSE;
    }

    switch ((int)(intptr_t)id) {
        case MID_resdlEvent:
            *out_value = resdlEvent_impl(args[0].i, args[1].i, (jbyteArray)args[2].l);
            return JNI_TRUE;
        default:
            return JNI_FALSE;
    }
}

jboolean gunbros_object_method_call_arrayargs(jmethodID id, const jvalue *args, jobject *out_value) {
    if (!args || !out_value) {
        return JNI_FALSE;
    }

    switch ((int)(intptr_t)id) {
        case MID_resdlStringEvent:
            *out_value = resdlStringEvent_impl(args[0].i, args[1].i, (jbyteArray)args[2].l);
            return JNI_TRUE;
        default:
            return JNI_FALSE;
    }
}

jboolean gunbros_void_method_call_noargs(jmethodID id) {
    switch ((int)(intptr_t)id) {
        case MID_FinishApp:
            gunbros_finish_requested = 1;
            sceClibPrintf("[JNI-CB] FinishApp()\n");
            return JNI_TRUE;
        case MID_DestroySoundEngine:
            gunbros_audio_stop_pcm();
            sceClibPrintf("[JNI-CB] DestroySoundEngine()\n");
            return JNI_TRUE;
        case MID_DestroyVibrationEngine:
            return JNI_TRUE;
        case MID_StopAudioStream:
            gunbros_audio_stop_pcm();
            return JNI_TRUE;
        case MID_StopAutoRotation:
        case MID_StartLocalNotificationService:
        case MID_CancelAllLocalNotifications:
            return JNI_TRUE;
        case MID_start:
            gunbros_music_start();
            return JNI_TRUE;
        case MID_pause:
            gunbros_music_pause();
            return JNI_TRUE;
        case MID_stop:
            gunbros_music_stop();
            return JNI_TRUE;
        case MID_release:
            gunbros_music_release();
            return JNI_TRUE;
        case MID_showAd:
            log_optional_jni_once("GluAdMarvel.showAd");
            return JNI_TRUE;
        case MID_hideAd:
            log_optional_jni_once("GluAdMarvel.hideAd");
            return JNI_TRUE;
        case MID_tick:
            log_optional_jni_once("GluAdMarvel.tick");
            return JNI_TRUE;
        case MID_facebookEvent:
            log_optional_jni_once("facebookEvent");
            return JNI_TRUE;
        case MID_onStartSession:
            log_optional_jni_once("onStartSession");
            return JNI_TRUE;
        case MID_onEvent:
            log_optional_jni_once("onEvent");
            return JNI_TRUE;
        case MID_onresume:
            log_optional_jni_once("onresume");
            return JNI_TRUE;
        case MID_queryFeaturedApp:
            log_optional_jni_once("queryFeaturedApp() -> no-op (Tapjoy offline)");
            return JNI_TRUE;
        case MID_close:
            sceClibPrintf("[JNI-CB] close()\n");
            close_asset_fd();
            return JNI_TRUE;
        default:
            return JNI_FALSE;
    }
}

jboolean gunbros_float_method_call_noargs(jmethodID id, jfloat *out_value) {
    if (!out_value) {
        return JNI_FALSE;
    }

    switch ((int)(intptr_t)id) {
        default:
            return JNI_FALSE;
    }
}

jboolean gunbros_double_method_call_noargs(jmethodID id, jdouble *out_value) {
    if (!out_value) {
        return JNI_FALSE;
    }

    switch ((int)(intptr_t)id) {
        default:
            return JNI_FALSE;
    }
}

static void closeMethod(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    sceClibPrintf("[JNI-CB] close()\n");
    close_asset_fd();
}

static jobject getDefault(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return DUMMY_TIMEZONE;
}

static jobject getExternalStorageDirectory(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    ensure_java_state();
    return DUMMY_EXTERNAL_STORAGE_FILE;
}

static jobject getExternalStorageState(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    ensure_java_state();
    return g_external_storage_state;
}

static jobject fileCtor(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return DUMMY_EXTERNAL_STORAGE_FILE;
}

static jobject fileToString(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    ensure_java_state();
    return g_external_storage_path;
}

static jboolean fileExists(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return JNI_TRUE;
}

static jobject getID(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return jstr_new("UTC");
}

static jobject getActiveNetworkInfo(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    log_network_once("getActiveNetworkInfo()", "NULL (offline)");
    return NULL;
}

static jobject getSystemService(jmethodID id, va_list args) {
    static int log_count = 0;
    jstring service = va_arg(args, jstring);
    const char *name = jstr_cstr(service);
    jobject ret = DUMMY_SYSTEM_SERVICE;

    (void)id;

    if (name && strcmp(name, "connectivity") == 0) {
        ret = DUMMY_CONNECTIVITY_MANAGER;
    }

    if (log_count < 8) {
        sceClibPrintf("[JNI-NET] getSystemService('%s') -> %s 0x%x\n",
                      name ? name : "(null)",
                      ret == DUMMY_CONNECTIVITY_MANAGER ? "connectivity-manager (offline)" : "service",
                      (unsigned)(uintptr_t)ret);
        log_count++;
    }

    jstr_release(service, name);
    return ret;
}

static jboolean isAvailable(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    log_network_once("NetworkInfo.isAvailable()", "false");
    return JNI_FALSE;
}

static jboolean interfaceIsOpen(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    log_network_once("interfaceIsOpen()", "false (offline)");
    return JNI_FALSE;
}

static void startMethod(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    gunbros_music_start();
}

static void pauseMethod(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    gunbros_music_pause();
}

static void stopMethod(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    gunbros_music_stop();
}

static void seekTo(jmethodID id, va_list args) {
    (void)id;
    gunbros_music_seek_ms(int_arg(args));
}

static jint getCurrentPosition(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return gunbros_music_get_position_ms();
}

static jint nfcEnabled(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    return 0;
}

static void setLooping(jmethodID id, va_list args) {
    (void)id;
    gunbros_music_set_looping(int_arg(args));
}

static void setMediaPlayerVolume(jmethodID id, va_list args) {
    (void)id;
    /* JNI float varargs are promoted to double by the native caller. */
    const float left = (float)va_arg(args, double);
    const float right = (float)va_arg(args, double);
    gunbros_music_set_volume(left, right);
}

static void releaseMethod(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    gunbros_music_release();
}

static jobject optionalObjectStub(jmethodID id, va_list args) {
    (void)args;

    switch ((int)(intptr_t)id) {
        case MID_GluAdMarvel_ctor:
            log_optional_jni_once("GluAdMarvel.<init>");
            return DUMMY_ADMARVEL;
        case MID_HashMap_ctor:
            log_optional_jni_once("HashMap.<init>");
            return DUMMY_HASH_MAP;
        case MID_HashMap_put:
            log_optional_jni_once("HashMap.put");
            return NULL;
        case MID_getContentResolver:
            log_optional_jni_once("getContentResolver");
            return DUMMY_CONTENT_RESOLVER;
        case MID_Settings_getString:
            log_optional_jni_once("Settings.Secure.getString");
            return jstr_new("vita-gunbros-android-id");
        default:
            return NULL;
    }
}

static jboolean optionalBooleanStub(jmethodID id, va_list args) {
    (void)args;

    if ((int)(intptr_t)id == MID_isVisible) {
        log_optional_jni_once("GluAdMarvel.isVisible");
    }

    return JNI_FALSE;
}

static jboolean canDisplayInterface(jmethodID id, va_list args) {
    (void)id;
    (void)args;
    log_optional_jni_once("canDisplayInterface() -> false (Tapjoy disabled)");
    return JNI_FALSE;
}

static jint optionalIntStub(jmethodID id, va_list args) {
    (void)args;

    if ((int)(intptr_t)id == MID_iapEvent) {
        log_optional_jni_once("iapEvent");
    }

    return 0;
}

static void optionalVoidStub(jmethodID id, va_list args) {
    (void)args;

    switch ((int)(intptr_t)id) {
        case MID_showAd:
            log_optional_jni_once("GluAdMarvel.showAd");
            break;
        case MID_hideAd:
            log_optional_jni_once("GluAdMarvel.hideAd");
            break;
        case MID_tick:
            log_optional_jni_once("GluAdMarvel.tick");
            break;
        case MID_facebookEvent:
            log_optional_jni_once("facebookEvent");
            break;
        case MID_onStartSession:
            log_optional_jni_once("onStartSession");
            break;
        case MID_onEvent:
            log_optional_jni_once("onEvent");
            break;
        case MID_onresume:
            log_optional_jni_once("onresume");
            break;
        case MID_queryFeaturedApp:
            log_optional_jni_once("queryFeaturedApp() -> no-op (Tapjoy offline)");
            break;
        default:
            break;
    }
}

NameToMethodID nameToMethodId[] = {
    { MID_SetTickRate, "SetTickRate", METHOD_TYPE_VOID },
    { MID_FinishApp, "FinishApp", METHOD_TYPE_VOID },
    { MID_LaunchURL, "LaunchURL", METHOD_TYPE_BOOLEAN },
    { MID_OpenAPKFile, "OpenAPKFile", METHOD_TYPE_OBJECT },
    { MID_APKExistsDir, "APKExistsDir", METHOD_TYPE_BOOLEAN },
    { MID_GetAndroidMinBufferSize, "GetAndroidMinBufferSize", METHOD_TYPE_INT },
    { MID_InitialiseSoundEngine, "InitialiseSoundEngine", METHOD_TYPE_BOOLEAN },
    { MID_DestroySoundEngine, "DestroySoundEngine", METHOD_TYPE_VOID },
    { MID_InitialiseVibrationEngine, "InitialiseVibrationEngine", METHOD_TYPE_BOOLEAN },
    { MID_DestroyVibrationEngine, "DestroyVibrationEngine", METHOD_TYPE_VOID },
    { MID_IsPSP, "IsPSP", METHOD_TYPE_BOOLEAN },
    { MID_SetVolume, "SetVolume", METHOD_TYPE_VOID },
    { MID_StartAudioStream, "StartAudioStream", METHOD_TYPE_BOOLEAN },
    { MID_StopAudioStream, "StopAudioStream", METHOD_TYPE_VOID },
    { MID_PlayVibration, "PlayVibration", METHOD_TYPE_BOOLEAN },
    { MID_StopVibration, "StopVibration", METHOD_TYPE_BOOLEAN },
    { MID_InitialiseSoundEvent, "InitialiseSoundEvent", METHOD_TYPE_OBJECT },
    { MID_SetDeviceOrientation, "SetDeviceOrientation", METHOD_TYPE_BOOLEAN },
    { MID_GetDeviceOrientation, "GetDeviceOrientation", METHOD_TYPE_INT },
    { MID_StartMovieActivity, "StartMovieActivity", METHOD_TYPE_BOOLEAN },
    { MID_EnumInit, "EnumInit", METHOD_TYPE_BOOLEAN },
    { MID_EnumNext, "EnumNext", METHOD_TYPE_OBJECT },
    { MID_GetDeviceAvailableMemory, "GetDeviceAvailableMemory", METHOD_TYPE_LONG },
    { MID_SystemMessageBox, "SystemMessageBox", METHOD_TYPE_VOID },
    { MID_CreateEGL, "CreateEGL", METHOD_TYPE_OBJECT },
    { MID_SetAutoRotationValues, "SetAutoRotationValues", METHOD_TYPE_VOID },
    { MID_StartAutoRotation, "StartAutoRotation", METHOD_TYPE_BOOLEAN },
    { MID_StopAutoRotation, "StopAutoRotation", METHOD_TYPE_VOID },
    { MID_StartLocalNotificationService, "StartLocalNotificationService", METHOD_TYPE_VOID },
    { MID_AddLocalNotification, "AddLocalNotification", METHOD_TYPE_VOID },
    { MID_RemoveLocalNotification, "RemoveLocalNotification", METHOD_TYPE_VOID },
    { MID_CancelAllLocalNotifications, "CancelAllLocalNotifications", METHOD_TYPE_VOID },
    { MID_EnablePushNotifications, "EnablePushNotifications", METHOD_TYPE_VOID },
    { MID_EnableMultipleTouch, "EnableMultipleTouch", METHOD_TYPE_VOID },
    { MID_IsUserOnWiFi, "IsUserOnWiFi", METHOD_TYPE_BOOLEAN },
    { MID_mp3Event, "mp3Event", METHOD_TYPE_INT },
    { MID_eglGetDisplay, "eglGetDisplay", METHOD_TYPE_OBJECT },
    { MID_eglGetCurrentDisplay, "eglGetCurrentDisplay", METHOD_TYPE_OBJECT },
    { MID_eglInitialize, "eglInitialize", METHOD_TYPE_BOOLEAN },
    { MID_eglSwapBuffers, "eglSwapBuffers", METHOD_TYPE_BOOLEAN },
    { MID_eglCreateContext, "eglCreateContext", METHOD_TYPE_OBJECT },
    { MID_eglDestroyContext, "eglDestroyContext", METHOD_TYPE_BOOLEAN },
    { MID_eglMakeCurrent, "eglMakeCurrent", METHOD_TYPE_BOOLEAN },
    { MID_eglGetCurrentContext, "eglGetCurrentContext", METHOD_TYPE_OBJECT },
    { MID_eglGetConfigs, "eglGetConfigs", METHOD_TYPE_BOOLEAN },
    { MID_eglGetConfigAttrib, "eglGetConfigAttrib", METHOD_TYPE_BOOLEAN },
    { MID_eglChooseConfig, "eglChooseConfig", METHOD_TYPE_BOOLEAN },
    { MID_eglCreateWindowSurface, "eglCreateWindowSurface", METHOD_TYPE_OBJECT },
    { MID_eglCreatePbufferSurface, "eglCreatePbufferSurface", METHOD_TYPE_OBJECT },
    { MID_eglDestroySurface, "eglDestroySurface", METHOD_TYPE_BOOLEAN },
    { MID_eglTerminate, "eglTerminate", METHOD_TYPE_BOOLEAN },
    { MID_eglGetError, "eglGetError", METHOD_TYPE_INT },
    { MID_eglQueryString, "eglQueryString", METHOD_TYPE_OBJECT },
    { MID_getFileDescriptor, "getFileDescriptor", METHOD_TYPE_OBJECT },
    { MID_getLength, "getLength", METHOD_TYPE_LONG },
    { MID_getStartOffset, "getStartOffset", METHOD_TYPE_LONG },
    { MID_close, "close", METHOD_TYPE_VOID },
    { MID_getDefault, "getDefault", METHOD_TYPE_OBJECT },
    { MID_getID, "getID", METHOD_TYPE_OBJECT },
    { MID_getActiveNetworkInfo, "getActiveNetworkInfo", METHOD_TYPE_OBJECT },
    { MID_getSystemService, "getSystemService", METHOD_TYPE_OBJECT },
    { MID_isAvailable, "isAvailable", METHOD_TYPE_BOOLEAN },
    { MID_GluAdMarvel_ctor, "com/glu/android/admarvel/GluAdMarvel/<init>", METHOD_TYPE_OBJECT },
    { MID_GluAdMarvel_ctor, "com/glu/android/admarvel/GluAdMarvel/", METHOD_TYPE_OBJECT },
    { MID_showAd, "showAd", METHOD_TYPE_VOID },
    { MID_hideAd, "hideAd", METHOD_TYPE_VOID },
    { MID_isVisible, "isVisible", METHOD_TYPE_BOOLEAN },
    { MID_tick, "tick", METHOD_TYPE_VOID },
    { MID_iapEvent, "iapEvent", METHOD_TYPE_INT },
    { MID_facebookEvent, "facebookEvent", METHOD_TYPE_VOID },
    { MID_onStartSession, "onStartSession", METHOD_TYPE_VOID },
    { MID_HashMap_ctor, "java/util/HashMap/<init>", METHOD_TYPE_OBJECT },
    { MID_HashMap_ctor, "java/util/HashMap/", METHOD_TYPE_OBJECT },
    { MID_HashMap_put, "put", METHOD_TYPE_OBJECT },
    { MID_getContentResolver, "getContentResolver", METHOD_TYPE_OBJECT },
    { MID_Settings_getString, "getString", METHOD_TYPE_OBJECT },
    { MID_onEvent, "onEvent", METHOD_TYPE_VOID },
    { MID_onresume, "onresume", METHOD_TYPE_VOID },
    { MID_getExternalStorageDirectory, "getExternalStorageDirectory", METHOD_TYPE_OBJECT },
    { MID_getExternalStorageState, "getExternalStorageState", METHOD_TYPE_OBJECT },
    { MID_File_ctor, "java/io/File/<init>", METHOD_TYPE_OBJECT },
    { MID_File_ctor, "java/io/File/", METHOD_TYPE_OBJECT },
    { MID_File_toString, "toString", METHOD_TYPE_OBJECT },
    { MID_File_exists, "exists", METHOD_TYPE_BOOLEAN },
    { MID_canDisplayInterface, "canDisplayInterface", METHOD_TYPE_BOOLEAN },
    { MID_queryFeaturedApp, "queryFeaturedApp", METHOD_TYPE_VOID },
    { MID_interfaceIsOpen, "interfaceIsOpen", METHOD_TYPE_BOOLEAN },
    { MID_start, "start", METHOD_TYPE_VOID },
    { MID_pause, "pause", METHOD_TYPE_VOID },
    { MID_stop, "stop", METHOD_TYPE_VOID },
    { MID_seekTo, "seekTo", METHOD_TYPE_VOID },
    { MID_getCurrentPosition, "getCurrentPosition", METHOD_TYPE_INT },
    { MID_setLooping, "setLooping", METHOD_TYPE_VOID },
    { MID_setVolume, "setVolume", METHOD_TYPE_VOID },
    { MID_release, "release", METHOD_TYPE_VOID },
    { MID_nfcEnabled, "nfcEnabled", METHOD_TYPE_INT },
    { MID_resdlEvent, "resdlEvent", METHOD_TYPE_INT },
    { MID_resdlStringEvent, "resdlStringEvent", METHOD_TYPE_OBJECT },
};

MethodsBoolean methodsBoolean[] = {
    { MID_LaunchURL, LaunchURL },
    { MID_APKExistsDir, APKExistsDir },
    { MID_InitialiseSoundEngine, InitialiseSoundEngine },
    { MID_InitialiseVibrationEngine, InitialiseVibrationEngine },
    { MID_IsPSP, IsPSP },
    { MID_StartAudioStream, StartAudioStream },
    { MID_PlayVibration, PlayVibration },
    { MID_StopVibration, StopVibration },
    { MID_SetDeviceOrientation, SetDeviceOrientation },
    { MID_StartMovieActivity, StartMovieActivity },
    { MID_EnumInit, EnumInit },
    { MID_StartAutoRotation, StartAutoRotation },
    { MID_IsUserOnWiFi, IsUserOnWiFi },
    { MID_eglInitialize, eglInitialize },
    { MID_eglSwapBuffers, eglSwapBuffers },
    { MID_eglDestroyContext, eglDestroyContext },
    { MID_eglMakeCurrent, eglMakeCurrent },
    { MID_eglGetConfigs, eglGetConfigs },
    { MID_eglGetConfigAttrib, eglGetConfigAttrib },
    { MID_eglChooseConfig, eglChooseConfig },
    { MID_eglDestroySurface, eglDestroySurface },
    { MID_eglTerminate, eglTerminate },
    { MID_isAvailable, isAvailable },
    { MID_interfaceIsOpen, interfaceIsOpen },
    { MID_isVisible, optionalBooleanStub },
    { MID_canDisplayInterface, canDisplayInterface },
    { MID_File_exists, fileExists },
};

MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};

MethodsInt methodsInt[] = {
    { MID_GetAndroidMinBufferSize, GetAndroidMinBufferSize },
    { MID_GetDeviceOrientation, GetDeviceOrientation },
    { MID_mp3Event, mp3Event },
    { MID_eglGetError, eglGetError },
    { MID_getCurrentPosition, getCurrentPosition },
    { MID_nfcEnabled, nfcEnabled },
    { MID_resdlEvent, resdlEvent },
    { MID_iapEvent, optionalIntStub },
};

MethodsLong methodsLong[] = {
    { MID_GetDeviceAvailableMemory, GetDeviceAvailableMemory },
    { MID_getLength, getLength },
    { MID_getStartOffset, getStartOffset },
};

MethodsObject methodsObject[] = {
    { MID_OpenAPKFile, OpenAPKFile },
    { MID_InitialiseSoundEvent, InitialiseSoundEvent },
    { MID_EnumNext, EnumNext },
    { MID_CreateEGL, CreateEGL },
    { MID_eglGetDisplay, eglGetDisplay },
    { MID_eglGetCurrentDisplay, eglGetCurrentDisplay },
    { MID_eglCreateContext, eglCreateContext },
    { MID_eglGetCurrentContext, eglGetCurrentContext },
    { MID_eglCreateWindowSurface, eglCreateWindowSurface },
    { MID_eglCreatePbufferSurface, eglCreatePbufferSurface },
    { MID_eglQueryString, eglQueryString },
    { MID_getFileDescriptor, getFileDescriptor },
    { MID_getDefault, getDefault },
    { MID_getID, getID },
    { MID_getActiveNetworkInfo, getActiveNetworkInfo },
    { MID_getSystemService, getSystemService },
    { MID_GluAdMarvel_ctor, optionalObjectStub },
    { MID_HashMap_ctor, optionalObjectStub },
    { MID_HashMap_put, optionalObjectStub },
    { MID_getContentResolver, optionalObjectStub },
    { MID_Settings_getString, optionalObjectStub },
    { MID_resdlStringEvent, resdlStringEvent },
    { MID_getExternalStorageDirectory, getExternalStorageDirectory },
    { MID_getExternalStorageState, getExternalStorageState },
    { MID_File_ctor, fileCtor },
    { MID_File_toString, fileToString },
};

MethodsShort methodsShort[] = {};

MethodsVoid methodsVoid[] = {
    { MID_SetTickRate, SetTickRate },
    { MID_FinishApp, FinishApp },
    { MID_DestroySoundEngine, DestroySoundEngine },
    { MID_DestroyVibrationEngine, DestroyVibrationEngine },
    { MID_SetVolume, SetVolume },
    { MID_StopAudioStream, StopAudioStream },
    { MID_SystemMessageBox, SystemMessageBox },
    { MID_SetAutoRotationValues, SetAutoRotationValues },
    { MID_StopAutoRotation, StopAutoRotation },
    { MID_StartLocalNotificationService, StartLocalNotificationService },
    { MID_AddLocalNotification, AddLocalNotification },
    { MID_RemoveLocalNotification, RemoveLocalNotification },
    { MID_CancelAllLocalNotifications, CancelAllLocalNotifications },
    { MID_EnablePushNotifications, EnablePushNotifications },
    { MID_EnableMultipleTouch, EnableMultipleTouch },
    { MID_close, closeMethod },
    { MID_start, startMethod },
    { MID_pause, pauseMethod },
    { MID_stop, stopMethod },
    { MID_seekTo, seekTo },
    { MID_setLooping, setLooping },
    { MID_setVolume, setMediaPlayerVolume },
    { MID_release, releaseMethod },
    { MID_showAd, optionalVoidStub },
    { MID_hideAd, optionalVoidStub },
    { MID_tick, optionalVoidStub },
    { MID_facebookEvent, optionalVoidStub },
    { MID_onStartSession, optionalVoidStub },
    { MID_onEvent, optionalVoidStub },
    { MID_onresume, optionalVoidStub },
    { MID_queryFeaturedApp, optionalVoidStub },
};

NameToFieldID nameToFieldId[] = {
    { FID_WINDOW_SERVICE, "WINDOW_SERVICE", FIELD_TYPE_OBJECT },
    { FID_INSTANCE, "instance", FIELD_TYPE_OBJECT },
    { FID_INSTANCE, "m_activity", FIELD_TYPE_OBJECT },
    { FID_SDK_INT, "SDK_INT", FIELD_TYPE_INT },
    { FID_MODEL, "MODEL", FIELD_TYPE_OBJECT },
    { FID_EGL_NO_CONTEXT, "EGL_NO_CONTEXT", FIELD_TYPE_OBJECT },
    { FID_EGL_NO_DISPLAY, "EGL_NO_DISPLAY", FIELD_TYPE_OBJECT },
    { FID_EGL_NO_SURFACE, "EGL_NO_SURFACE", FIELD_TYPE_OBJECT },
    { FID_DESCRIPTOR, "descriptor", FIELD_TYPE_INT },
    { FID_ASSET_FD, "mFd", FIELD_TYPE_OBJECT },
    { FID_ASSET_START_OFFSET, "mStartOffset", FIELD_TYPE_LONG },
    { FID_ASSET_LENGTH, "mLength", FIELD_TYPE_LONG },
    { FID_ENUM_FILENAME, "m_Filename", FIELD_TYPE_OBJECT },
    { FID_ENUM_LENGTH, "m_Length", FIELD_TYPE_LONG },
    { FID_MEDIA_MOUNTED, "MEDIA_MOUNTED", FIELD_TYPE_OBJECT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};

FieldsInt fieldsInt[] = {
    { FID_SDK_INT, 19 },
    { FID_DESCRIPTOR, -1 },
};

FieldsObject fieldsObject[] = {
    { FID_WINDOW_SERVICE, NULL },
    { FID_INSTANCE, NULL },
    { FID_MODEL, NULL },
    { FID_EGL_NO_CONTEXT, NULL },
    { FID_EGL_NO_DISPLAY, NULL },
    { FID_EGL_NO_SURFACE, NULL },
    { FID_ASSET_FD, NULL },
    { FID_ENUM_FILENAME, NULL },
    { FID_MEDIA_MOUNTED, NULL },
};

FieldsLong fieldsLong[] = {
    { FID_ASSET_START_OFFSET, 0 },
    { FID_ASSET_LENGTH, 0 },
    { FID_ENUM_LENGTH, 0 },
};

FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
