/*
 * bink1_verify.c -- standalone Bink1 decode verification tool.
 *
 * Decodes a .bik file through the native Bink1 decoder without
 * launching the game, so you can confirm a freshly-encoded file
 * actually plays before wiring it into Arcanum. Handy when producing
 * .bik files with RAD Video Tools (which defaults to Bink2 -- this
 * tool detects that and tells you).
 *
 * Usage:
 *   bink1_verify <file.bik> [num_frames]
 *
 * Environment:
 *   BINK_VERIFY_PPM=path   dump the last decoded frame as a PPM (P6)
 *   BINK_VERIFY_PCM=path   dump decoded audio as interleaved s16 PCM
 *
 * Exit codes:
 *   0  all requested frames decoded
 *   1  opened but decoding failed / produced no frames
 *   2  could not open (not a Bink1 stream -- e.g. Bink2/KB2, or I/O)
 *
 * Build (from the project root, via CMake):
 *   cmake --build --preset <preset> --target bink1_verify
 */

#include "bink1_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Peek the first 4 bytes so we can give a precise diagnostic when the
 * decoder refuses a file (the most common cause being a Bink2 stream
 * produced by RAD Video Tools' default settings). */
static void report_magic(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "  (could not open '%s' for reading)\n", path);
        return;
    }
    unsigned char m[4] = { 0 };
    size_t n = fread(m, 1, 4, f);
    fclose(f);
    if (n < 4) {
        fprintf(stderr, "  (file too short to contain a Bink header)\n");
        return;
    }
    fprintf(stderr, "  magic: %02X %02X %02X %02X  '%c%c%c%c'\n",
        m[0], m[1], m[2], m[3],
        (m[0] >= 32 && m[0] < 127) ? m[0] : '.',
        (m[1] >= 32 && m[1] < 127) ? m[1] : '.',
        (m[2] >= 32 && m[2] < 127) ? m[2] : '.',
        (m[3] >= 32 && m[3] < 127) ? m[3] : '.');
    if (m[0] == 'K' && m[1] == 'B' && m[2] == '2') {
        fprintf(stderr,
            "  -> This is a Bink 2 file. The native decoder only\n"
            "     supports Bink 1. Re-encode and pick 'Bink 1' under\n"
            "     RAD Video Tools' File format options.\n");
    } else if (m[0] == 'B' && m[1] == 'I' && m[2] == 'K') {
        fprintf(stderr,
            "  -> Bink 1 magic looks valid (BIK%c); open failed for\n"
            "     another reason (truncated file or I/O error).\n", m[3]);
    } else {
        fprintf(stderr, "  -> Not a Bink stream (no BIK/KB2 magic).\n");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.bik> [num_frames]\n", argv[0]);
        return 2;
    }
    const char* path = argv[1];
    int want_frames = argc > 2 ? atoi(argv[2]) : 30;
    if (want_frames < 1) want_frames = 1;

    Bink1Decoder* d = bink1_decoder_open(path);
    if (!d) {
        fprintf(stderr, "FAIL: could not open '%s' as a Bink1 stream.\n", path);
        report_magic(path);
        return 2;
    }

    Bink1Info info;
    if (!bink1_decoder_get_info(d, &info)) {
        fprintf(stderr, "FAIL: bink1_decoder_get_info failed\n");
        bink1_decoder_close(d);
        return 1;
    }

    printf("file:  %s\n", path);
    printf("video: BIK%c %ux%u, %u frames, %u us/frame\n",
        info.video_version, info.width, info.height,
        info.frame_count, info.frame_duration_us);
    if (info.audio_track_count > 0) {
        printf("audio: %d Hz, %d ch, %s\n",
            info.audio_sample_rate, info.audio_channels,
            info.audio_is_dct ? "DCT" : "RDFT");
    } else {
        printf("audio: none\n");
    }

    int dst_pitch = (int)info.width * 4;
    uint8_t* dst = (uint8_t*)calloc((size_t)dst_pitch * info.height, 1);
    if (!dst) {
        bink1_decoder_close(d);
        return 1;
    }

    const char* ppm_path = getenv("BINK_VERIFY_PPM");
    const char* pcm_path = getenv("BINK_VERIFY_PCM");
    FILE* pcm = pcm_path ? fopen(pcm_path, "wb") : NULL;
    size_t max_audio = bink1_decoder_max_audio_bytes(d);
    uint8_t* abuf = max_audio ? (uint8_t*)malloc(max_audio) : NULL;

    int decoded = 0;
    int max = want_frames > (int)info.frame_count ? (int)info.frame_count
                                                  : want_frames;
    int dump_at = max - 1;
    for (int i = 0; i < max; ++i) {
        if (!bink1_decoder_decode_video(d, dst, dst_pitch,
                (int)info.width, (int)info.height)) {
            fprintf(stderr, "FAIL: decode_video failed on frame %d\n", i);
            break;
        }
        ++decoded;
        if (ppm_path && i == dump_at) {
            FILE* fp = fopen(ppm_path, "wb");
            if (fp) {
                fprintf(fp, "P6\n%u %u\n255\n", info.width, info.height);
                for (unsigned y = 0; y < info.height; ++y) {
                    for (unsigned x = 0; x < info.width; ++x) {
                        uint8_t* px = dst + y * dst_pitch + x * 4;
                        uint8_t rgb[3] = { px[2], px[1], px[0] }; /* BGRA->RGB */
                        fwrite(rgb, 1, 3, fp);
                    }
                }
                fclose(fp);
                printf("dumped frame %d to %s\n", i, ppm_path);
            }
        }
        if (pcm && abuf) {
            size_t ab = 0;
            if (bink1_decoder_decode_audio(d, abuf, max_audio, &ab) && ab) {
                fwrite(abuf, 1, ab, pcm);
            }
        }
        if (!bink1_decoder_next_frame(d)) break;
    }
    if (pcm) { fclose(pcm); printf("dumped audio to %s\n", pcm_path); }

    /* Aggregate the last frame's average colour: an all-zero or wildly
     * skewed average is a quick smell test for a broken decode. */
    long sum_b = 0, sum_g = 0, sum_r = 0;
    int npx = 0;
    size_t total = (size_t)dst_pitch * info.height;
    int budget = (int)(total < 65536 ? total : 65536);
    for (int i = 0; i + 3 < budget; i += 4, ++npx) {
        sum_b += dst[i];
        sum_g += dst[i + 1];
        sum_r += dst[i + 2];
    }
    if (npx > 0) {
        printf("last frame avg BGR (first %d px): B=%ld G=%ld R=%ld\n",
            npx, sum_b / npx, sum_g / npx, sum_r / npx);
    }

    free(abuf);
    free(dst);
    bink1_decoder_close(d);

    if (decoded == max) {
        printf("PASS: decoded all %d requested frame(s) cleanly.\n", decoded);
        return 0;
    }
    printf("PARTIAL: decoded %d of %d requested frame(s).\n", decoded, max);
    return 1;
}
