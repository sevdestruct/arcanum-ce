#include "tig/movie.h"

#include <bink_compat.h>

#include <SDL3/SDL_timer.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <stdint.h>
#include <string.h>

#include "tig/color.h"
#include "tig/core.h"
#include "tig/kb.h"
#include "tig/message.h"
#include "tig/video.h"
#include "tig/window.h"

static bool tig_movie_do_frame(bool* presented);
typedef struct SdlMixerSoundData SdlMixerSoundData;

// 0x62B2BC
static HBINK tig_movie_bink;

static TigVideoBuffer* tig_movie_video_buffer;
static int tig_movie_screen_width;
static int tig_movie_screen_height;
static int tig_movie_display_width;
static int tig_movie_display_height;
static tig_timestamp_t tig_movie_next_frame_due_ms;
static bool tig_movie_audio_master_sync;
static bool tig_movie_frame_pending;
static int64_t tig_movie_pending_frame_time_ms;
static SdlMixerSoundData* tig_movie_active_sound_data;

struct SdlMixerSoundData {
    u8* buffer;
    u32 buffer_size;
    u32 max_queued_bytes;
    SDL_AudioStream* stream;
    MIX_Track* track;
    s32 paused;
    bool started;
    float volume;
    MIX_StereoGains pan;
};

static BINKSNDOPEN BINKCALL SdlMixerSystemOpen(void* param);
static s32 BINKCALL SdlMixerOpen(struct BINKSND* snd, u32 freq, s32 bits, s32 chans, u32 flags, HBINK bnk);
static s32 BINKCALL SdlMixerReady(struct BINKSND* snd);
static s32 BINKCALL SdlMixerLock(struct BINKSND* snd, u8** addr, u32* len);
static s32 BINKCALL SdlMixerUnlock(struct BINKSND* snd, u32 filled);
static void BINKCALL SdlMixerSetVolume(struct BINKSND* snd, s32 volume);
static void BINKCALL SdlMixerSetPan(struct BINKSND* snd, s32 pan);
static s32 BINKCALL SdlMixerPause(struct BINKSND* snd, s32 status);
static s32 BINKCALL SdlMixerSetOnOff(struct BINKSND* snd, s32 status);
static void BINKCALL SdlMixerClose(struct BINKSND* snd);
static bool sdl_mixer_play_track(MIX_Track* track);
static void setup_track(struct BINKSND* snd);
static int ref_mixer(void);
static void unref_mixer(void);
static tig_timestamp_t tig_movie_schedule_next_frame_due(tig_timestamp_t now, tig_timestamp_t previous_due, unsigned int frame_ms);
static bool tig_movie_time_is_in_future(tig_timestamp_t now, tig_timestamp_t due);
static unsigned int tig_movie_time_until_ms(tig_timestamp_t now, tig_timestamp_t due);
static bool tig_movie_path_uses_audio_master_sync(const char* path);
static bool tig_movie_get_audio_clock_ms(int64_t* audio_clock_ms);
static unsigned int tig_movie_compute_audio_master_delay_ms(unsigned int frame_ms, int64_t frame_time_ms, int64_t audio_clock_ms);
static void tig_movie_fill_audio_master_queue(void);

static MIX_Mixer* mixer;
static int mixer_refcount;

// 0x5314F0
int tig_movie_init(TigInitInfo* init_info)
{
    // COMPAT: Load `binkw32.dll`.
    bink_compat_init();

    BinkSetSoundSystem(SdlMixerSystemOpen, NULL);

    tig_movie_screen_width = init_info->width;
    tig_movie_screen_height = init_info->height;

    return TIG_OK;
}

// 0x531520
void tig_movie_exit(void)
{
    // COMPAT: Free binkw32.dll
    bink_compat_exit();
}

