/* Read only the requested image from the installed Classic core BIG. */
#ifndef GUNBROS_LAUNCHER_BIG_H
#define GUNBROS_LAUNCHER_BIG_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vita2d.h>
#include <stdlib.h>
#include <zlib.h>

static uint32_t big_u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint32_t png_u32(const unsigned char *p) {
    return (uint32_t)p[3] | (uint32_t)p[2]<<8 | (uint32_t)p[1]<<16 | (uint32_t)p[0]<<24;
}
static int big_valid_png(const unsigned char *p, size_t size, unsigned w, unsigned h) {
    if (size < 45 || memcmp(p, "\x89PNG\r\n\x1a\n", 8) ||
        png_u32(p+8) != 13 || memcmp(p+12, "IHDR", 4) ||
        png_u32(p+16) != w || png_u32(p+20) != h) return 0;
    size_t at=8;
    while (size-at >= 12) {
        uint32_t n=png_u32(p+at);
        if (n > size-at-12) return 0;
        if (crc32(0,p+at+4,n+4) != png_u32(p+at+8+n)) return 0;
        if (!memcmp(p+at+4,"IEND",4)) return n == 0 && at+12 == size;
        at += n+12;
    }
    return 0;
}
static unsigned char *big_png(FILE *f, unsigned id, unsigned w, unsigned h, size_t *length) {
    unsigned char header[32],entry[16],*encoded=NULL,*decoded=NULL;
    const uint32_t limit=8*1024*1024;
    if (fseek(f,0,SEEK_END)) return NULL;
    long size=ftell(f);
    if (size < 32 || fseek(f,0,SEEK_SET) || fread(header,1,32,f)!=32 ||
        memcmp(header,"FGIB\x01\x00\x80\x00",8)) return NULL;
    uint32_t table=big_u32(header+16),count=big_u32(header+20),endtable=big_u32(header+24);
    if (table<32 || id>=count || (uint64_t)table+(uint64_t)count*8+8 != endtable ||
        endtable>(uint64_t)size) return NULL;
    if (fseek(f,(long)(table+id*8),SEEK_SET) || fread(entry,1,16,f)!=16) return NULL;
    uint32_t begin=big_u32(entry+4),end=big_u32(entry+12);
    if (begin<endtable || end<=begin || end>(uint64_t)size || end-begin>limit) return NULL;
    size_t bytes=end-begin;
    encoded=malloc(bytes);
    if (!encoded || fseek(f,begin,SEEK_SET) || fread(encoded,1,bytes,f)!=bytes) goto done;
    if (bytes>=12 && big_u32(encoded)==0x00800004) {
        uint32_t raw=big_u32(encoded+4),packed=big_u32(encoded+8);
        if (!raw || raw>limit || packed!=bytes-12) goto done;
        decoded=malloc(raw);
        uLongf actual=raw;
        if (!decoded || uncompress(decoded,&actual,encoded+12,packed)!=Z_OK || actual!=raw) goto bad;
        *length=raw;
    } else if (bytes>4 && big_u32(encoded)==4) {
        *length=bytes-4;
        decoded=malloc(*length);
        if (!decoded) goto done;
        memcpy(decoded,encoded+4,*length);
    } else goto done;
    if (!big_valid_png(decoded,*length,w,h)) goto bad;
    free(encoded);
    return decoded;
bad:
    free(decoded); decoded=NULL;
done:
    free(encoded);
    return decoded;
}
static vita2d_texture *big_texture(FILE *f, unsigned id, unsigned w, unsigned h) {
    if (!f) return NULL;
    size_t length=0;
    unsigned char *data=big_png(f,id,w,h,&length);
    if (!data) return NULL;
    vita2d_texture *texture=vita2d_load_PNG_buffer(data);
    free(data);
    return texture;
}
#endif
