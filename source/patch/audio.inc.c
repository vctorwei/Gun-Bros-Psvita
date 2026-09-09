/* Loose OGG replacement for PCM media resources loaded from WVGA BIG packs. */

#ifdef GUNBROS_QUIET_LOGS
#define GUNBROS_AUDIO_LOG(...) ((void)0)
#else
#define GUNBROS_AUDIO_LOG(...) (sceClibPrintf)(__VA_ARGS__)
#endif

typedef unsigned int (*media_player_play_internal_fn_t)(void *, void *,
                                                         unsigned char,
                                                         unsigned char,
                                                         unsigned int);
typedef void (*media_player_set_sound_enabled_fn_t)(void *, unsigned char);

static media_player_set_sound_enabled_fn_t g_media_player_set_sound_enabled;
static void *g_media_player_enable_owner;

static void configure_media_player_sound_enable(uintptr_t address) {
    g_media_player_set_sound_enabled =
        (media_player_set_sound_enabled_fn_t)(uintptr_t)address;
    GUNBROS_AUDIO_LOG("[AUDIO-PLAYER][SETUP] SetSoundEnabled=%p\n",
                    (void *)address);
}

static void ensure_media_player_sound_enabled(void *self) {
    unsigned char *player;
    unsigned int before_enabled;
    unsigned int before_engine;
    unsigned int before_stream;

    if (!self || !g_media_player_set_sound_enabled ||
        g_media_player_enable_owner == self) {
        return;
    }

    player = (unsigned char *)self;
    before_enabled = player[0x24];
    before_engine = player[0x54];
    before_stream = player[0x64];
    g_media_player_enable_owner = self;
    if (!before_enabled || !before_engine) {
        g_media_player_set_sound_enabled(self, 1);
    }

    GUNBROS_AUDIO_LOG("[AUDIO-PLAYER][ENABLE] player=%p enabled=%u->%u "
                    "engine=%u->%u stream=%u\n",
                    self, before_enabled, (unsigned int)player[0x24],
                    before_engine, (unsigned int)player[0x54], before_stream);
}

static unsigned int continue_media_player_play_internal(void *self,
                                                         void *binary,
                                                         unsigned char priority,
                                                         unsigned char flags,
                                                         unsigned int stream_type) {
    media_player_play_internal_fn_t original;
    unsigned int result;

    if (!h_media_player_play_internal.addr) {
        return 0;
    }

    kuKernelCpuUnrestrictedMemcpy((void *)h_media_player_play_internal.addr,
                                  h_media_player_play_internal.orig_instr,
                                  sizeof(h_media_player_play_internal.orig_instr));
    kuKernelFlushCaches((void *)h_media_player_play_internal.addr,
                        sizeof(h_media_player_play_internal.orig_instr));
    original = (media_player_play_internal_fn_t)(uintptr_t)
        (h_media_player_play_internal.thumb_addr ?
         h_media_player_play_internal.thumb_addr :
         h_media_player_play_internal.addr);
    result = original(self, binary, priority, flags, stream_type);
    kuKernelCpuUnrestrictedMemcpy((void *)h_media_player_play_internal.addr,
                                  h_media_player_play_internal.patch_instr,
                                  sizeof(h_media_player_play_internal.patch_instr));
    kuKernelFlushCaches((void *)h_media_player_play_internal.addr,
                        sizeof(h_media_player_play_internal.patch_instr));
    return result;
}

static unsigned int redirect_media_player_play_internal(void *self,
                                                         void *binary,
                                                         unsigned char priority,
                                                         unsigned char flags,
                                                         unsigned int stream_type) {
    enum { SOUND_STREAM_PCM = 0 };
    int redirected = 0;
    unsigned int result;
    unsigned char *player = (unsigned char *)self;

    ensure_media_player_sound_enabled(self);

    if (stream_type == SOUND_STREAM_PCM) {
        redirected = gunbros_audio_redirect_big_sound(binary);
    }
    result = continue_media_player_play_internal(self, binary, priority, flags,
                                                  stream_type);
    if (redirected == 2) {
        GUNBROS_AUDIO_LOG("[AUDIO-BIG][PLAY] media=%p result=%u priority=%u "
                        "flags=0x%02x stream=%u enabled=%u engine=%u\n",
                        binary, result, (unsigned int)priority,
                        (unsigned int)flags, stream_type,
                        player ? (unsigned int)player[0x24] : 0,
                        player ? (unsigned int)player[0x54] : 0);
    }
    return result;
}

