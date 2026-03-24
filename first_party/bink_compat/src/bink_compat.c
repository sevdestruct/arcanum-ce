#include "bink_compat.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#endif

/* -------------------------------------------------------------------------
 * Non-Windows FFmpeg backend
 * -------------------------------------------------------------------------
 * On macOS/Linux there is no binkw32.dll, so we use FFmpeg to decode Bink
 * files (via its native Bink decoder) as well as MP4/WebM for the main
 * menu video background feature.  The public Bink API surface is the same;
 * the implementation dispatches through BinkFFmpeg instead of DLL function
 * pointers.
 * -------------------------------------------------------------------------
 */
#ifdef HAVE_FFMPEG

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#ifdef _WIN32

#ifndef BINK_COMPAT_DLL_AVCODEC
#define BINK_COMPAT_DLL_AVCODEC "avcodec-62.dll"
#endif

#ifndef BINK_COMPAT_DLL_AVFORMAT
#define BINK_COMPAT_DLL_AVFORMAT "avformat-62.dll"
#endif

#ifndef BINK_COMPAT_DLL_AVUTIL
#define BINK_COMPAT_DLL_AVUTIL "avutil-60.dll"
#endif

#ifndef BINK_COMPAT_DLL_SWSCALE
#define BINK_COMPAT_DLL_SWSCALE "swscale-9.dll"
#endif

#ifndef BINK_COMPAT_DLL_SWRESAMPLE
#define BINK_COMPAT_DLL_SWRESAMPLE "swresample-6.dll"
#endif

static HMODULE g_ffmpeg_avcodec_module;
static HMODULE g_ffmpeg_avformat_module;
static HMODULE g_ffmpeg_avutil_module;
static HMODULE g_ffmpeg_swscale_module;
static HMODULE g_ffmpeg_swresample_module;

typedef int (*PFN_av_get_bytes_per_sample)(enum AVSampleFormat sample_fmt);
typedef int (*PFN_swr_get_out_samples)(struct SwrContext* s, int in_samples);
typedef void* (*PFN_av_realloc)(void* ptr, size_t size);
typedef int (*PFN_swr_convert)(struct SwrContext* s, uint8_t** out, int out_count, const uint8_t** in, int in_count);
typedef void (*PFN_av_freep)(void* ptr);
typedef void (*PFN_sws_freeContext)(struct SwsContext* sws_context);
typedef void (*PFN_swr_free)(struct SwrContext** s);
typedef AVFrame* (*PFN_av_frame_alloc)(void);
typedef void (*PFN_av_frame_free)(AVFrame** frame);
typedef int (*PFN_av_frame_ref)(AVFrame* dst, const AVFrame* src);
typedef AVPacket* (*PFN_av_packet_alloc)(void);
typedef void (*PFN_av_packet_free)(AVPacket** pkt);
typedef void (*PFN_avcodec_free_context)(AVCodecContext** avctx);
typedef void (*PFN_avformat_close_input)(AVFormatContext** s);
typedef int (*PFN_sws_scale)(struct SwsContext* c, const uint8_t* const src_slice[], const int src_stride[], int src_slice_y, int src_slice_h, uint8_t* const dst[], const int dst_stride[]);
typedef int (*PFN_avcodec_receive_frame)(AVCodecContext* avctx, AVFrame* frame);
typedef int (*PFN_av_read_frame)(AVFormatContext* s, AVPacket* pkt);
typedef int (*PFN_avcodec_send_packet)(AVCodecContext* avctx, const AVPacket* avpkt);
typedef void (*PFN_av_packet_unref)(AVPacket* pkt);
typedef int (*PFN_avformat_open_input)(AVFormatContext** ps, const char* url, const AVInputFormat* fmt, AVDictionary** options);
typedef int (*PFN_avformat_find_stream_info)(AVFormatContext* ic, AVDictionary** options);
typedef int (*PFN_av_find_best_stream)(AVFormatContext* ic, enum AVMediaType type, int wanted_stream_nb, int related_stream, const AVCodec** decoder_ret, int flags);
typedef const AVCodec* (*PFN_avcodec_find_decoder)(enum AVCodecID id);
typedef AVCodecContext* (*PFN_avcodec_alloc_context3)(const AVCodec* codec);
typedef int (*PFN_avcodec_parameters_to_context)(AVCodecContext* codec, const AVCodecParameters* par);
typedef int (*PFN_avcodec_open2)(AVCodecContext* avctx, const AVCodec* codec, AVDictionary** options);
typedef struct SwsContext* (*PFN_sws_getContext)(int src_w, int src_h, enum AVPixelFormat src_format, int dst_w, int dst_h, enum AVPixelFormat dst_format, int flags, SwsFilter* src_filter, SwsFilter* dst_filter, const double* param);
typedef int (*PFN_swr_alloc_set_opts2)(struct SwrContext** ps, const AVChannelLayout* out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate, const AVChannelLayout* in_ch_layout, enum AVSampleFormat in_sample_fmt, int in_sample_rate, int log_offset, void* log_ctx);
typedef int (*PFN_swr_init)(struct SwrContext* s);
typedef int (*PFN_av_seek_frame)(AVFormatContext* s, int stream_index, int64_t timestamp, int flags);
typedef void (*PFN_avcodec_flush_buffers)(AVCodecContext* avctx);

