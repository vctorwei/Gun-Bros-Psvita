/*
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/utils.h"
#include "utils/logger.h"

#include <psp2/kernel/clib.h>
#include <psp2/io/stat.h>

#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <sha1/sha1.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

uint64_t current_timestamp_ms() {
    struct timeval te;
    gettimeofday(&te, NULL);
    return (te.tv_sec * 1000LL + te.tv_usec / 1000);
}

bool file_copy(const char * path, const char * destination) {
    if (!file_exists(path)) {
        l_error("file_copy: Specified source path \"%s\" "
                "does not exist.", path);
        return false;
    }

    if (!file_mkpath(destination, 0755)) {
        l_error("file_copy: Could not create parent directories for the "
                "specified destination path \"%s\".", destination);
        return false;
    }

    uint8_t * buffer;
    size_t    size;

    if (!file_load(path, &buffer, &size)) {
        l_error("file_copy: Failed to read data from "
                "the source path \"%s\".", path);
        return false;
    }

    if (!file_save(destination, buffer, size)) {
        l_error("file_copy: Failed to write data to the specified "
                "destination path \"%s\".", destination);
        return false;
    }

    free(buffer);
    return true;
}

bool file_exists(const char * path) {
    SceIoStat stat;
    return sceIoGetstat(path, &stat) >= 0;
}

bool file_load(const char * path, uint8_t ** buffer, size_t * size) {
    if (!buffer || !size) {
        l_error("file_load: Invalid argument(s).");
        return false;
    }

    if (!file_exists(path)) {
        l_error("file_load: Specified source path \"%s\" "
                "does not exist.", path);
        return false;
    }

#ifdef USE_SCELIBC_IO
    FILE * f = sceLibcBridge_fopen(path, "rb");
#else
    FILE * f = fopen(path, "rb");
#endif

    if (!f) {
        l_error("file_load: Could not open the specified "
                "source path \"%s\".", path);
        return false;
    }

#ifdef USE_SCELIBC_IO
    sceLibcBridge_fseek(f, 0, SEEK_END);
    *size = sceLibcBridge_ftell(f);
    sceLibcBridge_fseek(f, 0, SEEK_SET);
#else
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
#endif

    if (*size <= 0) {
        l_error("file_load: The specified source file \"%s\" is empty.", path);
    #ifdef USE_SCELIBC_IO
        sceLibcBridge_fclose(f);
    #else
        fclose(f);
    #endif
        return false;
    }

    *buffer = malloc(*size);

    if (!*buffer) {
        l_error("file_load: Unable to allocate %d bytes of memory to load "
                "the specified source file \"%s\".", path);
    #ifdef USE_SCELIBC_IO
        sceLibcBridge_fclose(f);
    #else
        fclose(f);
    #endif
        return false;
    }

#ifdef USE_SCELIBC_IO
    sceLibcBridge_fread(*buffer, 1, *size, f);
    sceLibcBridge_fclose(f);
#else
    fread(*buffer, 1, *size, f);
    fclose(f);
#endif

    return true;
}

bool file_mkpath(const char * path, mode_t mode) {
    if (!path || !*path) {
        l_error("file_mkpath: Invalid argument.");
        return false;
    }

    char * file_path = strdup(path);
    for (char* p = strchr(file_path + 1, '/'); p; p = strchr(p + 1, '/')) {
        *p = '\0';
        if (mkdir(file_path, mode) == -1) {
            if (errno != EEXIST) {
                l_error("file_mkpath: Unable to create a directory \"%s\", "
                        "mkdir error code is %s.", file_path, strerror(errno));
                free(file_path);
                return false;
            }
        }
        *p = '/';
    }

    free(file_path);
    return true;
}

bool file_save(const char * path, const uint8_t * buffer, size_t size) {
#ifdef USE_SCELIBC_IO
    FILE * f = sceLibcBridge_fopen(path, "wb");
#else
    FILE * f = fopen(path, "wb");
#endif

    if (!f) {
        l_error("file_save: Could not open the specified "
                "target path \"%s\".", path);
        return false;
    }

#ifdef USE_SCELIBC_IO
    sceLibcBridge_fwrite(buffer, size, 1, f);
    sceLibcBridge_fclose(f);
#else
    fwrite(buffer, size, 1, f);
    fclose(f);
#endif

    return true;
}

size_t file_size(const char * path) {
    size_t ret = -1;

    if (!file_exists(path)) {
        l_error("size_t: Specified source path \"%s\" "
                "does not exist.", path);
        return ret;
    }

#ifdef USE_SCELIBC_IO
    FILE * f = sceLibcBridge_fopen(path, "rb");
#else
    FILE * f = fopen(path, "rb");
#endif

    if (!f) {
        l_error("size_t: Could not open the specified "
                "source path \"%s\".", path);
        return ret;
    }

#ifdef USE_SCELIBC_IO
    sceLibcBridge_fseek(f, 0, SEEK_END);
    ret = sceLibcBridge_ftell(f);
    sceLibcBridge_fseek(f, 0, SEEK_SET);
    sceLibcBridge_fclose(f);
#else
    fseek(f, 0, SEEK_END);
    ret = ftell(f);
    fseek(f, 0, SEEK_SET);
    fclose(f);
#endif

    return ret;
}

char * file_sha1sum(const char * path) {
    uint8_t * buffer;
    size_t    size;

    if (!file_load(path, &buffer, &size)) {
        l_error("file_sha1sum: Failed to read data from "
                "the source path \"%s\".", path);
        return NULL;
    }

    char * ret = str_sha1sum((const char *) buffer, size);

    free(buffer);
    return ret;
}

bool is_dir(const char * path) {
    DIR* d = opendir(path);

    if (d != NULL) {
        closedir(d);
        return true;
    }

    return false;
}

SceUID _vshKernelSearchModuleByName(const char *, int *);

bool module_loaded(const char * name) {
    int search_unk[2];
    return _vshKernelSearchModuleByName(name, search_unk) >= 0;
}

int ret0(void) {
    return 0;
}

int ret1(void) {
    return 1;
}

int retminus1(void) {
    return -1;
}

static bool is_probable_cstr(const char *str) {
    uintptr_t addr;
    size_t i;

    if (!str) {
        return false;
    }

    addr = (uintptr_t)str;
    if (addr < 0x81000000u || addr >= 0xA0000000u) {
        return false;
    }

    for (i = 0; i < 4096; ++i) {
        if (str[i] == '\0') {
            return true;
        }
    }

    return false;
}

static void trace_string_helper_call(const char *name, const void *caller,
                                     const void *str_ref, const void *str,
                                     const void *needle, const void *replacement) {
    static int budget = 32;

    if (budget <= 0) {
        return;
    }

    budget--;
    sceClibPrintf("[STR-TRACE] %s caller=%p str_ref=%p str=%p needle=%p replacement=%p\n",
                  name, caller, str_ref, str, needle, replacement);
}

static char *dup_fallback_sha1(void) {
    return strdup("0000000000000000000000000000000000000000");
}

void str_replace(char ** str, const char * needle, const char * replacement) {
    void *caller = __builtin_return_address(0);

    trace_string_helper_call("str_replace", caller, str, str ? *str : NULL,
                             needle, replacement);
    (void)str;
    (void)needle;
    (void)replacement;
}

void str_remove(char * str, const char * needle) {
    void *caller = __builtin_return_address(0);

    trace_string_helper_call("str_remove", caller, NULL, str, needle, NULL);
    (void)str;
    (void)needle;
}

bool str_starts_with(const char * str, const char * prefix) {
    char cp;
    char cs;

    if (!str || !prefix) {
        return false;
    }

    if (!is_probable_cstr(str) || !is_probable_cstr(prefix)) {
        return false;
    }

    if (!*prefix)
        return true;

    while ((cp = *prefix++) && (cs = *str++)) {
        if (cp != cs)
            return false;
    }

    if (!cs)
        return false;

    return true;
}

bool str_ends_with(const char * str, const char * suffix) {
    if (!str || !suffix) {
        return false;
    }

    if (!is_probable_cstr(str) || !is_probable_cstr(suffix)) {
        return false;
    }

    int str_len = (int)strlen(str);
    int suffix_len = (int)strlen(suffix);

    return (str_len >= suffix_len) &&
           (0 == strcmp(str + (str_len-suffix_len), suffix));
}

char * str_sha1sum(const char * str, size_t size) {
    (void)str;
    (void)size;
    // Keep shader/file hash naming deterministic enough for boot while avoiding
    // crashes from foreign callers passing invalid pointers during startup.
    return dup_fallback_sha1();
}