// 0x531530
int tig_movie_play(const char* path, TigMovieFlags movie_flags, int sound_track)
{
    unsigned int bink_open_flags = 0;
    TigMessage message;
    bool stop;
    bool presented;
    SDL_Scancode stop_scancode = SDL_SCANCODE_UNKNOWN;
    TigVideoBufferCreateInfo vb_create_info;

    if (path == NULL) {
        tig_video_fade(0, 0, 0.0f, 1);
        return TIG_ERR_INVALID_PARAM;
    }

    tig_movie_audio_master_sync = tig_movie_path_uses_audio_master_sync(path);
    tig_movie_frame_pending = false;
    tig_movie_pending_frame_time_ms = -1;
    bink_open_flags |= BINKSNDTRACK;

    if (sound_track >= 0) {
        BinkSetSoundTrack((unsigned)sound_track);
    }

    tig_movie_bink = BinkOpen(path, bink_open_flags);
    if (tig_movie_bink == NULL) {
        tig_video_fade(0, 0, 0.0f, 1);
        return TIG_ERR_IO;
    }

    tig_movie_next_frame_due_ms = 0;

    /* Compute display size: scale down to fit screen, never upscale. */
    {
        int vid_w = (int)tig_movie_bink->Width;
        int vid_h = (int)tig_movie_bink->Height;
        if (vid_w <= tig_movie_screen_width && vid_h <= tig_movie_screen_height) {
            tig_movie_display_width = vid_w;
            tig_movie_display_height = vid_h;
        } else {
            float scale_x = (float)tig_movie_screen_width / (float)vid_w;
            float scale_y = (float)tig_movie_screen_height / (float)vid_h;
            float scale = scale_x < scale_y ? scale_x : scale_y;
            tig_movie_display_width = (int)(vid_w * scale);
            tig_movie_display_height = (int)(vid_h * scale);
        }
    }

    vb_create_info.flags = TIG_VIDEO_BUFFER_CREATE_SYSTEM_MEMORY;
    vb_create_info.background_color = 0;
    vb_create_info.color_key = 0;
    vb_create_info.width = tig_movie_bink->Width;
    vb_create_info.height = tig_movie_bink->Height;

    if (tig_video_buffer_create(&vb_create_info, &tig_movie_video_buffer) != TIG_OK) {
        BinkClose(tig_movie_bink);
        tig_movie_audio_master_sync = false;
        return TIG_ERR_GENERIC;
    }

    if ((movie_flags & TIG_MOVIE_FADE_IN) != 0) {
        tig_video_fade(0, 64, 3.0f, 0);
    }

    // Reset to black.
    tig_video_fill(NULL, tig_color_make(0, 0, 0));
    tig_video_flip();

    tig_video_fade(0, 0, 0.0f, 1);

    // Clear message queue.
    while (tig_message_dequeue(&message) == TIG_OK) {
    }

    stop = false;
    while (!stop && tig_movie_do_frame(&presented)) {
        if (presented) {
            tig_video_flip();
        } else {
            tig_timestamp_t now_ms;
            unsigned int wait_ms = 1;

            tig_timer_now(&now_ms);
            if (tig_movie_next_frame_due_ms != 0
                && tig_movie_time_is_in_future(now_ms, tig_movie_next_frame_due_ms)) {
                wait_ms = tig_movie_time_until_ms(now_ms, tig_movie_next_frame_due_ms);
            }

            SDL_Delay(wait_ms > 5 ? 5 : wait_ms);
        }

        tig_ping();

        while (tig_message_dequeue(&message) == TIG_OK) {
            if ((movie_flags & TIG_MOVIE_IGNORE_KEYBOARD) == 0
                && message.type == TIG_MESSAGE_KEYBOARD
                && message.data.keyboard.pressed == 1
                && message.data.keyboard.scancode == SDL_SCANCODE_ESCAPE) {
                stop_scancode = message.data.keyboard.scancode;
                stop = true;
            }
        }
    }

    if (stop_scancode != SDL_SCANCODE_UNKNOWN) {
        // Wait until ESC is released so the next screen doesn't see a held key.
        while (tig_kb_is_key_pressed(stop_scancode)) {
            tig_ping();
        }
    }

    // Clear message queue.
    while (tig_message_dequeue(&message) == TIG_OK) {
    }

    tig_video_buffer_destroy(tig_movie_video_buffer);
    tig_movie_video_buffer = NULL;

    BinkClose(tig_movie_bink);
    tig_movie_bink = NULL;
    tig_movie_next_frame_due_ms = 0;
    tig_movie_audio_master_sync = false;
    tig_movie_frame_pending = false;
    tig_movie_pending_frame_time_ms = -1;
    tig_movie_active_sound_data = NULL;

    tig_window_invalidate_rect(NULL);

    if ((movie_flags & TIG_MOVIE_FADE_OUT) != 0) {
        tig_video_fade(0, 0, 0.0f, 0);
    }

    if ((movie_flags & TIG_MOVIE_NO_FINAL_FLIP) == 0) {
        tig_video_fill(NULL, tig_color_make(0, 0, 0));
        tig_video_flip();
    }

    return TIG_OK;
}

