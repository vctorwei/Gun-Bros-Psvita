#include "utils/data_check.h"
#include "utils/data_paths.h"

#include <psp2/kernel/clib.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define GUNBROS_PACK_TOC_MAX 32
#define GUNBROS_BIG_TOC_MAX_BYTES (4u * 1024u * 1024u)
#define GUNBROS_GAME_OBJECT_TYPE_COUNT 28u
#define GUNBROS_GAME_OBJECT_INDEX_COUNT 34u
#define GUNBROS_GAME_OBJECT_ARMOR_TYPE 2u
#define GUNBROS_GAME_OBJECT_GUN_TYPE 6u
#define GUNBROS_GAME_OBJECT_COUNTS_HASH 0x02719514u
#define GUNBROS_GAME_OBJECT_TOC_HASH 0xd8a9daa5u

static const unsigned char g_big_data_type[4] = { 0x23, 0x22, 0xe0, 0xf4 };
static const unsigned char g_big_keyset_type[4] = { 0x5c, 0xd3, 0xe5, 0x69 };

typedef struct GunBrosPackTOCExpectation {
    bool valid;
    bool game_object_counts_valid;
    bool game_object_indices_valid;
    char pack_name[48];
    uint32_t logical_id;
    uint32_t entry_count;
    uint32_t digest;
    uint8_t game_object_type_count;
    uint8_t game_object_counts[GUNBROS_GAME_OBJECT_TYPE_COUNT];
    uint32_t game_object_indices[GUNBROS_GAME_OBJECT_INDEX_COUNT];
} GunBrosPackTOCExpectation;

static GunBrosPackTOCExpectation g_pack_toc_expectations[GUNBROS_PACK_TOC_MAX];
static unsigned int g_pack_toc_expectation_count;