typedef int (*options_read_fn_t)(void *);

static int continue_options_read(void *self) {
    options_read_fn_t original;
    int result;

    if (!h_options_read.addr) {
        return 0;
    }

    kuKernelCpuUnrestrictedMemcpy((void *)h_options_read.addr,
                                  h_options_read.orig_instr,
                                  sizeof(h_options_read.orig_instr));
    kuKernelFlushCaches((void *)h_options_read.addr,
                        sizeof(h_options_read.orig_instr));
    original = (options_read_fn_t)(uintptr_t)
        (h_options_read.thumb_addr ? h_options_read.thumb_addr :
                                     h_options_read.addr);
    result = original(self);
    kuKernelCpuUnrestrictedMemcpy((void *)h_options_read.addr,
                                  h_options_read.patch_instr,
                                  sizeof(h_options_read.patch_instr));
    kuKernelFlushCaches((void *)h_options_read.addr,
                        sizeof(h_options_read.patch_instr));
    return result;
}

static int redirect_options_read(void *self) {
    static unsigned char initialized;
    int result = continue_options_read(self);

    if (self && !initialized) {
        unsigned char *options = (unsigned char *)self;
        const unsigned int before = options[0x15];
        options[0x15] = 1;
        initialized = 1;
        GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][DEFAULT] options=%p enabled=%u->1 "
                        "read=%d\n",
                        self, before, result);
    }
    return result;
}

static void play_music_request(const char *source, const char *path,
                               unsigned char looping) {
    int loaded;
    int playing;

    loaded = gunbros_music_load(path);
    gunbros_music_set_looping(looping != 0);
    if (loaded) {
        gunbros_music_start();
    }
    playing = gunbros_music_is_playing();
    GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][CALL] action=play source=%s request='%s' "
                    "loop=%u loaded=%d playing=%d\n",
                    source ? source : "unknown", path ? path : "(null)",
                    (unsigned int)looping, loaded, playing);
}

static void redirect_platform_play_music(void *self, const char *path,
                                         unsigned char looping) {
    (void)self;
    play_music_request("platform", path, looping);
}

static void redirect_hardware_play_music(const char *path,
                                         unsigned char looping,
                                         float unused_start,
                                         float unused_fade) {
    (void)unused_start;
    (void)unused_fade;
    play_music_request("hardware", path, looping);
}

static void redirect_platform_pause_music(void *self) {
    (void)self;
    gunbros_music_pause();
    GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][CALL] action=pause playing=%d\n",
                    gunbros_music_is_playing());
}

static void redirect_platform_resume_music(void *self) {
    (void)self;
    gunbros_music_start();
    GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][CALL] action=resume playing=%d\n",
                    gunbros_music_is_playing());
}

static void redirect_platform_stop_music(void *self) {
    (void)self;
    gunbros_music_stop();
    GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][CALL] action=stop playing=0\n");
}

static unsigned char redirect_platform_is_music_playing(void *self) {
    static int last_state = -1;
    static unsigned int log_count;
    const int playing = gunbros_music_is_playing();

    (void)self;
    if (playing != last_state || log_count < 4) {
        GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][CALL] action=query playing=%d\n",
                        playing);
        last_state = playing;
        ++log_count;
    }
    return playing ? 1 : 0;
}

static void redirect_platform_set_music_volume(void *self, float volume) {
    static float last_logged = -10.0f;
    static unsigned int log_count;
    float delta = volume - last_logged;

    (void)self;
    gunbros_music_set_volume(volume, volume);
    if (delta < 0.0f) {
        delta = -delta;
    }
    if (log_count < 4 || delta >= 0.25f ||
        (volume <= 0.0f && last_logged > 0.0f)) {
        GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][CALL] action=volume value=%.3f\n",
                        volume);
        last_logged = volume;
        ++log_count;
    }
}