// NOTE: Original code probably provides access to the underlying BINK pointer.
// However this function is only used to check if movie is currently playing
// during message handling, so there is no point to expose it.
//
// 0x5317C0
bool tig_movie_is_playing(void)
{
    return tig_movie_bink != NULL;
}

// 0x5317D0
bool tig_movie_do_frame(bool* presented)
{
    TigVideoBufferData video_buffer_data;
    TigRect src_rect;
    TigRect dst_rect;
    int64_t audio_clock_ms;
    int64_t frame_time_ms;
    tig_timestamp_t now_ms;
    unsigned frame_ms;

    *presented = false;

    if (tig_movie_bink->Frames > 0
        && tig_movie_bink->FrameNum > tig_movie_bink->Frames) {
        return false;
    }

    frame_ms = tig_movie_bink->FrameDurationMs > 0
        ? tig_movie_bink->FrameDurationMs
        : 33;

    tig_timer_now(&now_ms);
    if (tig_movie_audio_master_sync) {
        tig_movie_fill_audio_master_queue();
    }

    if (tig_movie_next_frame_due_ms != 0
        && tig_movie_time_is_in_future(now_ms, tig_movie_next_frame_due_ms)) {
        return true;
    }

    if (!tig_movie_frame_pending && !BinkWait(tig_movie_bink)) {
        if (BinkDoFrame(tig_movie_bink) < 0) {
            return false;
        }

        if (tig_video_buffer_lock(tig_movie_video_buffer) != TIG_OK) {
            return false;
        }

        if (tig_video_buffer_data(tig_movie_video_buffer, &video_buffer_data) != TIG_OK) {
            return false;
        }

        src_rect.x = 0;
        src_rect.y = 0;
        src_rect.width = video_buffer_data.width;
        src_rect.height = video_buffer_data.height;

        dst_rect.x = (tig_movie_screen_width - tig_movie_display_width) / 2;
        dst_rect.y = (tig_movie_screen_height - tig_movie_display_height) / 2;
        dst_rect.width = tig_movie_display_width;
        dst_rect.height = tig_movie_display_height;

        // Copy movie pixels to the video buffer.
        BinkCopyToBuffer(tig_movie_bink,
            video_buffer_data.surface_data.pixels,
            video_buffer_data.pitch,
            video_buffer_data.height,
            0,
            0,
            3);

        tig_video_buffer_unlock(tig_movie_video_buffer);
        tig_movie_frame_pending = true;
        tig_movie_pending_frame_time_ms = -1;
        if (tig_movie_audio_master_sync) {
            if (bink_compat_get_frame_time_ms(tig_movie_bink, &frame_time_ms)) {
                tig_movie_pending_frame_time_ms = frame_time_ms;
            }
        }
    }

    if (tig_movie_frame_pending) {
        int64_t presented_frame_time_ms = tig_movie_pending_frame_time_ms;

        // Blit buffer to screen, scaling down if needed to fit within screen
        // dimensions while preserving aspect ratio.
        tig_video_blit_scaled(tig_movie_video_buffer, &src_rect, &dst_rect);

        *presented = true;

        BinkNextFrame(tig_movie_bink);
        tig_movie_frame_pending = false;
        tig_movie_pending_frame_time_ms = -1;
        if (tig_movie_audio_master_sync) {
            unsigned int delay_ms = frame_ms;

            if (presented_frame_time_ms >= 0
                && tig_movie_get_audio_clock_ms(&audio_clock_ms)) {
                delay_ms = tig_movie_compute_audio_master_delay_ms(
                    frame_ms,
                    presented_frame_time_ms,
                    audio_clock_ms);
            }

            tig_movie_next_frame_due_ms = now_ms + delay_ms;
            tig_movie_fill_audio_master_queue();
        } else {
            tig_movie_next_frame_due_ms = tig_movie_schedule_next_frame_due(
                now_ms,
                tig_movie_next_frame_due_ms,
                frame_ms);
        }
    }

    return true;
}