static PFN_av_get_bytes_per_sample p_av_get_bytes_per_sample;
static PFN_swr_get_out_samples p_swr_get_out_samples;
static PFN_av_realloc p_av_realloc;
static PFN_swr_convert p_swr_convert;
static PFN_av_freep p_av_freep;
static PFN_sws_freeContext p_sws_freeContext;
static PFN_swr_free p_swr_free;
static PFN_av_frame_alloc p_av_frame_alloc;
static PFN_av_frame_free p_av_frame_free;
static PFN_av_frame_ref p_av_frame_ref;
static PFN_av_packet_alloc p_av_packet_alloc;
static PFN_av_packet_free p_av_packet_free;
static PFN_avcodec_free_context p_avcodec_free_context;
static PFN_avformat_close_input p_avformat_close_input;
static PFN_sws_scale p_sws_scale;
static PFN_avcodec_receive_frame p_avcodec_receive_frame;
static PFN_av_read_frame p_av_read_frame;
static PFN_avcodec_send_packet p_avcodec_send_packet;
static PFN_av_packet_unref p_av_packet_unref;
static PFN_avformat_open_input p_avformat_open_input;
static PFN_avformat_find_stream_info p_avformat_find_stream_info;
static PFN_av_find_best_stream p_av_find_best_stream;
static PFN_avcodec_find_decoder p_avcodec_find_decoder;
static PFN_avcodec_alloc_context3 p_avcodec_alloc_context3;
static PFN_avcodec_parameters_to_context p_avcodec_parameters_to_context;
static PFN_avcodec_open2 p_avcodec_open2;
static PFN_sws_getContext p_sws_getContext;
static PFN_swr_alloc_set_opts2 p_swr_alloc_set_opts2;
static PFN_swr_init p_swr_init;
static PFN_av_seek_frame p_av_seek_frame;
static PFN_avcodec_flush_buffers p_avcodec_flush_buffers;

static bool bink_ffmpeg_load_proc(HMODULE module, FARPROC* proc, const char* name)
{
    *proc = GetProcAddress(module, name);
    return *proc != NULL;
}

#define BINK_FFMPEG_LOAD_PROC(module, symbol) \
    bink_ffmpeg_load_proc((module), (FARPROC*)&p_##symbol, #symbol)

static void bink_ffmpeg_unload(void)
{
    p_av_get_bytes_per_sample = NULL;
    p_swr_get_out_samples = NULL;
    p_av_realloc = NULL;
    p_swr_convert = NULL;
    p_av_freep = NULL;
    p_sws_freeContext = NULL;
    p_swr_free = NULL;
    p_av_frame_alloc = NULL;
    p_av_frame_free = NULL;
    p_av_frame_ref = NULL;
    p_av_packet_alloc = NULL;
    p_av_packet_free = NULL;
    p_avcodec_free_context = NULL;
    p_avformat_close_input = NULL;
    p_sws_scale = NULL;
    p_avcodec_receive_frame = NULL;
    p_av_read_frame = NULL;
    p_avcodec_send_packet = NULL;
    p_av_packet_unref = NULL;
    p_avformat_open_input = NULL;
    p_avformat_find_stream_info = NULL;
    p_av_find_best_stream = NULL;
    p_avcodec_find_decoder = NULL;
    p_avcodec_alloc_context3 = NULL;
    p_avcodec_parameters_to_context = NULL;
    p_avcodec_open2 = NULL;
    p_sws_getContext = NULL;
    p_swr_alloc_set_opts2 = NULL;
    p_swr_init = NULL;
    p_av_seek_frame = NULL;
    p_avcodec_flush_buffers = NULL;

    if (g_ffmpeg_avformat_module != NULL) {
        FreeLibrary(g_ffmpeg_avformat_module);
        g_ffmpeg_avformat_module = NULL;
    }
    if (g_ffmpeg_avcodec_module != NULL) {
        FreeLibrary(g_ffmpeg_avcodec_module);
        g_ffmpeg_avcodec_module = NULL;
    }
    if (g_ffmpeg_swscale_module != NULL) {
        FreeLibrary(g_ffmpeg_swscale_module);
        g_ffmpeg_swscale_module = NULL;
    }
    if (g_ffmpeg_swresample_module != NULL) {
        FreeLibrary(g_ffmpeg_swresample_module);
        g_ffmpeg_swresample_module = NULL;
    }
    if (g_ffmpeg_avutil_module != NULL) {
        FreeLibrary(g_ffmpeg_avutil_module);
        g_ffmpeg_avutil_module = NULL;
    }
}

static bool bink_ffmpeg_load(void)
{
    if (g_ffmpeg_avformat_module != NULL
        && g_ffmpeg_avcodec_module != NULL
        && g_ffmpeg_avutil_module != NULL
        && g_ffmpeg_swscale_module != NULL
        && g_ffmpeg_swresample_module != NULL) {
        return true;
    }

    bink_ffmpeg_unload();

    g_ffmpeg_avutil_module = LoadLibraryA(BINK_COMPAT_DLL_AVUTIL);
    g_ffmpeg_swresample_module = LoadLibraryA(BINK_COMPAT_DLL_SWRESAMPLE);
    g_ffmpeg_swscale_module = LoadLibraryA(BINK_COMPAT_DLL_SWSCALE);
    g_ffmpeg_avcodec_module = LoadLibraryA(BINK_COMPAT_DLL_AVCODEC);
    g_ffmpeg_avformat_module = LoadLibraryA(BINK_COMPAT_DLL_AVFORMAT);

    if (g_ffmpeg_avutil_module == NULL
        || g_ffmpeg_swresample_module == NULL
        || g_ffmpeg_swscale_module == NULL
        || g_ffmpeg_avcodec_module == NULL
        || g_ffmpeg_avformat_module == NULL) {
        bink_ffmpeg_unload();
        return false;
    }

    if (!BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avutil_module, av_get_bytes_per_sample)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_swresample_module, swr_get_out_samples)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avutil_module, av_realloc)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_swresample_module, swr_convert)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avutil_module, av_freep)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_swscale_module, sws_freeContext)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_swresample_module, swr_free)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avutil_module, av_frame_alloc)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avutil_module, av_frame_free)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avutil_module, av_frame_ref)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, av_packet_alloc)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, av_packet_free)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, avcodec_free_context)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avformat_module, avformat_close_input)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_swscale_module, sws_scale)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, avcodec_receive_frame)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avformat_module, av_read_frame)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, avcodec_send_packet)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, av_packet_unref)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avformat_module, avformat_open_input)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avformat_module, avformat_find_stream_info)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avformat_module, av_find_best_stream)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, avcodec_find_decoder)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, avcodec_alloc_context3)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, avcodec_parameters_to_context)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, avcodec_open2)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_swscale_module, sws_getContext)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_swresample_module, swr_alloc_set_opts2)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_swresample_module, swr_init)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avformat_module, av_seek_frame)
        || !BINK_FFMPEG_LOAD_PROC(g_ffmpeg_avcodec_module, avcodec_flush_buffers)) {
        bink_ffmpeg_unload();
        return false;
    }

    return true;
}

