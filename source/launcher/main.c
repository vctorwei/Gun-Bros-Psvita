/* Version selection owns no Classic state. LoadExec replaces this process. */
#include <vita2d.h>
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#include <string.h>
#include "utils/data_paths.h"
#include "big.h"

#ifndef GUNBROS_LOGOS_PATH
#define GUNBROS_LOGOS_PATH "ux0:data/gunbros/logos/"
#endif

static vita2d_texture *background, *panels, *heading, *info_icon, *close_icon, *title_sprite, *selection_arrow;

static vita2d_pgf *font;
static const unsigned int white = RGBA8(238,243,248,255u);
static const unsigned int muted = RGBA8(168,185,201,255u);
static const unsigned int accent = RGBA8(116,218,255,255u);
static void text(int x, int y, float scale, unsigned int color, const char *s) {
    if (font) vita2d_pgf_draw_text(font, x, y, color, scale, s);
}
static int exists(const char *path) {
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0 && SCE_S_ISREG(st.st_mode);
}
static void logo(vita2d_texture *texture, float x, float y, float w, float h) {
    if (!texture) return;
    float tw = vita2d_texture_get_width(texture), th = vita2d_texture_get_height(texture);
    if (!tw || !th) return;
    float scale = w / tw < h / th ? w / tw : h / th;
    vita2d_draw_texture_scale(texture, x + (w-tw*scale)/2, y + (h-th*scale)/2, scale, scale);
}
/* Nine-slice the original glowing menu frame, preserving its corners. */
static void panel(float x, float y, float w, float h) {
    if (!panels) return;
    const float sx[4]={512,524,686,698}, sy[4]={53,65,95,107};
    float dx[4]={x,x+12,x+w-12,x+w},dy[4]={y,y+12,y+h-12,y+h};
    for(int r=0;r<3;++r) for(int c=0;c<3;++c)
        vita2d_draw_texture_part_scale(panels,dx[c],dy[r],sx[c],sy[r],
            sx[c+1]-sx[c],sy[r+1]-sy[r],
            (dx[c+1]-dx[c])/(sx[c+1]-sx[c]),(dy[r+1]-dy[r])/(sy[r+1]-sy[r]));
}
static void load_menu_art(void) {
    char path[512];
    FILE *pack=NULL;
    if(gunbros_resolve_data_path(path,sizeof(path),"pack0_core_wvga.big",false))
        pack=fopen(path,"rb");
    background=big_texture(pack,294,800,425);
    panels=big_texture(pack,361,1024,256);
    heading=big_texture(pack,365,512,128);
    info_icon=vita2d_load_PNG_file(GUNBROS_LOGOS_PATH "info_logo.png");
    close_icon=big_texture(pack,331,46,46);
    title_sprite=vita2d_load_PNG_file(GUNBROS_LOGOS_PATH "choose your version.png");
    selection_arrow=vita2d_load_PNG_file(GUNBROS_LOGOS_PATH "selection arrow.png");
    if(pack) fclose(pack);
    vita2d_texture *textures[]={background,panels,heading,info_icon,close_icon,title_sprite,selection_arrow};
    for(unsigned i=0;i<sizeof(textures)/sizeof(textures[0]);++i)
        if(textures[i]) vita2d_texture_set_filters(textures[i],SCE_GXM_TEXTURE_FILTER_LINEAR,SCE_GXM_TEXTURE_FILTER_LINEAR);
}
typedef struct { float x, y, w, h; } VersionBounds;
static VersionBounds version_bounds(int index, float weight) {
    float w=360+120*weight, h=144+46*weight;
    return (VersionBounds){680-w/2,198+index*194-h/2,w,h};
}
static void animate_versions(float weights[2], float from[2], int active,
                             float elapsed) {
    float t=elapsed/0.20f;
    if(t>1) t=1;
    t=t*t*(3-2*t); /* 200 ms smooth expansion, independent of frame rate. */
    for(int i=0;i<2;++i) {
        float target=active<2 ? (i==active ? 1.0f : 0.0f) : 0.5f;
        weights[i]=from[i]+(target-from[i])*t;
    }
}
static int hit(float x, float y, float bx, float by, float bw, float bh) {
    return x >= bx && x < bx+bw && y >= by && y < by+bh;
}
int main(void) {
    int selected = 0, previous_card = 0, popup = 0, touching = 0;
    unsigned int old_buttons = 0;
    char error[256] = "";
    float weights[2]={1,0}, from[2]={1,0};
    int animated_selection=0;
    uint64_t animation_start=sceKernelGetProcessTimeWide();
    VersionBounds cards[2]={version_bounds(0,1),version_bounds(1,0)};
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    if (vita2d_init() < 0) return 1;
    font = vita2d_load_default_pgf();
    if (!font) { vita2d_fini(); return 1; }
    load_menu_art();
    vita2d_texture *logos[2] = {vita2d_load_PNG_file(GUNBROS_LOGOS_PATH "classic.png"),
                              vita2d_load_PNG_file(GUNBROS_LOGOS_PATH "reloaded.png")};
    for (int i=0; i<2; ++i) if (logos[i])
        vita2d_texture_set_filters(logos[i], SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    vita2d_set_clear_color(RGBA8(12,20,30,255u));
    for (;;) {
        SceCtrlData pad = {0};
        pad.lx = pad.ly = 128;
        SceTouchData touch = {0};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        unsigned int buttons = pad.buttons;
        if (pad.lx < 64) buttons |= SCE_CTRL_LEFT;
        if (pad.lx > 192) buttons |= SCE_CTRL_RIGHT;
        if (pad.ly < 64) buttons |= SCE_CTRL_UP;
        if (pad.ly > 192) buttons |= SCE_CTRL_DOWN;
        unsigned int pressed = buttons & ~old_buttons;
        old_buttons = buttons;
        int tap = touch.reportNum && !touching;
        touching = touch.reportNum != 0;
        int tx = touching ? touch.report[0].x / 2 : -1;
        int ty = touching ? touch.report[0].y / 2 : -1;
        if (popup) {
            if ((pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE)) ||
                (tap && hit(tx,ty,350,440,260,64))) popup = 0;
        } else {
            if (pressed & SCE_CTRL_LEFT) selected = 0;
            if (pressed & SCE_CTRL_RIGHT) selected = 1;
            if (pressed & SCE_CTRL_UP) { previous_card = selected < 2 ? selected : previous_card; selected = selected == 1 ? 0 : 2; }
            if (pressed & SCE_CTRL_DOWN) selected = selected == 2 ? 0 : 1;
            if (pressed & SCE_CTRL_CIRCLE) selected = previous_card;
            int activate = (pressed & SCE_CTRL_CROSS) != 0;
            if (pressed & SCE_CTRL_TRIANGLE) { selected = 2; activate = 1; }
            if (tap) {
                for (int i=0;i<2;++i) {
                    VersionBounds b=cards[i];
                    if (hit(tx,ty,b.x,b.y,b.w,b.h)) { selected=i; activate=1; break; }
                }
                if (hit(tx,ty,836,24,88,80)) { selected = 2; activate = 1; }
            }
            if (selected < 2) previous_card = selected;
            if (activate) {
                if (selected == 2) popup = 2;
                else if (selected == 1) popup = 1;
                else if (!exists("app0:/classic.bin") || !exists(SO_PATH) ||
                         !gunbros_resolve_data_path_ex(NULL, 0, "pack0_core_wvga.big", false, NULL, NULL)) {
                    snprintf(error, sizeof(error), "Classic game files could not be found.\nInstall the Classic data in:\n%s\nThen try again.", DATA_PATH);
                    popup = 3;
                } else {
                    vita2d_wait_rendering_done();
                    int result = sceAppMgrLoadExec("app0:/classic.bin", NULL, NULL);
                    snprintf(error, sizeof(error), "Classic could not start (error %d).\nPlease reinstall the launcher package.", result);
                    popup = 3;
                }
            }
        }
        int active=(selected==2 || popup==2) ? 2 : selected;
        uint64_t now=sceKernelGetProcessTimeWide();
        if(active!=animated_selection) {
            from[0]=weights[0]; from[1]=weights[1];
            animation_start=now; animated_selection=active;
        }
        animate_versions(weights,from,active,(now-animation_start)/1000000.0f);
        for(int i=0;i<2;++i) cards[i]=version_bounds(i,weights[i]);
        vita2d_start_drawing();
        vita2d_clear_screen();
        if (background) vita2d_draw_texture_scale(background,0,0,1.2f,544.0f/425);
        if(title_sprite) logo(title_sprite,36,20,780,76);
        else {
            if (heading) vita2d_draw_texture_part_scale(heading,36,28,0,0,426,45,780.0f/426,1.25f);
            text(220,66,1.25f,white,"CHOOSE YOUR VERSION");
        }
        if (selected == 2) panel(836,24,88,80);
        logo(info_icon,855,36,80,80);
        if (!info_icon) text(872,76,1.4f,white,"i");
        for (int i=0; i<2; ++i) {
            VersionBounds b=cards[i];
            panel(b.x,b.y,b.w,b.h);
            logo(logos[i],b.x+26,b.y+16,b.w-52,b.h-32);
            if (!logos[i]) text(b.x+32,b.y+b.h/2+8,1.0f,white,i ? "GUN BROS RELOADED" : "GUN BROS CLASSIC");
            if(active==i) {
                /* Original blue arrow from the game's menu atlas. */
                if(selection_arrow) logo(selection_arrow,b.x-62,b.y+b.h/2-22,54,44);
                else if(panels) vita2d_draw_texture_part_scale(panels,b.x-30,b.y+b.h/2-12,157,74,21,16,1,1.5f);
                else text(b.x-25,b.y+b.h/2+8,1.4f,accent,">");
            }
        }
        text(36,524,0.85f,muted,"D-pad / stick: Navigate     X: Select     Triangle: Info");
        if (!background || !panels) text(36,456,0.8f,white,"Menu art missing or invalid.\nCheck Classic's pack0_core_wvga.big.");
        if (popup) {
            vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,200u));
            panel(58,32,844,480);
            text(88,77,1.4f,accent,popup == 2 ? "TWO VERSIONS. ONE BROTHERHOOD." :
                 popup == 1 ? "Gun Bros Reloaded" : "Gun Bros Classic");
            if (popup == 2) {
                text(88,122,1.1f,white,"Gun Bros Classic");
                text(88,156,0.82f,white,"Experience the original game in a\nfully offline, single-player\nexperience. Weapons and armor\nretain their original prices,\nwith pure offline progression\nand no online requirements.");
                text(410,122,1.1f,white,"Gun Bros Reloaded");
                text(410,156,0.82f,white,"A community-driven project dedicated to restoring\nGun Bros to what it was meant to be. We've restored\nmultiplayer functionality, including Bro-Op and\nBrotherhood, bringing back the social experience\nof the original game.");
                text(410,268,0.82f,white,"Thanks to Vincent, the original author, and to Xori\nfor their contributions. Special thanks to Mark030a\nfor testing the latest builds and providing valuable\nadvice and feedback.");
                text(410,372,1.0f,accent,"Ported by Rocroverss.");
                text(88,421,0.85f,accent,"NOT AVALIABLE YET ETA LATE NOVEMBER 2026");
            } else text(88,158,1.0f,white,popup == 1 ?
                "NOT AVALIABLE YET ETA LATE NOVEMBER 2026" : error);
            panel(350,440,260,64);
            logo(close_icon,365,452,40,40);
            text(415,480,1.1f,white,"X / O: Close");
        }
        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceDisplayWaitVblankStart();
    }
}
