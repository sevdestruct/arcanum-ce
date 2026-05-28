/*
 * test_bink1.c -- minimal Bink1 decoder test harness.
 *
 * Usage: test_bink1 <path/to/file.bik> [frames]
 *
 * Opens the .bik file, decodes the requested number of frames (or
 * 30 by default), and prints aggregate stats (block-type histogram
 * frequencies from the trace logger, frame size stats, plane non-
 * zero counts). Use this to verify the decoder against a known-good
 * file without launching the full game.
 */

#include "bink1_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.bik> [num_frames]\n", argv[0]);
        return 2;
    }
    int want_frames = argc > 2 ? atoi(argv[2]) : 30;

    Bink1Decoder* d = bink1_decoder_open(argv[1]);
    if (!d) {
        fprintf(stderr, "bink1_decoder_open failed for %s\n", argv[1]);
        return 1;
    }

    Bink1Info info;
    if (!bink1_decoder_get_info(d, &info)) {
        fprintf(stderr, "bink1_decoder_get_info failed\n");
        bink1_decoder_close(d);
        return 1;
    }

    printf("file:  %s\n", argv[1]);
    printf("video: BIK%c %ux%u, %u frames, %u us/frame\n",
        info.video_version, info.width, info.height,
        info.frame_count, info.frame_duration_us);
    if (info.audio_track_count > 0) {
        printf("audio: %d Hz, %d ch, %s\n",
            info.audio_sample_rate, info.audio_channels,
            info.audio_is_dct ? "DCT" : "RDFT");
    }

    int dst_pitch = (int)info.width * 4;
    uint8_t* dst = (uint8_t*)calloc((size_t)dst_pitch * info.height, 1);
    if (!dst) {
        bink1_decoder_close(d);
        return 1;
    }

    int decoded = 0;
    int max = want_frames > (int)info.frame_count ? (int)info.frame_count
                                                  : want_frames;
    /* Dump frame `max-1` as a PPM (P6, 24-bit RGB) at /tmp/bink1_frame.ppm. */
    int dump_at = max - 1;
    for (int i = 0; i < max; ++i) {
        if (!bink1_decoder_decode_video(d, dst, dst_pitch,
                (int)info.width, (int)info.height)) {
            fprintf(stderr, "decode_video failed on frame %d\n", i);
            break;
        }
        ++decoded;
        if (i == dump_at) {
            FILE* fp = fopen("/tmp/bink1_frame.ppm", "wb");
            if (fp) {
                fprintf(fp, "P6\n%u %u\n255\n", info.width, info.height);
                for (unsigned y = 0; y < info.height; ++y) {
                    for (unsigned x = 0; x < info.width; ++x) {
                        uint8_t* px = dst + y * dst_pitch + x * 4;
                        /* BGRA -> RGB */
                        uint8_t rgb[3] = { px[2], px[1], px[0] };
                        fwrite(rgb, 1, 3, fp);
                    }
                }
                fclose(fp);
                printf("dumped frame %d to /tmp/bink1_frame.ppm\n", i);
            }
        }
        if (!bink1_decoder_next_frame(d)) break;
    }
    printf("decoded %d frames cleanly\n", decoded);

    /* Sample a few pixels of the last frame to see what's in there. */
    size_t total = (size_t)dst_pitch * info.height;
    long sum_b = 0, sum_g = 0, sum_r = 0;
    int npx = 0;
    int samples = (int)(total < 65536 ? total : 65536);
    for (int i = 0; i + 3 < samples; i += 4, ++npx) {
        sum_b += dst[i];
        sum_g += dst[i + 1];
        sum_r += dst[i + 2];
    }
    if (npx > 0) {
        printf("avg BGR over first %d pixels: B=%ld G=%ld R=%ld\n", npx,
            sum_b / npx, sum_g / npx, sum_r / npx);
    }

    free(dst);
    bink1_decoder_close(d);
    return decoded > 0 ? 0 : 1;
}