#define av_get_bytes_per_sample p_av_get_bytes_per_sample
#define swr_get_out_samples p_swr_get_out_samples
#define av_realloc p_av_realloc
#define swr_convert p_swr_convert
#define av_freep p_av_freep
#define sws_freeContext p_sws_freeContext
#define swr_free p_swr_free
#define av_frame_alloc p_av_frame_alloc
#define av_frame_free p_av_frame_free
#define av_frame_ref p_av_frame_ref
#define av_packet_alloc p_av_packet_alloc
#define av_packet_free p_av_packet_free
#define avcodec_free_context p_avcodec_free_context
#define avformat_close_input p_avformat_close_input
#define sws_scale p_sws_scale
#define avcodec_receive_frame p_avcodec_receive_frame
#define av_read_frame p_av_read_frame
#define avcodec_send_packet p_avcodec_send_packet
#define av_packet_unref p_av_packet_unref
#define avformat_open_input p_avformat_open_input
#define avformat_find_stream_info p_avformat_find_stream_info
#define av_find_best_stream p_av_find_best_stream
#define avcodec_find_decoder p_avcodec_find_decoder
#define avcodec_alloc_context3 p_avcodec_alloc_context3
#define avcodec_parameters_to_context p_avcodec_parameters_to_context
#define avcodec_open2 p_avcodec_open2
#define sws_getContext p_sws_getContext
#define swr_alloc_set_opts2 p_swr_alloc_set_opts2
#define swr_init p_swr_init
#define av_seek_frame p_av_seek_frame
#define avcodec_flush_buffers p_avcodec_flush_buffers

#endif /* _WIN32 */

/* Global sound system opener set by BinkSetSoundSystem. */
static BINKSNDSYSOPEN g_snd_sys_open;
static void* g_snd_sys_param;
static bool g_ffmpeg_backend_available = true;

typedef struct BinkFFmpegQueuedFrame {
    AVFrame* frame;
    int64_t time_ns;
} BinkFFmpegQueuedFrame;

/* Internal per-instance state wrapping the public BINK header. */
typedef struct BinkFFmpeg {
    struct BINK pub; /* must be first — HBINK aliases this */
    AVFormatContext* fmt_ctx;
    AVCodecContext* vid_ctx;
    AVCodecContext* aud_ctx;
    int vid_stream;
    int aud_stream;
    AVFrame* display_frame;
    AVFrame* decode_frame;
    AVFrame* decode_audio_frame;
    AVPacket* pkt;
    BinkFFmpegQueuedFrame* queued_frames;
    int queued_frame_count;
    int queued_frame_capacity;
    struct SwsContext* sws;
    struct SwrContext* swr;
    int64_t vid_start_pts; /* stream PTS of first frame, for seeking */
    uint8_t* audio_scratch;
    int audio_scratch_size;
    uint8_t* pending_audio;
    int pending_audio_size;
    int pending_audio_capacity;
    int64_t current_frame_time_ns;
    bool demux_eof;
    /* Embedded BINKSND filled by the caller's sound system open function. */
    BINKSND snd_buf;
    bool has_snd;
    bool precise_timing;
} BinkFFmpeg;

static int64_t bink_ffmpeg_stream_time_ns(AVStream* stream, int64_t pts)
{
    int64_t start_pts;
    double seconds;

    if (pts == AV_NOPTS_VALUE) {
        return -1;
    }

    start_pts = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    if (pts < start_pts) {
        pts = start_pts;
    }

    seconds = (double)(pts - start_pts) * av_q2d(stream->time_base);
    if (seconds < 0.0) {
        seconds = 0.0;
    }

    return (int64_t)(seconds * 1000000000.0 + 0.5);
}

static void bink_ffmpeg_feed_audio(BinkFFmpeg* bff, AVFrame* audio_frame);
static bool bink_ffmpeg_flush_pending_audio(BinkFFmpeg* bff);

static void bink_ffmpeg_clear_queued_frames(BinkFFmpeg* bff)
{
    int index;

    for (index = 0; index < bff->queued_frame_count; index++) {
        av_frame_free(&bff->queued_frames[index].frame);
    }

    bff->queued_frame_count = 0;
}

static bool bink_ffmpeg_enqueue_video_frame(BinkFFmpeg* bff, AVFrame* frame)
{
    BinkFFmpegQueuedFrame* new_items;
    AVFrame* queued_frame;
    int new_capacity;
    int64_t pts;

    if (bff->queued_frame_count == bff->queued_frame_capacity) {
        new_capacity = bff->queued_frame_capacity == 0
            ? 8
            : bff->queued_frame_capacity * 2;
        new_items = av_realloc(
            bff->queued_frames,
            (size_t)new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            return false;
        }

        bff->queued_frames = new_items;
        bff->queued_frame_capacity = new_capacity;
    }

    queued_frame = av_frame_alloc();
    if (queued_frame == NULL) {
        return false;
    }

    if (av_frame_ref(queued_frame, frame) < 0) {
        av_frame_free(&queued_frame);
        return false;
    }

    bff->queued_frames[bff->queued_frame_count].frame = queued_frame;
    pts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
    bff->queued_frames[bff->queued_frame_count].time_ns = bink_ffmpeg_stream_time_ns(
        bff->fmt_ctx->streams[bff->vid_stream],
        pts);
    bff->queued_frame_count++;
    return true;
}

