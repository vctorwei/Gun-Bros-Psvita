#ifndef GUNBROS_DATA_PATHS_H
#define GUNBROS_DATA_PATHS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

typedef enum GunBrosDataLocation {
    GUNBROS_DATA_LOCATION_DIRECT = 0,
    GUNBROS_DATA_LOCATION_ASSETS,
    GUNBROS_DATA_LOCATION_FILES,
    GUNBROS_DATA_LOCATION_ROOT,
    GUNBROS_DATA_LOCATION_MISSING,
} GunBrosDataLocation;

static inline bool gunbros_path_exists(const char *path, bool want_dir, struct stat *out_st) {
    struct stat st;

    if (!path || stat(path, &st) < 0) {
        return false;
    }

    if (want_dir) {
        if (!S_ISDIR(st.st_mode)) {
            return false;
        }
    } else if (!S_ISREG(st.st_mode)) {
        return false;
    }

    if (out_st) {
        *out_st = st;
    }

    return true;
}

static inline void gunbros_format_data_path(char *dst, size_t dst_size,
                                            const char *prefix, const char *name) {
    if (!dst || dst_size == 0) {
        return;
    }

    if (!prefix) {
        prefix = "";
    }

    if (name && name[0] != '\0') {
        snprintf(dst, dst_size, DATA_PATH "%s%s", prefix, name);
    } else {
        snprintf(dst, dst_size, DATA_PATH "%s", prefix);
        if (dst[0] != '\0' && dst[strlen(dst) - 1] == '/') {
            dst[strlen(dst) - 1] = '\0';
        }
    }
}

static inline bool gunbros_resolve_data_path_ex(char *dst, size_t dst_size, const char *name,
                                                bool want_dir, struct stat *out_st,
                                                GunBrosDataLocation *out_location) {
    struct stat st;
    char candidate[512];
    GunBrosDataLocation location = GUNBROS_DATA_LOCATION_MISSING;
    const struct {
        const char *prefix;
        GunBrosDataLocation location;
    } layouts[] = {
        { "assets/", GUNBROS_DATA_LOCATION_ASSETS },
        { "files/",  GUNBROS_DATA_LOCATION_FILES  },
        { "",        GUNBROS_DATA_LOCATION_ROOT   },
    };
    size_t i;

    if (name && gunbros_path_exists(name, want_dir, &st)) {
        if (dst) {
            snprintf(dst, dst_size, "%s", name);
        }
        if (out_st) {
            *out_st = st;
        }
        if (out_location) {
            *out_location = GUNBROS_DATA_LOCATION_DIRECT;
        }
        return true;
    }

    for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); ++i) {
        gunbros_format_data_path(candidate, sizeof(candidate), layouts[i].prefix, name);
        if (gunbros_path_exists(candidate, want_dir, &st)) {
            if (dst) {
                snprintf(dst, dst_size, "%s", candidate);
            }
            if (out_st) {
                *out_st = st;
            }
            if (out_location) {
                *out_location = layouts[i].location;
            }
            return true;
        }
    }

    gunbros_format_data_path(candidate, sizeof(candidate), "assets/", name);
    if (dst) {
        snprintf(dst, dst_size, "%s", candidate);
    }
    if (out_st) {
        memset(out_st, 0, sizeof(*out_st));
    }
    if (out_location) {
        *out_location = location;
    }
    return false;
}

static inline bool gunbros_resolve_data_path(char *dst, size_t dst_size, const char *name,
                                             bool want_dir) {
    return gunbros_resolve_data_path_ex(dst, dst_size, name, want_dir, NULL, NULL);
}

static inline const char *gunbros_data_location_name(GunBrosDataLocation location) {
    switch (location) {
        case GUNBROS_DATA_LOCATION_DIRECT:
            return "direct";
        case GUNBROS_DATA_LOCATION_ASSETS:
            return "assets";
        case GUNBROS_DATA_LOCATION_FILES:
            return "files";
        case GUNBROS_DATA_LOCATION_ROOT:
            return "root";
        default:
            return "missing";
    }
}

static inline const char *gunbros_get_preferred_resource_base_path(void) {
    static char path[512];
    static bool initialized;
    struct stat st;
    if (initialized) {
        return path;
    }

    snprintf(path, sizeof(path), DATA_PATH "files/file.big");
    if (gunbros_path_exists(path, false, &st)) {
        snprintf(path, sizeof(path), DATA_PATH "files/");
        initialized = true;
        return path;
    }

    snprintf(path, sizeof(path), DATA_PATH "assets/");
    if (gunbros_path_exists(path, true, &st)) {
        initialized = true;
        return path;
    }

    snprintf(path, sizeof(path), DATA_PATH "files/");
    if (gunbros_path_exists(path, true, &st)) {
        initialized = true;
        return path;
    }

    snprintf(path, sizeof(path), DATA_PATH);
    if (gunbros_path_exists(path, true, &st)) {
        initialized = true;
        return path;
    }

    snprintf(path, sizeof(path), DATA_PATH);
    initialized = true;
    return path;
}

#endif