BINKSNDOPEN BINKCALL SdlMixerSystemOpen(void* param)
{
    (void)param;

    return SdlMixerOpen;
}

s32 BINKCALL SdlMixerOpen(struct BINKSND* snd, u32 freq, s32 bits, s32 chans, u32 flags, HBINK bnk)
{
    SdlMixerSoundData* snddata;
    SDL_AudioSpec src_spec;
    SDL_AudioSpec dst_spec;

    (void)flags;
    (void)bnk;

    memset(snd, 0, sizeof(*snd));

    if (ref_mixer() == 0) {
        return 0;
    }

    snd->freq = freq;
    snd->bits = bits;
    snd->chans = chans;

    snddata = (SdlMixerSoundData*)snd->snddata;

    src_spec.channels = (int)chans;
    src_spec.freq = (int)freq;
    src_spec.format = SDL_AUDIO_S16LE;
    MIX_GetMixerFormat(mixer, &dst_spec);
    snddata->buffer_size = freq * (u32)chans * (u32)(bits / 8);
    if (snddata->buffer_size < 65536) {
        snddata->buffer_size = 65536;
    }
    snddata->max_queued_bytes = snddata->buffer_size * 4;
    snddata->buffer = SDL_malloc(snddata->buffer_size);
    if (snddata->buffer == NULL) {
        return 0;
    }

    snddata->stream = SDL_CreateAudioStream(&src_spec, &dst_spec);
    if (snddata->stream == NULL) {
        SDL_free(snddata->buffer);
        return 0;
    }

    snddata->track = MIX_CreateTrack(mixer);
    if (snddata->track == NULL) {
        SDL_DestroyAudioStream(snddata->stream);
        SDL_free(snddata->buffer);
        return 0;
    }

    if (!MIX_SetTrackAudioStream(snddata->track, snddata->stream)) {
        MIX_DestroyTrack(snddata->track);
        SDL_DestroyAudioStream(snddata->stream);
        SDL_free(snddata->buffer);
        return 0;
    }

    snddata->paused = 0;
    snddata->started = false;
    snddata->volume = 1.0f;
    snddata->pan.left = 0.5f;
    snddata->pan.right = 0.5f;
    tig_movie_active_sound_data = snddata;
    setup_track(snd);

    snd->Latency = 64;
    snd->Ready = SdlMixerReady;
    snd->Lock = SdlMixerLock;
    snd->Unlock = SdlMixerUnlock;
    snd->Volume = SdlMixerSetVolume;
    snd->Pan = SdlMixerSetPan;
    snd->Pause = SdlMixerPause;
    snd->SetOnOff = SdlMixerSetOnOff;
    snd->Close = SdlMixerClose;

    return 1;
}

s32 BINKCALL SdlMixerReady(struct BINKSND* snd)
{
    SdlMixerSoundData* snddata = (SdlMixerSoundData*)snd->snddata;

    if (snddata->paused && !snd->OnOff) {
        return 0;
    }

    return SDL_GetAudioStreamQueued(snddata->stream) < (int)snddata->max_queued_bytes;
}

s32 BINKCALL SdlMixerLock(struct BINKSND* snd, u8** addr, u32* len)
{
    SdlMixerSoundData* snddata = (SdlMixerSoundData*)snd->snddata;

    *addr = snddata->buffer;
    *len = snddata->buffer_size;

    return 1;
}

s32 BINKCALL SdlMixerUnlock(struct BINKSND* snd, u32 filled)
{
    SdlMixerSoundData* snddata = (SdlMixerSoundData*)snd->snddata;

    if (filled == 0) {
        return 1;
    }

    SDL_PutAudioStreamData(snddata->stream, snddata->buffer, (int)filled);
    if (!snddata->started || !MIX_TrackPlaying(snddata->track)) {
        sdl_mixer_play_track(snddata->track);
        snddata->started = true;
    }

    return 1;
}