static bool bink_ffmpeg_dequeue_video_frame(BinkFFmpeg* bff)
{
    BinkFFmpegQueuedFrame queued;

    if (bff->queued_frame_count == 0) {
        return false;
    }

    queued = bff->queued_frames[0];
    if (bff->queued_frame_count > 1) {
        memmove(
            bff->queued_frames,
            bff->queued_frames + 1,
            (size_t)(bff->queued_frame_count - 1) * sizeof(*bff->queued_frames));
    }
    bff->queued_frame_count--;

    av_frame_free(&bff->display_frame);
    bff->display_frame = queued.frame;
    bff->current_frame_time_ns = queued.time_ns;
    return bff->display_frame != NULL;
}

static void bink_ffmpeg_decode_audio_packet(BinkFFmpeg* bff)
{
    avcodec_send_packet(bff->aud_ctx, bff->pkt);
    av_packet_unref(bff->pkt);

    while (avcodec_receive_frame(bff->aud_ctx, bff->decode_audio_frame) == 0) {
        bink_ffmpeg_feed_audio(bff, bff->decode_audio_frame);
    }
}

static bool bink_ffmpeg_drain_video_frames(BinkFFmpeg* bff)
{
    bool queued_video;

    queued_video = false;

    while (avcodec_receive_frame(bff->vid_ctx, bff->decode_frame) == 0) {
        if (!bink_ffmpeg_enqueue_video_frame(bff, bff->decode_frame)) {
            break;
        }
        queued_video = true;
    }

    return queued_video;
}

static bool bink_ffmpeg_flush_pending_audio(BinkFFmpeg* bff)
{
    BINKSND* snd;
    bool wrote_audio;

    snd = &bff->snd_buf;
    wrote_audio = false;

    while (bff->pending_audio_size > 0) {
        u8* addr;
        u32 len;
        int chunk_bytes;

        if (!bff->has_snd || !snd->Ready || !snd->Lock || !snd->Unlock) {
            break;
        }

        if (!snd->Ready(snd)) {
            break;
        }

        if (!snd->Lock(snd, &addr, &len)) {
            break;
        }

        chunk_bytes = bff->pending_audio_size;
        if ((u32)chunk_bytes > len) {
            chunk_bytes = (int)len;
        }

        memcpy(addr, bff->pending_audio, (size_t)chunk_bytes);
        snd->Unlock(snd, (u32)chunk_bytes);
        wrote_audio = true;

        bff->pending_audio_size -= chunk_bytes;
        if (bff->pending_audio_size > 0) {
            memmove(
                bff->pending_audio,
                bff->pending_audio + chunk_bytes,
                (size_t)bff->pending_audio_size);
        }
    }

    return wrote_audio;
}

static bool bink_ffmpeg_pump(BinkFFmpeg* bff, int packet_budget, bool stop_after_video)
{
    int ret;
    bool progressed;

    if (!bff->precise_timing || bff->demux_eof) {
        return false;
    }

    progressed = bink_ffmpeg_flush_pending_audio(bff);
    if (bff->pending_audio_size > 0) {
        return progressed;
    }

    while (packet_budget-- > 0) {
        ret = av_read_frame(bff->fmt_ctx, bff->pkt);
        if (ret < 0) {
            bff->demux_eof = true;
            avcodec_send_packet(bff->vid_ctx, NULL);
            progressed |= bink_ffmpeg_drain_video_frames(bff);
            if (bff->aud_ctx != NULL) {
                avcodec_send_packet(bff->aud_ctx, NULL);
                while (avcodec_receive_frame(bff->aud_ctx, bff->decode_audio_frame) == 0) {
                    bink_ffmpeg_feed_audio(bff, bff->decode_audio_frame);
                    progressed = true;
                }
            }
            break;
        }

        if (bff->pkt->stream_index == bff->aud_stream && bff->aud_ctx) {
            bink_ffmpeg_decode_audio_packet(bff);
            progressed = true;
        } else if (bff->pkt->stream_index == bff->vid_stream) {
            avcodec_send_packet(bff->vid_ctx, bff->pkt);
            av_packet_unref(bff->pkt);
            if (bink_ffmpeg_drain_video_frames(bff)) {
                progressed = true;
            }
            if (stop_after_video && bff->queued_frame_count > 0) {
                break;
            }
        } else {
            av_packet_unref(bff->pkt);
        }
    }

    return progressed;
}

static bool bink_ffmpeg_ensure_video_frame(BinkFFmpeg* bff)
{
    while (bff->queued_frame_count == 0 && !bff->demux_eof) {
        if (!bink_ffmpeg_pump(bff, 32, true)) {
            break;
        }
    }

    return bff->queued_frame_count > 0;
}

static bool bink_ffmpeg_pump_audio(BinkFFmpeg* bff)
{
    return bink_ffmpeg_pump(bff, 16, false);
}