static uint16_t read_le16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t read_be16(const unsigned char *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint32_t fnv1a32(const unsigned char *data, size_t size) {
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool read_exact(FILE *fp, void *dst, size_t size) {
    return fp && dst && fread(dst, 1, size, fp) == size;
}

static bool find_big_resource_index(FILE *fp,
                                    uint32_t header_size,
                                    uint32_t range_count,
                                    uint32_t resource_count,
                                    uint32_t logical_id,
                                    uint32_t *resource_index) {
    unsigned char range[8];
    uint32_t found = UINT32_MAX;
    uint32_t i;

    if (!fp || !resource_index ||
        fseek(fp, (long)header_size, SEEK_SET) != 0) {
        return false;
    }

    for (i = 0; i < range_count; ++i) {
        uint32_t first_id;
        uint32_t count;
        uint32_t first_index;

        if (!read_exact(fp, range, sizeof(range))) {
            return false;
        }
        first_id = read_le32(range);
        count = read_le16(range + 4);
        first_index = read_le16(range + 6);
        if ((uint64_t)first_index + count > resource_count) {
            return false;
        }
        if (logical_id >= first_id && logical_id - first_id < count) {
            found = first_index + (logical_id - first_id);
        }
    }

    if (found == UINT32_MAX || found >= resource_count) {
        return false;
    }
    *resource_index = found;
    return true;
}

static bool decode_big_wrapped_resource(FILE *fp,
                                        uint32_t file_size,
                                        uint32_t table_start,
                                        uint32_t table_end,
                                        uint32_t resource_count,
                                        uint32_t resource_index,
                                        const unsigned char expected_type[4],
                                        unsigned char **raw_out,
                                        uint32_t *raw_size_out) {
    unsigned char entries[16];
    unsigned char *stored = NULL;
    unsigned char *raw = NULL;
    uint32_t resource_offset;
    uint32_t resource_end;
    uint32_t stored_size;
    uint32_t raw_size;
    uint32_t wrapper;
    bool ok = false;

    if (!fp || !expected_type || !raw_out || !raw_size_out) {
        return false;
    }
    *raw_out = NULL;
    *raw_size_out = 0;
    if (resource_index >= resource_count ||
        fseek(fp, (long)(table_start + resource_index * 8u), SEEK_SET) != 0 ||
        !read_exact(fp, entries, sizeof(entries)) ||
        memcmp(entries, expected_type, 4u) != 0) {
        return false;
    }

    resource_offset = read_le32(entries + 4);
    resource_end = read_le32(entries + 12);
    if (resource_offset < table_end || resource_end <= resource_offset ||
        resource_end > file_size) {
        return false;
    }

    stored_size = resource_end - resource_offset;
    if (stored_size < 4u || stored_size > GUNBROS_BIG_TOC_MAX_BYTES ||
        fseek(fp, (long)resource_offset, SEEK_SET) != 0) {
        return false;
    }
    stored = (unsigned char *)malloc(stored_size);
    if (!stored || !read_exact(fp, stored, stored_size)) {
        goto cleanup;
    }

    wrapper = read_le32(stored);
    if (wrapper == 0x00000004u) {
        raw_size = stored_size - 4u;
        if (raw_size == 0u) {
            goto cleanup;
        }
        raw = (unsigned char *)malloc(raw_size);
        if (!raw) {
            goto cleanup;
        }
        memcpy(raw, stored + 4, raw_size);
    } else if (wrapper == 0x00800004u && stored_size >= 12u) {
        uLongf inflated_size;
        uint32_t compressed_size = read_le32(stored + 8);

        raw_size = read_le32(stored + 4);
        if (raw_size == 0u || raw_size > GUNBROS_BIG_TOC_MAX_BYTES ||
            compressed_size != stored_size - 12u) {
            goto cleanup;
        }
        raw = (unsigned char *)malloc(raw_size);
        if (!raw) {
            goto cleanup;
        }
        inflated_size = raw_size;
        if (uncompress(raw, &inflated_size, stored + 12, compressed_size) != Z_OK ||
            inflated_size != raw_size) {
            goto cleanup;
        }
    } else {
        goto cleanup;
    }

    *raw_out = raw;
    *raw_size_out = raw_size;
    raw = NULL;
    ok = true;

cleanup:
    free(raw);
    free(stored);
    return ok;
}

static bool audit_big_pack_toc(const char *pack_name,
                               uint32_t logical_id,
                               GunBrosPackTOCExpectation *out) {
    unsigned char header[0x20];
    unsigned char *raw = NULL;
    unsigned char *counts_raw = NULL;
    unsigned char *indices_raw = NULL;
    char path[512];
    struct stat st;
    FILE *fp = NULL;
    uint32_t header_size;
    uint32_t range_count;
    uint32_t table_start;
    uint32_t resource_count;
    uint32_t table_end;
    uint32_t content_size;
    uint32_t resource_index = UINT32_MAX;
    uint32_t raw_size;
    uint32_t toc_count;
    uint32_t counts_handle = 0;
    uint32_t counts_logical_id;
    uint32_t counts_resource_index = UINT32_MAX;
    uint32_t counts_raw_size = 0;
    uint32_t indices_handle = 0;
    uint32_t indices_logical_id;
    uint32_t indices_resource_index = UINT32_MAX;
    uint32_t indices_raw_size = 0;
    uint32_t keyset_count = 0;
    uint32_t i;
    bool ok = false;

    if (!pack_name || !out ||
        !gunbros_resolve_data_path_ex(path, sizeof(path), pack_name, false,
                                      &st, NULL) ||
        st.st_size < (off_t)sizeof(header)) {
        sceClibPrintf("[DATA][BIG][FAIL] %s logical=0x%08x missing/short\n",
                      pack_name ? pack_name : "(null)", logical_id);
        return false;
    }

    fp = fopen(path, "rb");
    if (!fp || !read_exact(fp, header, sizeof(header)) ||
        memcmp(header, "FGIB", 4) != 0) {
        sceClibPrintf("[DATA][BIG][FAIL] %s cannot read FGIB header\n", pack_name);
        goto cleanup;
    }

    header_size = read_le32(header + 0x08);
    range_count = read_le32(header + 0x0c);
    table_start = read_le32(header + 0x10);
    resource_count = read_le32(header + 0x14);
    table_end = read_le32(header + 0x18);
    content_size = read_le32(header + 0x1c);

    if (header_size < sizeof(header) ||
        range_count > 0x1000u ||
        resource_count == 0 || resource_count > 0x100000u ||
        (uint64_t)header_size + (uint64_t)range_count * 8u != table_start ||
        (uint64_t)table_start + ((uint64_t)resource_count + 1u) * 8u != table_end ||
        (uint64_t)table_end + content_size != (uint64_t)st.st_size ||
        st.st_size > UINT32_MAX) {
        sceClibPrintf("[DATA][BIG][FAIL] %s inconsistent FGIB layout\n", pack_name);
        goto cleanup;
    }

    if (!find_big_resource_index(fp, header_size, range_count, resource_count,
                                 logical_id, &resource_index) ||
        !decode_big_wrapped_resource(fp, (uint32_t)st.st_size,
                                     table_start, table_end, resource_count,
                                     resource_index, g_big_data_type,
                                     &raw, &raw_size)) {
        sceClibPrintf("[DATA][BIG][FAIL] %s logical=0x%08x has no data entry\n",
                      pack_name, logical_id);
        goto cleanup;
    }

    if (raw_size < 4u) {
        goto cleanup;
    }
    toc_count = read_le32(raw);
    if (toc_count == 0u || toc_count > 0x10000u ||
        (uint64_t)4u + (uint64_t)toc_count * 8u != raw_size) {
        sceClibPrintf("[DATA][BIG][FAIL] %s logical=0x%08x bad TOC shape raw=%u count=%u\n",
                      pack_name, logical_id, raw_size, toc_count);
        goto cleanup;
    }

    if (pack_name != out->pack_name) {
        snprintf(out->pack_name, sizeof(out->pack_name), "%s", pack_name);
    }
    out->logical_id = logical_id;
    out->entry_count = toc_count;
    out->digest = fnv1a32(raw + 4, raw_size - 4u);
    out->valid = true;
    for (i = 0; i < toc_count; ++i) {
        const unsigned char *entry = raw + 4u + i * 8u;

        if (read_le32(entry) == GUNBROS_GAME_OBJECT_COUNTS_HASH) {
            counts_handle = read_le32(entry + 4);
        } else if (read_le32(entry) == GUNBROS_GAME_OBJECT_TOC_HASH) {
            indices_handle = read_le32(entry + 4);
        }
    }
    counts_logical_id = counts_handle & 0x00ffffffu;
    if ((counts_handle >> 24) == 0x03u &&
        find_big_resource_index(fp, header_size, range_count, resource_count,
                                counts_logical_id, &counts_resource_index) &&
        decode_big_wrapped_resource(fp, (uint32_t)st.st_size,
                                    table_start, table_end, resource_count,
                                    counts_resource_index, g_big_data_type,
                                    &counts_raw, &counts_raw_size) &&
        counts_raw_size == GUNBROS_GAME_OBJECT_TYPE_COUNT + 1u &&
        counts_raw[0] == GUNBROS_GAME_OBJECT_TYPE_COUNT) {
        out->game_object_type_count = counts_raw[0];
        memcpy(out->game_object_counts, counts_raw + 1,
               GUNBROS_GAME_OBJECT_TYPE_COUNT);
        out->game_object_counts_valid = true;
    }

    indices_logical_id = indices_handle & 0x00ffffffu;
    if ((indices_handle >> 24) == 0x05u &&
        find_big_resource_index(fp, header_size, range_count, resource_count,
                                indices_logical_id, &indices_resource_index) &&
        decode_big_wrapped_resource(fp, (uint32_t)st.st_size,
                                    table_start, table_end, resource_count,
                                    indices_resource_index, g_big_keyset_type,
                                    &indices_raw, &indices_raw_size) &&
        indices_raw_size >= 2u) {
        keyset_count = read_le16(indices_raw);
        if (keyset_count >= GUNBROS_GAME_OBJECT_INDEX_COUNT &&
            (uint64_t)2u + (uint64_t)keyset_count * 4u == indices_raw_size) {
            for (i = 0; i < GUNBROS_GAME_OBJECT_INDEX_COUNT; ++i) {
                out->game_object_indices[i] =
                    read_le32(indices_raw + 2u + i * 4u);
            }
            out->game_object_indices_valid = true;
        }
    }

    if (out->game_object_counts_valid && out->game_object_indices_valid) {
        sceClibPrintf("[DATA][GOBJ][OK] %s counts=0x%04x toc=0x%04x types=%u indices=%u guns=%u armor=%u\n",
                      pack_name, counts_logical_id, indices_logical_id,
                      (unsigned int)out->game_object_type_count,
                      GUNBROS_GAME_OBJECT_INDEX_COUNT,
                      (unsigned int)out->game_object_counts[GUNBROS_GAME_OBJECT_GUN_TYPE],
                      (unsigned int)out->game_object_counts[GUNBROS_GAME_OBJECT_ARMOR_TYPE]);
    } else {
        sceClibPrintf("[DATA][GOBJ][FAIL] %s counts=0x%08x/raw=%u toc=0x%08x/raw=%u/keys=%u\n",
                      pack_name, counts_handle, counts_raw_size,
                      indices_handle, indices_raw_size, keyset_count);
    }

    sceClibPrintf("[DATA][BIG][OK] %s logical=0x%04x outer=%u entries=%u digest=%08x\n",
                  pack_name, logical_id, resource_index, toc_count, out->digest);
    ok = true;

cleanup:
    if (fp) {
        fclose(fp);
    }
    free(indices_raw);
    free(counts_raw);
    free(raw);
    return ok;
}

static void gunbros_audit_pack_tables(void) {
    unsigned char size_bytes[2];
    unsigned char id_bytes[4];
    char toc_path[512];
    struct stat st;
    FILE *fp;
    unsigned int ok_count = 0;
    unsigned int bad_count = 0;
    unsigned int game_object_count = 0;
    unsigned int total_guns = 0;
    unsigned int total_armor = 0;
    unsigned int i;

    memset(g_pack_toc_expectations, 0, sizeof(g_pack_toc_expectations));
    g_pack_toc_expectation_count = 0;

    if (!gunbros_resolve_data_path_ex(toc_path, sizeof(toc_path),
                                      "packTOC_wvga.dat", false, &st, NULL)) {
        sceClibPrintf("[DATA][TOC][FAIL] packTOC_wvga.dat missing\n");
        return;
    }
    fp = fopen(toc_path, "rb");
    if (!fp) {
        sceClibPrintf("[DATA][TOC][FAIL] cannot open %s\n", toc_path);
        return;
    }

    while (g_pack_toc_expectation_count < GUNBROS_PACK_TOC_MAX &&
           read_exact(fp, size_bytes, sizeof(size_bytes))) {
        GunBrosPackTOCExpectation *expectation =
            &g_pack_toc_expectations[g_pack_toc_expectation_count];
        char key[96];
        char *suffix;
        size_t base_len;
        uint16_t key_size = read_be16(size_bytes);
        uint32_t logical_id;

        if (key_size == 0u || key_size >= sizeof(key) ||
            !read_exact(fp, key, key_size) ||
            !read_exact(fp, id_bytes, sizeof(id_bytes))) {
            ++bad_count;
            break;
        }
        key[key_size] = '\0';
        logical_id = read_be32(id_bytes);
        suffix = strstr(key, ":TABLEOFCONTENTS");
        if (!suffix || suffix[sizeof(":TABLEOFCONTENTS") - 1] != '\0') {
            sceClibPrintf("[DATA][TOC][FAIL] bad key '%s'\n", key);
            ++bad_count;
            ++g_pack_toc_expectation_count;
            continue;
        }
        base_len = (size_t)(suffix - key);
        if (base_len + sizeof(".big") > sizeof(expectation->pack_name)) {
            ++bad_count;
            ++g_pack_toc_expectation_count;
            continue;
        }
        memcpy(expectation->pack_name, key, base_len);
        memcpy(expectation->pack_name + base_len, ".big", sizeof(".big"));
        expectation->logical_id = logical_id;

        if (audit_big_pack_toc(expectation->pack_name, logical_id, expectation)) {
            ++ok_count;
        } else {
            ++bad_count;
        }
        ++g_pack_toc_expectation_count;
    }
    fclose(fp);

    for (i = 0; i < g_pack_toc_expectation_count; ++i) {
        const GunBrosPackTOCExpectation *expectation =
            &g_pack_toc_expectations[i];

        if (!expectation->game_object_counts_valid ||
            !expectation->game_object_indices_valid) {
            continue;
        }
        ++game_object_count;
        total_guns += expectation->game_object_counts[GUNBROS_GAME_OBJECT_GUN_TYPE];
        total_armor += expectation->game_object_counts[GUNBROS_GAME_OBJECT_ARMOR_TYPE];
    }

    sceClibPrintf("[DATA][TOC][SUM] packs=%u ok=%u bad=%u bytes=%lld\n",
                  g_pack_toc_expectation_count, ok_count, bad_count,
                  (long long)st.st_size);
    sceClibPrintf("[DATA][GOBJ][SUM] packs=%u/%u guns=%u armor=%u\n",
                  game_object_count, g_pack_toc_expectation_count,
                  total_guns, total_armor);
}

unsigned int gunbros_get_pack_toc_expectation_count(void) {
    return g_pack_toc_expectation_count;
}

bool gunbros_get_pack_toc_expectation(unsigned short pack_index,
                                      unsigned int *entry_count,
                                      unsigned int *digest,
                                      const char **pack_name) {
    const GunBrosPackTOCExpectation *expectation;

    if (pack_index >= g_pack_toc_expectation_count) {
        return false;
    }
    expectation = &g_pack_toc_expectations[pack_index];
    if (!expectation->valid) {
        return false;
    }
    if (entry_count) {
        *entry_count = expectation->entry_count;
    }
    if (digest) {
        *digest = expectation->digest;
    }
    if (pack_name) {
        *pack_name = expectation->pack_name;
    }
    return true;
}

bool gunbros_get_pack_game_object_tables(unsigned short pack_index,
                                         const unsigned char **type_counts,
                                         unsigned int *type_count,
                                         const unsigned int **resource_indices,
                                         unsigned int *resource_index_count) {
    const GunBrosPackTOCExpectation *expectation;

    if (pack_index >= g_pack_toc_expectation_count) {
        return false;
    }
    expectation = &g_pack_toc_expectations[pack_index];
    if (!expectation->valid || !expectation->game_object_counts_valid ||
        !expectation->game_object_indices_valid) {
        return false;
    }
    if (type_counts) {
        *type_counts = expectation->game_object_counts;
    }
    if (type_count) {
        *type_count = expectation->game_object_type_count;
    }
    if (resource_indices) {
        *resource_indices = expectation->game_object_indices;
    }
    if (resource_index_count) {
        *resource_index_count = GUNBROS_GAME_OBJECT_INDEX_COUNT;
    }
    return true;
}

typedef struct GunBrosRequiredFile {
    const char *name;
    int free_350_size;
    int samsung_314_size;
} GunBrosRequiredFile;

static const GunBrosRequiredFile g_wvga_required_files[] = {
    { "Gman_intro.3gp", 1160768, 1160768 },
    { "events.dat", 95469, 95469 },
    { "pack0_core_wvga.big", 22866326, 25949529 },
    { "pack10_wvga.big", 7960597, 7960856 },
    { "pack11_wvga.big", 11252874, 11254481 },
    { "pack12_wvga.big", 14311867, 14312844 },
    { "pack1_wvga.big", 18690843, 18696803 },
    { "pack2_wvga.big", 4805622, 4808549 },
    { "pack3_wvga.big", 1387158, 1256293 },
    { "pack4_wvga.big", 44515959, 40085154 },
    { "pack5_wvga.big", 29173956, 26151988 },
    { "pack6_wvga.big", 6259674, 6259866 },
    { "pack7_wvga.big", 8255839, 8261427 },
    { "pack8_wvga.big", 11433204, 11433440 },
    { "pack9_wvga.big", 6848753, 6854363 },
    { "packTOC_wvga.dat", 424, 424 },
    { "1.mp3", 2224065, 2224065 },
    { "2.mp3", 1788656, 1788656 },
    { "3.mp3", 1852290, 1852290 },
    { "4.mp3", 1516878, 1516878 },
    { "5.mp3", 1870471, 1870471 },
    { "6.mp3", 1758249, 1758249 },
    { "game_0.mp3", 2095488, 2095488 },
};

static const char *profile_name_for_size(const GunBrosRequiredFile *file, int actual_size) {
    if (actual_size == file->free_350_size) {
        return "free_350";
    }
    if (actual_size == file->samsung_314_size) {
        return "samsung_314";
    }
    return NULL;
}

static bool locate_data_file(const char *name, char *out_path, size_t out_path_size,
                             struct stat *out_st, bool *found_in_assets,
                             GunBrosDataLocation *out_location) {
    GunBrosDataLocation location = GUNBROS_DATA_LOCATION_MISSING;
    bool found = gunbros_resolve_data_path_ex(out_path, out_path_size, name, false, out_st, &location);

    if (found_in_assets) {
        *found_in_assets = (location == GUNBROS_DATA_LOCATION_ASSETS);
    }
    if (out_location) {
        *out_location = location;
    }

    return found;
}

void gunbros_print_data_check(void) {
    int ok_count = 0;
    int wrong_size_count = 0;
    int missing_count = 0;
    int alt_layout_count = 0;
    int free_350_matches = 0;
    int samsung_314_matches = 0;
    size_t i;

    sceClibPrintf("=== Gun Bros Data Check ===\n");
    sceClibPrintf("Checking required WVGA files under %s\n", DATA_PATH);

    for (i = 0; i < sizeof(g_wvga_required_files) / sizeof(g_wvga_required_files[0]); ++i) {
        char path[512];
        struct stat st;
        bool found_in_assets;
        GunBrosDataLocation location;
        bool found = locate_data_file(g_wvga_required_files[i].name, path, sizeof(path), &st,
                                      &found_in_assets, &location);

        if (!found) {
            ++missing_count;
            sceClibPrintf("[DATA][MISS] %s\n", g_wvga_required_files[i].name);
            continue;
        }

        if (!found_in_assets) {
            ++alt_layout_count;
            sceClibPrintf("[DATA][ALT ] %s -> %s\n", g_wvga_required_files[i].name, path);
        }

        {
            const char *profile = profile_name_for_size(&g_wvga_required_files[i], (int)st.st_size);

            if (profile) {
                if (strcmp(profile, "free_350") == 0) {
                    ++free_350_matches;
                } else if (strcmp(profile, "samsung_314") == 0) {
                    ++samsung_314_matches;
                }

                ++ok_count;
                sceClibPrintf("[DATA][OK]   %s -> %lld bytes%s [%s]\n",
                              g_wvga_required_files[i].name,
                              (long long)st.st_size,
                              found_in_assets ? "" : (location == GUNBROS_DATA_LOCATION_FILES ? " [files]" : " [root]"),
                              profile);
                continue;
            }
        }

        {
            ++wrong_size_count;
            sceClibPrintf("[DATA][SIZE] %s -> %lld bytes, known profiles: free_350=%d samsung_314=%d\n",
                          g_wvga_required_files[i].name,
                          (long long)st.st_size,
                          g_wvga_required_files[i].free_350_size,
                          g_wvga_required_files[i].samsung_314_size);
        }
    }

    sceClibPrintf("[DATA][SUM] ok=%d missing=%d wrong_size=%d alt_layout=%d total=%d\n",
                  ok_count,
                  missing_count,
                  wrong_size_count,
                  alt_layout_count,
                  (int)(sizeof(g_wvga_required_files) / sizeof(g_wvga_required_files[0])));

    if (free_350_matches || samsung_314_matches) {
        sceClibPrintf("[DATA][PROFILE] best match: %s (free_350=%d samsung_314=%d)\n",
                      samsung_314_matches > free_350_matches ? "samsung_314" : "free_350",
                      free_350_matches, samsung_314_matches);
    }

    gunbros_audit_pack_tables();

    if (missing_count || wrong_size_count) {
        sceClibPrintf("[DATA][HINT] Missing files usually crash during OpenAPKFile or pack loading.\n");
        sceClibPrintf("[DATA][HINT] Supported layouts: %sassets/<file>, %sfiles/<file>, or %s<file>\n",
                      DATA_PATH, DATA_PATH, DATA_PATH);
        sceClibPrintf("[DATA][HINT] Size mismatches now mean unknown pack profile, not a path problem.\n");
    }
}