void BINKCALL SdlMixerSetVolume(struct BINKSND* snd, s32 volume)
{
    SdlMixerSoundData* snddata = (SdlMixerSoundData*)snd->snddata;

    if (volume < 0) {
        volume = 0;
    } else if (volume > 32767) {
        volume = 32767;
    }

    snddata->volume = (float)volume / 32767.0f;

    MIX_SetTrackGain(snddata->track, snddata->volume);
}

void BINKCALL SdlMixerSetPan(struct BINKSND* snd, s32 pan)
{
    SdlMixerSoundData* snddata = (SdlMixerSoundData*)snd->snddata;

    if (pan < 0) {
        pan = 0;
    } else if (pan > 65536) {
        pan = 65536;
    }

    snddata->pan.left = (65536.0f - (float)pan) / 65536.0f;
    snddata->pan.right = (float)pan / 65536.0f;

    MIX_SetTrackStereo(snddata->track, &(snddata->pan));
}

s32 BINKCALL SdlMixerPause(struct BINKSND* snd, s32 status)
{
    SdlMixerSoundData* snddata = (SdlMixerSoundData*)snd->snddata;

    if (status == 0) {
        if (snddata->paused == 1) {
            MIX_ResumeTrack(snddata->track);
            snddata->paused = 0;
        }
    } else if (status == 1) {
        if (snddata->paused == 0) {
            MIX_PauseTrack(snddata->track);
            snddata->paused = 1;
        }
    }

    return snddata->paused;
}

s32 BINKCALL SdlMixerSetOnOff(struct BINKSND* snd, s32 status)
{
    SdlMixerSoundData* snddata = (SdlMixerSoundData*)snd->snddata;

    if (status == 0) {
        if (snd->OnOff == 1) {
            MIX_StopTrack(snddata->track, 0);
            snd->OnOff = 0;
        }
    } else if (status == 1) {
        if (snd->OnOff == 0) {
            setup_track(snd);
            snd->OnOff = 1;
            if (SDL_GetAudioStreamQueued(snddata->stream) > 0) {
                sdl_mixer_play_track(snddata->track);
                snddata->started = true;
            }
        }
    }

    return snd->OnOff;
}

void BINKCALL SdlMixerClose(struct BINKSND* snd)
{
    SdlMixerSoundData* snddata = (SdlMixerSoundData*)snd->snddata;

    if (tig_movie_active_sound_data == snddata) {
        tig_movie_active_sound_data = NULL;
    }

    MIX_DestroyTrack(snddata->track);
    SDL_DestroyAudioStream(snddata->stream);
    SDL_free(snddata->buffer);

    unref_mixer();
}

bool sdl_mixer_play_track(MIX_Track* track)
{
    SDL_PropertiesID props;
    bool started;

    props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN, false);
    started = MIX_PlayTrack(track, props);
    SDL_DestroyProperties(props);

    return started;
}

void setup_track(struct BINKSND* snd)
{
    SdlMixerSoundData* snddata = (SdlMixerSoundData*)snd->snddata;

    MIX_SetTrackGain(snddata->track, snddata->volume);
    MIX_SetTrackStereo(snddata->track, &(snddata->pan));
}

int ref_mixer(void)
{
    if (++mixer_refcount == 1) {
        mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
        if (mixer == NULL) {
            --mixer_refcount;
            return 0;
        }
    }

    return 1;
}

void unref_mixer(void)
{
    if (--mixer_refcount == 0) {
        MIX_DestroyMixer(mixer);
    }
}

static tig_timestamp_t tig_movie_schedule_next_frame_due(tig_timestamp_t now, tig_timestamp_t previous_due, unsigned int frame_ms)
{
    tig_timestamp_t next_due;

    if (previous_due == 0) {
        return now + frame_ms;
    }

    next_due = previous_due + frame_ms;

    /* Clamp only after a noticeable stall; otherwise keep the original
     * timeline so video can catch back up to audio when we're slightly late. */
    if (!tig_movie_time_is_in_future(now, next_due)
        && (unsigned int)(now - next_due) > frame_ms * 4) {
        next_due = now + frame_ms;
    }

    return next_due;
}