/* Feed one audio frame worth of PCM to the BINKSND callbacks. */
static void bink_ffmpeg_feed_audio(BinkFFmpeg* bff, AVFrame* audio_frame)
{
    BINKSND* snd = &bff->snd_buf;
    uint8_t* out_data[1];
    int out_capacity_samples;
    int out_samples;
    int bytes_per_sample;
    int bytes_total;

    if (!bff->has_snd || !snd->Ready || !snd->Lock || !snd->Unlock) {
        return;
    }

    if (bff->pending_audio_size > 0) {
        bink_ffmpeg_flush_pending_audio(bff);
    }

    bytes_per_sample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * (int)snd->chans;
    if (bytes_per_sample <= 0) {
        return;
    }

    out_capacity_samples = swr_get_out_samples(bff->swr, audio_frame->nb_samples);
    if (out_capacity_samples <= 0) {
        return;
    }

    if (out_capacity_samples * bytes_per_sample > bff->audio_scratch_size) {
        uint8_t* new_buffer = av_realloc(
            bff->audio_scratch,
            (size_t)out_capacity_samples * (size_t)bytes_per_sample);
        if (new_buffer == NULL) {
            return;
        }

        bff->audio_scratch = new_buffer;
        bff->audio_scratch_size = out_capacity_samples * bytes_per_sample;
    }

    out_data[0] = bff->audio_scratch;

    /* Convert the full decoded audio frame up front so large Bink audio
     * packets keep their original timing instead of being truncated to the
     * small callback scratch buffer.
     * Pass extended_data directly so planar multi-channel formats (e.g.
     * AV_SAMPLE_FMT_FLTP) supply all channel planes, not just channel 0. */
    out_samples = swr_convert(bff->swr,
        out_data,
        out_capacity_samples,
        (const uint8_t**)audio_frame->extended_data,
        audio_frame->nb_samples);
    if (out_samples <= 0) {
        return;
    }

    bytes_total = out_samples * bytes_per_sample;
    if (bff->pending_audio_size + bytes_total > bff->pending_audio_capacity) {
        int new_capacity;
        uint8_t* new_buffer;

        new_capacity = bff->pending_audio_capacity > 0
            ? bff->pending_audio_capacity
            : 65536;
        while (new_capacity < bff->pending_audio_size + bytes_total) {
            new_capacity *= 2;
        }

        new_buffer = av_realloc(bff->pending_audio, (size_t)new_capacity);
        if (new_buffer == NULL) {
            return;
        }
        bff->pending_audio = new_buffer;
        bff->pending_audio_capacity = new_capacity;
    }

    memcpy(
        bff->pending_audio + bff->pending_audio_size,
        bff->audio_scratch,
        (size_t)bytes_total);
    bff->pending_audio_size += bytes_total;
    bink_ffmpeg_flush_pending_audio(bff);

}