static int install_platform_music_hook(const char *symbol,
                                       uintptr_t replacement) {
    const uintptr_t address = so_symbol(&so_mod, symbol);
    if (!address) {
        GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][SETUP] missing symbol=%s\n", symbol);
        return 0;
    }
    (void)hook_addr(address, replacement);
    return 1;
}

static void install_platform_music_hooks(void) {
    unsigned int installed = 0;
    unsigned int hardware_installed;

    installed += install_platform_music_hook(
        "_ZN22GluPlatformCallbackJNI9PlayMusicEPKcb",
        (uintptr_t)redirect_platform_play_music);
    installed += install_platform_music_hook(
        "_ZN22GluPlatformCallbackJNI10PauseMusicEv",
        (uintptr_t)redirect_platform_pause_music);
    installed += install_platform_music_hook(
        "_ZN22GluPlatformCallbackJNI11ResumeMusicEv",
        (uintptr_t)redirect_platform_resume_music);
    installed += install_platform_music_hook(
        "_ZN22GluPlatformCallbackJNI9StopMusicEv",
        (uintptr_t)redirect_platform_stop_music);
    installed += install_platform_music_hook(
        "_ZN22GluPlatformCallbackJNI14IsMusicPlayingEv",
        (uintptr_t)redirect_platform_is_music_playing);
    installed += install_platform_music_hook(
        "_ZN22GluPlatformCallbackJNI14SetMusicVolumeEf",
        (uintptr_t)redirect_platform_set_music_volume);
    hardware_installed = install_platform_music_hook(
        "_ZN8Hardware9PlayMusicEPKcbff",
        (uintptr_t)redirect_hardware_play_music);
    GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][SETUP] native callbacks=%u/6 "
                    "hardware-play=%u/1\n",
                    installed, hardware_installed);
}

typedef void (*bgm_play_track_fn_t)(void *, unsigned int, unsigned char);

static void continue_bgm_play_track(void *self, unsigned int track,
                                    unsigned char looping) {
    bgm_play_track_fn_t original;

    if (!h_bgm_play_track.addr) {
        return;
    }

    kuKernelCpuUnrestrictedMemcpy((void *)h_bgm_play_track.addr,
                                  h_bgm_play_track.orig_instr,
                                  sizeof(h_bgm_play_track.orig_instr));
    kuKernelFlushCaches((void *)h_bgm_play_track.addr,
                        sizeof(h_bgm_play_track.orig_instr));
    original = (bgm_play_track_fn_t)(uintptr_t)
        (h_bgm_play_track.thumb_addr ? h_bgm_play_track.thumb_addr :
                                      h_bgm_play_track.addr);
    original(self, track, looping);
    kuKernelCpuUnrestrictedMemcpy((void *)h_bgm_play_track.addr,
                                  h_bgm_play_track.patch_instr,
                                  sizeof(h_bgm_play_track.patch_instr));
    kuKernelFlushCaches((void *)h_bgm_play_track.addr,
                        sizeof(h_bgm_play_track.patch_instr));
}

static void redirect_bgm_play_track(void *self, unsigned int track,
                                    unsigned char looping) {
    static const char *const tracks[] = {
        "game_0.mp3", "1.mp3", "2.mp3", "3.mp3",
        "4.mp3", "5.mp3", "6.mp3"
    };
    int mode = self ? *(const int *)self : -1;
    int fallback = 0;
    if (self && mode == 2) {
        *(int *)self = 1;
        mode = 1;
        GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][DEFAULT] bgm=%p mode=2->1\n", self);
    }

    continue_bgm_play_track(self, track, looping);
    if (!gunbros_music_is_playing() && mode == 1 &&
        track < sizeof(tracks) / sizeof(tracks[0])) {
        fallback = 1;
        play_music_request("bgm-fallback", tracks[track], looping);
    }
    GUNBROS_AUDIO_LOG("[AUDIO-MUSIC][BGM] track=%u mode=%d loop=%u "
                    "fallback=%d playing=%d\n",
                    track, mode, (unsigned int)looping, fallback,
                    gunbros_music_is_playing());
}