bool tig_movie_time_is_in_future(tig_timestamp_t now, tig_timestamp_t due)
{
    return (int32_t)(due - now) > 0;
}

unsigned int tig_movie_time_until_ms(tig_timestamp_t now, tig_timestamp_t due)
{
    int32_t delta = (int32_t)(due - now);
    return delta > 0 ? (unsigned int)delta : 0;
}

static bool tig_movie_path_uses_audio_master_sync(const char* path)
{
    const char* ext;

    if (path == NULL) {
        return false;
    }

    ext = strrchr(path, '.');
    if (ext == NULL) {
        return false;
    }

    return SDL_strcasecmp(ext, ".mp4") == 0
        || SDL_strcasecmp(ext, ".m4v") == 0
        || SDL_strcasecmp(ext, ".mov") == 0
        || SDL_strcasecmp(ext, ".webm") == 0;
}

static bool tig_movie_get_audio_clock_ms(int64_t* audio_clock_ms)
{
    SdlMixerSoundData* snddata;
    Sint64 frames;
    Sint64 playback_ms;

    if (audio_clock_ms == NULL) {
        return false;
    }

    snddata = tig_movie_active_sound_data;
    if (snddata == NULL || snddata->track == NULL) {
        return false;
    }

    frames = MIX_GetTrackPlaybackPosition(snddata->track);
    if (frames < 0) {
        return false;
    }

    playback_ms = MIX_TrackFramesToMS(snddata->track, frames);
    if (playback_ms < 0) {
        return false;
    }

    *audio_clock_ms = (int64_t)playback_ms;
    return true;
}

static unsigned int tig_movie_compute_audio_master_delay_ms(unsigned int frame_ms, int64_t frame_time_ms, int64_t audio_clock_ms)
{
    int64_t diff_ms;
    int64_t delay_ms;
    int64_t sync_threshold_ms;

    diff_ms = frame_time_ms - audio_clock_ms;
    delay_ms = frame_ms;
    sync_threshold_ms = frame_ms;

    if (sync_threshold_ms < 10) {
        sync_threshold_ms = 10;
    } else if (sync_threshold_ms > 100) {
        sync_threshold_ms = 100;
    }

    /* Follow ffplay's general approach: keep the nominal frame cadence and
     * apply only bounded corrections when video drifts ahead/behind audio,
     * instead of driving every frame directly from the raw audio clock. */
    if (diff_ms > sync_threshold_ms) {
        delay_ms += diff_ms;
        if (delay_ms > (int64_t)frame_ms * 2) {
            delay_ms = (int64_t)frame_ms * 2;
        }
    } else if (diff_ms < -sync_threshold_ms) {
        delay_ms += diff_ms;
        if (delay_ms < 1) {
            delay_ms = 1;
        }
    }

    if (delay_ms < 1) {
        delay_ms = 1;
    }

    return (unsigned int)delay_ms;
}

static void tig_movie_fill_audio_master_queue(void)
{
    SdlMixerSoundData* snddata;
    int queued_bytes;
    int target_bytes;
    int attempts;

    snddata = tig_movie_active_sound_data;
    if (!tig_movie_audio_master_sync || snddata == NULL || snddata->stream == NULL) {
        return;
    }

    queued_bytes = SDL_GetAudioStreamQueued(snddata->stream);
    if (queued_bytes < 0) {
        return;
    }

    target_bytes = (int)(snddata->buffer_size * 2);
    if (target_bytes > (int)snddata->max_queued_bytes) {
        target_bytes = (int)snddata->max_queued_bytes;
    }

    attempts = 0;
    while (queued_bytes < target_bytes && attempts < 16) {
        if (!bink_compat_pump_audio(tig_movie_bink)) {
            break;
        }

        queued_bytes = SDL_GetAudioStreamQueued(snddata->stream);
        if (queued_bytes < 0) {
            break;
        }
        attempts++;
    }
}