static HBINK bink_ffmpeg_open_impl(const char* name, unsigned flags)
{
    BinkFFmpeg* bff;
    AVStream* vs;
    const AVCodec* vcodec;

    bff = (BinkFFmpeg*)calloc(1, sizeof(*bff));
    if (!bff) return NULL;

    bff->vid_stream = -1;
    bff->aud_stream = -1;

    if (avformat_open_input(&bff->fmt_ctx, name, NULL, NULL) < 0) {
        free(bff);
        return NULL;
    }

    if (avformat_find_stream_info(bff->fmt_ctx, NULL) < 0) {
        avformat_close_input(&bff->fmt_ctx);
        free(bff);
        return NULL;
    }

    bff->vid_stream = av_find_best_stream(bff->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (bff->vid_stream < 0) {
        avformat_close_input(&bff->fmt_ctx);
        free(bff);
        return NULL;
    }

    vs = bff->fmt_ctx->streams[bff->vid_stream];
    vcodec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!vcodec) {
        avformat_close_input(&bff->fmt_ctx);
        free(bff);
        return NULL;
    }

    bff->vid_ctx = avcodec_alloc_context3(vcodec);
    if (!bff->vid_ctx) {
        avformat_close_input(&bff->fmt_ctx);
        free(bff);
        return NULL;
    }

    avcodec_parameters_to_context(bff->vid_ctx, vs->codecpar);
    if (avcodec_open2(bff->vid_ctx, vcodec, NULL) < 0) {
        avcodec_free_context(&bff->vid_ctx);
        avformat_close_input(&bff->fmt_ctx);
        free(bff);
        return NULL;
    }

    /* Swscale context: any source format -> BGRA. We use SWS_POINT because we
     * don't strictly scale here, we just convert color spaces (YUV420P -> BGRA).
     * This often unlocks accelerated paths that SWS_FAST_BILINEAR misses. */
    bff->sws = sws_getContext(
        bff->vid_ctx->width, bff->vid_ctx->height, bff->vid_ctx->pix_fmt,
        bff->vid_ctx->width, bff->vid_ctx->height, AV_PIX_FMT_BGRA,
        SWS_POINT, NULL, NULL, NULL);
    if (!bff->sws) {
        avcodec_free_context(&bff->vid_ctx);
        avformat_close_input(&bff->fmt_ctx);
        free(bff);
        return NULL;
    }

    if ((flags & BINKSNDTRACK) != 0) {
        bff->aud_stream = av_find_best_stream(
            bff->fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

        if (bff->aud_stream >= 0) {
            AVStream* as = bff->fmt_ctx->streams[bff->aud_stream];
            const AVCodec* acodec = avcodec_find_decoder(as->codecpar->codec_id);

            if (acodec) {
                bff->aud_ctx = avcodec_alloc_context3(acodec);
                if (bff->aud_ctx) {
                    avcodec_parameters_to_context(bff->aud_ctx, as->codecpar);
                    if (avcodec_open2(bff->aud_ctx, acodec, NULL) < 0) {
                        avcodec_free_context(&bff->aud_ctx);
                        bff->aud_stream = -1;
                    }
                }
            }

            if (bff->aud_ctx) {
                AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                swr_alloc_set_opts2(&bff->swr,
                    &stereo, AV_SAMPLE_FMT_S16, bff->aud_ctx->sample_rate,
                    &bff->aud_ctx->ch_layout, bff->aud_ctx->sample_fmt, bff->aud_ctx->sample_rate,
                    0, NULL);
                if (!bff->swr || swr_init(bff->swr) < 0) {
                    swr_free(&bff->swr);
                    bff->swr = NULL;
                }

                if (bff->swr && g_snd_sys_open) {
                    BINKSNDOPEN open_fn = g_snd_sys_open(g_snd_sys_param);
                    if (open_fn) {
                        int ch = bff->aud_ctx->ch_layout.nb_channels;
                        if (open_fn(&bff->snd_buf,
                                (uint32_t)bff->aud_ctx->sample_rate,
                                16, ch > 2 ? 2 : ch, 0, (HBINK)bff)
                            != 0) {
                            bff->has_snd = true;
                        }
                    }
                }
            }
        }
    }

    bff->display_frame = av_frame_alloc();
    bff->decode_frame = av_frame_alloc();
    bff->decode_audio_frame = av_frame_alloc();
    bff->pkt = av_packet_alloc();
    if (!bff->display_frame || !bff->decode_frame || !bff->decode_audio_frame || !bff->pkt) {
        BinkClose((HBINK)bff);
        return NULL;
    }

    bff->pub.Width = (unsigned)bff->vid_ctx->width;
    bff->pub.Height = (unsigned)bff->vid_ctx->height;
    bff->pub.FrameNum = 0;

    if (vs->nb_frames > 0) {
        bff->pub.Frames = (unsigned)vs->nb_frames;
    } else if (vs->avg_frame_rate.den > 0 && vs->avg_frame_rate.num > 0
        && bff->fmt_ctx->duration != AV_NOPTS_VALUE) {
        double fps = av_q2d(vs->avg_frame_rate);
        double dur = (double)bff->fmt_ctx->duration / AV_TIME_BASE;
        bff->pub.Frames = (unsigned)(dur * fps + 0.5);
    } else {
        bff->pub.Frames = 0;
    }

    if (vs->avg_frame_rate.num > 0 && vs->avg_frame_rate.den > 0) {
        bff->pub.FrameDurationMs = (unsigned)(1000.0 * vs->avg_frame_rate.den
            / vs->avg_frame_rate.num + 0.5);
    } else {
        bff->pub.FrameDurationMs = 33;
    }

    bff->vid_start_pts = (vs->start_time != AV_NOPTS_VALUE) ? vs->start_time : 0;
    bff->current_frame_time_ns = -1;
    bff->precise_timing = bff->aud_ctx != NULL
        && vs->codecpar->codec_id != AV_CODEC_ID_BINKVIDEO;

    return (HBINK)bff;
}

#endif /* HAVE_FFMPEG */

typedef void(BINKCALL* BINKCLOSE)(HBINK);
typedef int(BINKCALL* BINKCOPYTOBUFFER)(HBINK, void*, int, unsigned, unsigned, unsigned, unsigned);
typedef int(BINKCALL* BINKDDSURFACETYPE)(void*);
typedef int(BINKCALL* BINKDOFRAME)(HBINK);
typedef void(BINKCALL* BINKNEXTFRAME)(HBINK);
typedef HBINK(BINKCALL* BINKOPEN)(const char*, unsigned);
typedef BINKSNDOPEN(BINKCALL* BINKOPENMILES)(void*);
typedef int(BINKCALL* BINKSETSOUNDSYSTEM)(BINKSNDSYSOPEN, void*);
typedef void(BINKCALL* BINKSETSOUNDTRACK)(unsigned);
typedef int(BINKCALL* BINKWAIT)(HBINK);

#ifdef _WIN32
static HMODULE binkw32;
#endif

static BINKCLOSE _BinkClose;
static BINKCOPYTOBUFFER _BinkCopyToBuffer;
static BINKDDSURFACETYPE _BinkDDSurfaceType;
static BINKDOFRAME _BinkDoFrame;
static BINKNEXTFRAME _BinkNextFrame;
static BINKOPEN _BinkOpen;
static BINKOPENMILES _BinkOpenMiles;
static BINKSETSOUNDSYSTEM _BinkSetSoundSystem;
static BINKSETSOUNDTRACK _BinkSetSoundTrack;
static BINKWAIT _BinkWait;

void BINKCALL BinkClose(HBINK bnk)
{
#ifdef _WIN32
    if (_BinkClose != NULL) {
        _BinkClose(bnk);
        return;
    }
#endif
#ifdef HAVE_FFMPEG
    BinkFFmpeg* bff = (BinkFFmpeg*)bnk;
    if (!bff) return;

    if (bff->has_snd && bff->snd_buf.Close) {
        bff->snd_buf.Close(&bff->snd_buf);
    }
    av_freep(&bff->audio_scratch);
    av_freep(&bff->pending_audio);
    sws_freeContext(bff->sws);
    swr_free(&bff->swr);
    av_frame_free(&bff->display_frame);
    av_frame_free(&bff->decode_frame);
    av_frame_free(&bff->decode_audio_frame);
    av_packet_free(&bff->pkt);
    bink_ffmpeg_clear_queued_frames(bff);
    av_freep(&bff->queued_frames);
    if (bff->aud_ctx) avcodec_free_context(&bff->aud_ctx);
    if (bff->vid_ctx) avcodec_free_context(&bff->vid_ctx);
    if (bff->fmt_ctx) avformat_close_input(&bff->fmt_ctx);
    free(bff);
#endif
}

int BINKCALL BinkCopyToBuffer(HBINK bnk, void* dest, int destpitch, unsigned destheight, unsigned destx, unsigned desty, unsigned flags)
{
#ifdef _WIN32
    if (_BinkCopyToBuffer != NULL) {
        return _BinkCopyToBuffer(bnk, dest, destpitch, destheight, destx, desty, flags);
    }
#endif
#ifdef HAVE_FFMPEG
    {
        BinkFFmpeg* bff = (BinkFFmpeg*)bnk;
        uint8_t* dst_ptr;
        int dst_linesize[1];

        (void)destheight;
        (void)flags;

        if (!bff || !bff->vid_ctx) {
            return -1;
        }

        dst_ptr = (uint8_t*)dest + (int)desty * destpitch + (int)destx * 4;
        dst_linesize[0] = destpitch;

        if (bff->display_frame == NULL || bff->sws == NULL) {
            return -1;
        }

        /*
         * Output: AV_PIX_FMT_BGRA (4 bytes/pixel, [B,G,R,A] in memory).
         * On little-endian macOS/Linux SDL3 surfaces use SDL_PIXELFORMAT_ARGB8888
         * which has the same byte layout ([B,G,R,A] in memory), so the pixels
         * land correctly in the destination video buffer.
         */
        sws_scale(bff->sws,
            (const uint8_t* const*)bff->display_frame->data, bff->display_frame->linesize,
            0, bff->vid_ctx->height,
            &dst_ptr, dst_linesize);

        return 0;
    }
#endif
    return -1;
}

int BINKCALL BinkDDSurfaceType(void* lpDDS)
{
#ifdef _WIN32
    if (_BinkDDSurfaceType != NULL) {
        return _BinkDDSurfaceType(lpDDS);
    }
#endif
    return -1;
}

int BINKCALL BinkDoFrame(HBINK bnk)
{
#ifdef _WIN32
    if (_BinkDoFrame != NULL) {
        return _BinkDoFrame(bnk);
    }
#endif
#ifdef HAVE_FFMPEG
    BinkFFmpeg* bff = (BinkFFmpeg*)bnk;
    int ret;

    if (!bff) return -1;

    if (bff->precise_timing) {
        if (!bink_ffmpeg_ensure_video_frame(bff)) {
            return -1;
        }

        if (!bink_ffmpeg_dequeue_video_frame(bff)) {
            return -1;
        }

        return 0;
    }

    /* Drain any already-decoded video frame first. */
    ret = avcodec_receive_frame(bff->vid_ctx, bff->display_frame);
    if (ret == 0) {
        return 0;
    }

    /* Read packets until we get a decoded video frame (audio fed as we go). */
    while (1) {
        ret = av_read_frame(bff->fmt_ctx, bff->pkt);
        if (ret < 0) {
            /* EOF or error — flush decoders. */
            avcodec_send_packet(bff->vid_ctx, NULL);
            avcodec_receive_frame(bff->vid_ctx, bff->display_frame);
            if (bff->aud_ctx) {
                AVFrame* af = av_frame_alloc();
                avcodec_send_packet(bff->aud_ctx, NULL);
                while (af && avcodec_receive_frame(bff->aud_ctx, af) == 0) {
                    bink_ffmpeg_feed_audio(bff, af);
                }
                av_frame_free(&af);
            }
            return -1;
        }

        if (bff->pkt->stream_index == bff->vid_stream) {
            avcodec_send_packet(bff->vid_ctx, bff->pkt);
            av_packet_unref(bff->pkt);
            if (avcodec_receive_frame(bff->vid_ctx, bff->display_frame) == 0) {
                return 0;
            }
        } else if (bff->pkt->stream_index == bff->aud_stream && bff->aud_ctx) {
            bink_ffmpeg_decode_audio_packet(bff);
        } else {
            av_packet_unref(bff->pkt);
        }
    }
#endif
    return -1;
}

void BINKCALL BinkNextFrame(HBINK bnk)
{
#ifdef _WIN32
    if (_BinkNextFrame != NULL) {
        _BinkNextFrame(bnk);
        return;
    }
#endif
#ifdef HAVE_FFMPEG
    BinkFFmpeg* bff = (BinkFFmpeg*)bnk;
    if (bff) {
        bff->pub.FrameNum++;
        if (bff->precise_timing) {
            av_frame_free(&bff->display_frame);
            bff->current_frame_time_ns = -1;
            bink_ffmpeg_pump_audio(bff);
        }
    }
#endif
}

HBINK BINKCALL BinkOpen(const char* name, unsigned flags)
{
#ifdef _WIN32
    if (_BinkOpen != NULL) {
        return _BinkOpen(name, flags);
    }
#endif
#ifdef HAVE_FFMPEG
    if (!g_ffmpeg_backend_available) {
        return NULL;
    }

#ifdef _WIN32
    if (!bink_ffmpeg_load()) {
        g_ffmpeg_backend_available = false;
        return NULL;
    }
#endif
    return bink_ffmpeg_open_impl(name, flags);
#endif
    (void)name;
    (void)flags;
    return NULL;
}

BINKSNDOPEN BINKCALL BinkOpenMiles(void* param)
{
#ifdef _WIN32
    if (_BinkOpenMiles != NULL) {
        return _BinkOpenMiles(param);
    }
#endif
    return NULL;
}

int BINKCALL BinkSetSoundSystem(BINKSNDSYSOPEN open, void* param)
{
#ifdef _WIN32
    if (_BinkSetSoundSystem != NULL) {
        return _BinkSetSoundSystem(open, param);
    }
#endif
#ifdef HAVE_FFMPEG
    g_snd_sys_open = open;
    g_snd_sys_param = param;
    return 1;
#endif
    return -1;
}

void BINKCALL BinkSetSoundTrack(unsigned track)
{
#ifdef _WIN32
    if (_BinkSetSoundTrack != NULL) {
        _BinkSetSoundTrack(track);
        return;
    }
#endif
#ifdef HAVE_FFMPEG
    (void)track; /* no-op; track selection handled by BinkOpen flags */
#endif
}

int BINKCALL BinkWait(HBINK bnk)
{
#ifdef _WIN32
    if (_BinkWait != NULL) {
        return _BinkWait(bnk);
    }
#endif
#ifdef HAVE_FFMPEG
    /* Caller-side pacing handles timing on non-Windows. */
    (void)bnk;
    return 0;
#endif
    (void)bnk;
    return -1;
}

void BINKCALL BinkRewind(HBINK bnk)
{
#ifdef HAVE_FFMPEG
    BinkFFmpeg* bff = (BinkFFmpeg*)bnk;
    if (!bff || !bff->fmt_ctx) return;

    if (av_seek_frame(bff->fmt_ctx, bff->vid_stream,
            bff->vid_start_pts, AVSEEK_FLAG_BACKWARD) < 0) {
        fprintf(stderr, "bink_compat: BinkRewind seek failed\n");
    }
    avcodec_flush_buffers(bff->vid_ctx);
    if (bff->aud_ctx) {
        avcodec_flush_buffers(bff->aud_ctx);
        if (bff->swr) {
            /* Drain any latency-buffered samples so they don't bleed into
             * the next loop iteration's audio. */
            swr_convert(bff->swr, NULL, 0, NULL, 0);
        }
    }
    av_frame_free(&bff->display_frame);
    bff->display_frame = av_frame_alloc();
    av_frame_free(&bff->decode_frame);
    bff->decode_frame = av_frame_alloc();
    bff->current_frame_time_ns = -1;
    bff->demux_eof = false;
    bff->pending_audio_size = 0;
    bink_ffmpeg_clear_queued_frames(bff);
    bff->pub.FrameNum = 0;
#else
    (void)bnk;
#endif
}

bool bink_compat_get_frame_time_ns(HBINK bnk, int64_t* frame_time_ns)
{
#ifdef HAVE_FFMPEG
    BinkFFmpeg* bff = (BinkFFmpeg*)bnk;

    if (bff != NULL && bff->precise_timing && frame_time_ns != NULL) {
        *frame_time_ns = bff->current_frame_time_ns;
        return *frame_time_ns >= 0;
    }
#endif
    (void)bnk;
    (void)frame_time_ns;
    return false;
}

bool bink_compat_pump_audio(HBINK bnk)
{
#ifdef HAVE_FFMPEG
    BinkFFmpeg* bff = (BinkFFmpeg*)bnk;

    if (bff != NULL && bff->precise_timing) {
        return bink_ffmpeg_pump_audio(bff);
    }
#endif
    (void)bnk;
    return false;
}

int bink_compat_get_queued_video_frames(HBINK bnk)
{
#ifdef HAVE_FFMPEG
    BinkFFmpeg* bff = (BinkFFmpeg*)bnk;

    if (bff != NULL && bff->precise_timing) {
        return bff->queued_frame_count;
    }
#endif
    (void)bnk;
    return 0;
}

bool bink_compat_init(void)
{
#ifdef _WIN32
#ifndef HAVE_FFMPEG
    binkw32 = LoadLibraryA("binkw32.dll");
    if (binkw32 == NULL) {
        return false;
    }
#else
#ifndef _WIN64
    binkw32 = LoadLibraryA("binkw32.dll");
    if (binkw32 == NULL) {
        return true;
    }
#endif
#endif

    if (binkw32 != NULL) {
        _BinkClose = (BINKCLOSE)GetProcAddress(binkw32, "_BinkClose@4");
        _BinkCopyToBuffer = (BINKCOPYTOBUFFER)GetProcAddress(binkw32, "_BinkCopyToBuffer@28");
        _BinkDDSurfaceType = (BINKDDSURFACETYPE)GetProcAddress(binkw32, "_BinkDDSurfaceType@4");
        _BinkDoFrame = (BINKDOFRAME)GetProcAddress(binkw32, "_BinkDoFrame@4");
        _BinkNextFrame = (BINKNEXTFRAME)GetProcAddress(binkw32, "_BinkNextFrame@4");
        _BinkOpen = (BINKOPEN)GetProcAddress(binkw32, "_BinkOpen@8");
        _BinkOpenMiles = (BINKOPENMILES)GetProcAddress(binkw32, "_BinkOpenMiles@4");
        _BinkSetSoundSystem = (BINKSETSOUNDSYSTEM)GetProcAddress(binkw32, "_BinkSetSoundSystem@8");
        _BinkSetSoundTrack = (BINKSETSOUNDTRACK)GetProcAddress(binkw32, "_BinkSetSoundTrack@4");
        _BinkWait = (BINKWAIT)GetProcAddress(binkw32, "_BinkWait@4");

        if (_BinkClose == NULL
            || _BinkCopyToBuffer == NULL
            || _BinkDDSurfaceType == NULL
            || _BinkDoFrame == NULL
            || _BinkNextFrame == NULL
            || _BinkOpen == NULL
            || _BinkOpenMiles == NULL
            || _BinkSetSoundSystem == NULL
            || _BinkSetSoundTrack == NULL
            || _BinkWait == NULL) {
            bink_compat_exit();
#ifndef HAVE_FFMPEG
            return false;
#endif
        }
    }
#endif
    /* On non-Windows the FFmpeg backend needs no explicit init. */
    return true;
}

void bink_compat_exit(void)
{
#ifdef _WIN32
    if (binkw32 != NULL) {
        FreeLibrary(binkw32);
        binkw32 = NULL;

        _BinkClose = NULL;
        _BinkCopyToBuffer = NULL;
        _BinkDDSurfaceType = NULL;
        _BinkDoFrame = NULL;
        _BinkNextFrame = NULL;
        _BinkOpen = NULL;
        _BinkOpenMiles = NULL;
        _BinkSetSoundSystem = NULL;
        _BinkSetSoundTrack = NULL;
        _BinkWait = NULL;
    }
#ifdef HAVE_FFMPEG
    g_snd_sys_open = NULL;
    g_snd_sys_param = NULL;
    bink_ffmpeg_unload();
    g_ffmpeg_backend_available = true;
#endif
#elif defined(HAVE_FFMPEG)
    /* Non-Windows with FFmpeg: reset global sound-system pointer. */
    g_snd_sys_open = NULL;
    g_snd_sys_param = NULL;
    g_ffmpeg_backend_available = true;
#endif
}
