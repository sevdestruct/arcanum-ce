#include "ui/slide_ui.h"

#include <stdio.h>
#include <string.h>

#include "tig/bmp.h"
#include "tig/color.h"
#include "tig/memory.h"

#include "game/gamelib.h"
#include "game/gfade.h"
#include "game/gsound.h"
#include "game/hrp.h"
#include "game/mes.h"
#include "game/script.h"

/**
 * The maximum number of enqueued slides.
 */
#define MAX_QUEUE_SIZE 100

static void slide_ui_prepare(int type);
static bool slide_ui_do_slide(tig_window_handle_t window_handle, int type, int slide);
static bool slide_ui_get_assets(int slide, char* bmp_path, char* speech_path);
static bool slide_ui_get_custom_credits_bg_path(char* bmp_path);
static bool slide_ui_blit_custom_credits_slide(TigBmp* slide_bmp, const char* custom_bg_path, tig_window_handle_t window_handle);
static bool slide_ui_build_background_mask(TigBmp* slide_bmp, uint8_t* mask);
static uint32_t slide_ui_bmp_color_at(TigBmp* bmp, int x, int y);
static bool slide_ui_colors_match(uint32_t a, uint32_t b);
static void slide_ui_fade_out(void);
static void slide_ui_fade_in(void);
static void slide_ui_queue_clear(void);

/**
 * Current number of slides in the queue.
 *
 * 0x67B970
 */
static int slide_ui_queue_size;

/**
 * "slide.mes"
 *
 * 0x67B974
 */
static mes_file_handle_t slide_ui_slide_mes_file;

/**
 * Array storing the numbers of queued slides.
 *
 * 0x67B978
 */
static int slide_ui_queue[MAX_QUEUE_SIZE];

/**
 * Flag indicating if the slide UI is active.
 *
 * 0x67BB08
 */
static bool slide_ui_active;

/**
 * Called when a module is being loaded.
 *
 * 0x5695B0
 */
bool slide_ui_mod_load(void)
{
    mes_load("rules\\slide.mes", &slide_ui_slide_mes_file);
    return true;
}

/**
 * Called when a module is being unloaded.
 *
 * 0x5695D0
 */
void slide_ui_mod_unload(void)
{
    mes_unload(slide_ui_slide_mes_file);
    slide_ui_slide_mes_file = MES_FILE_HANDLE_INVALID;
}

/**
 * Checks if the slide UI is active.
 *
 * 0x5695F0
 */
bool slide_ui_is_active(void)
{
    return slide_ui_active;
}

/**
 * Starts slide UI.
 *
 * 0x569600
 */
void slide_ui_start(int type)
{
    TigWindowData window_data;
    tig_window_handle_t window_handle;
    tig_sound_handle_t sound_handle;
    int index;

    slide_ui_active = true;

    // Remove all enqeueued slides.
    slide_ui_queue_clear();

    // Run script associated with slide UI type.
    slide_ui_prepare(type);

    // From `slide.mes`: "Slide 0 is the DEFAULT death slide. If the player dies
    // and no slide is queued by script 998, then slide 0 is used."
    if (type == SLIDE_UI_TYPE_DEATH
        && slide_ui_queue_size == 0
        && slide_ui_slide_mes_file != MES_FILE_HANDLE_INVALID) {
        slide_ui_enqueue(0);
    }

    // Perform initial fade-out.
    slide_ui_fade_out();

    // Display slides if queue is not empty.
    if (slide_ui_queue_size > 0) {
        // Set up window properties.
        window_data.flags = TIG_WINDOW_ALWAYS_ON_TOP;
        window_data.rect.x = 0;
        window_data.rect.y = 0;
        window_data.rect.width = 800;
        window_data.rect.height = 600;
        window_data.background_color = 0;
        hrp_apply(&(window_data.rect), GRAVITY_CENTER_HORIZONTAL | GRAVITY_CENTER_VERTICAL);

        // Create the slide window.
        if (tig_window_create(&window_data, &window_handle) == TIG_OK) {
            gsound_stop_all(25);
            tig_mouse_hide();
            sound_handle = gsound_play_music_indefinitely("sound\\music\\DwarvenMusic.mp3", 25);

            // Present each slide in the queue.
            for (index = 0; index < slide_ui_queue_size; index++) {
                if (!slide_ui_do_slide(window_handle, type, slide_ui_queue[index])) {
                    // Escape was pressed - stop slideshow.
                    break;
                }
            }

            // Clean up resources.
            tig_sound_destroy(sound_handle);
            tig_window_destroy(window_handle);

            tig_mouse_show();
            tig_window_display();
        }
    }

    // Perform fade-in when we're presenting credits. Otherwise the caller is
    // responsible to remove fading.
    if (type == SLIDE_UI_TYPE_CREDITS) {
        slide_ui_fade_in();
    }

    slide_ui_active = false;
}

/**
 * Configures the slide UI by executing a script for the given type.
 *
 * The script is assumed to call `slide_ui_enqueue` via appropriate action, up
 * to `MAX_QUEUE_SIZE` number of times.
 *
 * 0x569720
 */
void slide_ui_prepare(int type)
{
    Script scr;
    ScriptInvocation invocation;

    scr.num = dword_5A5700[type];

    invocation.script = &scr;
    invocation.attachment_point = SAP_USE;
    invocation.triggerer_obj = OBJ_HANDLE_NULL;
    invocation.attachee_obj = OBJ_HANDLE_NULL;
    invocation.extra_obj = OBJ_HANDLE_NULL;
    invocation.line = 0;
    script_execute(&invocation);
}

/**
 * Performs the display of a single slide.
 *
 * Returns `true` if slideshow show continue, `false` if slide was interrupted
 * by pressing ESC.
 *
 * 0x569770
 */
bool slide_ui_do_slide(tig_window_handle_t window_handle, int type, int slide)
{
    TigMessage msg;
    TigRect rect;
    TigBmp bmp;
    char bmp_path[TIG_MAX_PATH];
    char custom_bg_path[TIG_MAX_PATH];
    char speech_path[TIG_MAX_PATH];
    tig_sound_handle_t speech_handle;
    bool cont = true;
    bool stop;
    bool custom_override;
    tig_timestamp_t start;
    tig_duration_t elapsed;

    // Set slide rect.
    rect.x = 0;
    rect.y = 0;
    rect.width = 800;
    rect.height = 600;

    if (slide_ui_get_assets(slide, bmp_path, speech_path)) {
        strcpy(bmp.name, bmp_path);
        custom_override = type == SLIDE_UI_TYPE_CREDITS
            && slide_ui_get_custom_credits_bg_path(custom_bg_path);

        // Load image.
        if (tig_bmp_create(&bmp) == TIG_OK) {
            // Put image into window.
            if (custom_override) {
                if (!slide_ui_blit_custom_credits_slide(&bmp, custom_bg_path, window_handle)) {
                    TigRect dst_rect = rect;
                    tig_bmp_copy_to_window(&bmp, &rect, window_handle, &dst_rect);
                }
            } else {
                tig_bmp_copy_to_window(&bmp, &rect, window_handle, &rect);
            }
            tig_bmp_destroy(&bmp);

            // Refresh screen.
            sub_51E850(window_handle);
            tig_window_display();

            // Perform fade-in effect.
            slide_ui_fade_in();

            // Play voiceover.
            speech_handle = gsound_play_voice(speech_path, 0);

            // Clear initial message queue.
            while (tig_message_dequeue(&msg) == TIG_OK) {
                if (msg.type == TIG_MESSAGE_REDRAW) {
                    gamelib_redraw();
                }
            }

            // NOTE: There is an interesting bug related to the slideshow.
            // Occasionally, the slides are played in rapid succession. This
            // happens because the underlying `tig_sound_play_streamed_once`
            // does not start the speech immediately. Instead, it marks the
            // speech handle as active and ready to fade in (even with a fade
            // duration of 0). If the host drains the message queue (below in
            // the loop) too quickly, it may not trigger `tig_sound_update`,
            // which is responsible for starting the stream. Consequently, by
            // the time the iteration completes, the stream may not have started
            // at all, causing `tig_sound_is_playing` to report `false` and
            // immediately ending the loop.
            //
            // The fix is adapted from ToEE, where this function has been moved
            // to TIG as `tig_movie_play_slide`. The solution involves adding a
            // minimum delay of 3 seconds per slide. This delay not only serves
            // as a fallback in case the voiceover is missing for any reason,
            // but it also gives the sound system enough time to start the
            // voiceover (which is typically longer than 3 seconds).
            tig_timer_now(&start);

            stop = false;
            do {
                elapsed = tig_timer_elapsed(start);

                // Pump events.
                tig_ping();

                // Process input.
                while (tig_message_dequeue(&msg) == TIG_OK) {
                    if (msg.type == TIG_MESSAGE_KEYBOARD) {
                        if (msg.data.keyboard.pressed == false) {
                            stop = true;
                            if (msg.data.keyboard.scancode == SDL_SCANCODE_ESCAPE) {
                                // Interrupt slideshow.
                                cont = false;
                            }
                        }
                    } else if (msg.type == TIG_MESSAGE_MOUSE) {
                        if (msg.data.mouse.event == TIG_MESSAGE_MOUSE_LEFT_BUTTON_UP
                            || msg.data.mouse.event == TIG_MESSAGE_MOUSE_RIGHT_BUTTON_UP
                            || msg.data.mouse.event == TIG_MESSAGE_MOUSE_MIDDLE_BUTTON_UP) {
                            // Interrupt this slide.
                            stop = true;
                        }
                    } else if (msg.type == TIG_MESSAGE_REDRAW) {
                        gamelib_redraw();
                    }
                }

                if (stop) {
                    break;
                }
            } while ((speech_handle != TIG_SOUND_HANDLE_INVALID && tig_sound_is_playing(speech_handle)) || elapsed < 3000);

            // Clean up voiceover.
            tig_sound_destroy(speech_handle);
        }

        // Clear remaining message queue.
        while (tig_message_dequeue(&msg) == TIG_OK) {
            if (msg.type == TIG_MESSAGE_REDRAW) {
                gamelib_redraw();
            }
        }

        // Perform fade-out effect.
        slide_ui_fade_out();
    }

    return cont;
}

static bool slide_ui_get_custom_credits_bg_path(char* bmp_path)
{
    FILE* stream;

    stream = fopen("data/art/ui/credits_bg.bmp", "rb");
    if (stream != NULL) {
        fclose(stream);
        strcpy(bmp_path, "art\\ui\\credits_bg.bmp");
        return true;
    }

    stream = fopen("data/art/ui/mainmenu_bg.bmp", "rb");
    if (stream != NULL) {
        fclose(stream);
        strcpy(bmp_path, "art\\ui\\mainmenu_bg.bmp");
        return true;
    }

    return false;
}

static bool slide_ui_blit_custom_credits_slide(TigBmp* slide_bmp, const char* custom_bg_path, tig_window_handle_t window_handle)
{
    TigWindowData window_data;
    TigBmp custom_bg_bmp;
    TigBmp composite_bmp;
    TigRect src_rect;
    TigRect dst_rect;
    int sw;
    int sh;
    int bmp_ox;
    int bmp_oy;
    int src_x;
    int src_y;
    int x;
    int y;
    uint8_t* background_mask;

    strcpy(custom_bg_bmp.name, custom_bg_path);
    if (tig_bmp_create(&custom_bg_bmp) != TIG_OK) {
        return false;
    }

    if (tig_window_data(window_handle, &window_data) != TIG_OK) {
        tig_bmp_destroy(&custom_bg_bmp);
        return false;
    }

    sw = hrp_iso_window_width_get();
    sh = hrp_iso_window_height_get();
    bmp_ox = (sw - custom_bg_bmp.width) / 2;
    bmp_oy = (sh - custom_bg_bmp.height) / 2;
    src_x = window_data.rect.x - bmp_ox;
    src_y = window_data.rect.y - bmp_oy;

    if (src_x < 0
        || src_y < 0
        || src_x + window_data.rect.width > custom_bg_bmp.width
        || src_y + window_data.rect.height > custom_bg_bmp.height
        || slide_bmp->width < window_data.rect.width
        || slide_bmp->height < window_data.rect.height) {
        tig_bmp_destroy(&custom_bg_bmp);
        return false;
    }

    background_mask = MALLOC(window_data.rect.width * window_data.rect.height);
    if (background_mask == NULL) {
        tig_bmp_destroy(&custom_bg_bmp);
        return false;
    }

    if (!slide_ui_build_background_mask(slide_bmp, background_mask)) {
        FREE(background_mask);
        tig_bmp_destroy(&custom_bg_bmp);
        return false;
    }

    composite_bmp.bpp = 24;
    composite_bmp.width = window_data.rect.width;
    composite_bmp.height = window_data.rect.height;
    composite_bmp.pitch = composite_bmp.width * 3;
    composite_bmp.pixels = MALLOC(composite_bmp.pitch * composite_bmp.height);
    if (composite_bmp.pixels == NULL) {
        FREE(background_mask);
        tig_bmp_destroy(&custom_bg_bmp);
        return false;
    }

    for (y = 0; y < composite_bmp.height; y++) {
        uint8_t* dst = (uint8_t*)composite_bmp.pixels + y * composite_bmp.pitch;
        for (x = 0; x < composite_bmp.width; x++) {
            uint32_t slide_color = slide_ui_bmp_color_at(slide_bmp, x, y);
            uint32_t bg_color = slide_ui_bmp_color_at(&custom_bg_bmp, src_x + x, src_y + y);
            uint32_t out_color;

            if (background_mask[y * composite_bmp.width + x] != 0) {
                out_color = bg_color;
            } else {
                out_color = slide_color;
            }

            dst[0] = (uint8_t)(out_color & 0xFF);
            dst[1] = (uint8_t)((out_color >> 8) & 0xFF);
            dst[2] = (uint8_t)((out_color >> 16) & 0xFF);
            dst += 3;
        }
    }

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = composite_bmp.width;
    src_rect.height = composite_bmp.height;
    dst_rect = src_rect;
    tig_bmp_copy_to_window(&composite_bmp, &src_rect, window_handle, &dst_rect);

    FREE(background_mask);
    tig_bmp_destroy(&custom_bg_bmp);
    tig_bmp_destroy(&composite_bmp);
    return true;
}

static bool slide_ui_build_background_mask(TigBmp* slide_bmp, uint8_t* mask)
{
    int width = slide_bmp->width;
    int height = slide_bmp->height;
    int queue_size = width * height;
    int* queue;
    int head = 0;
    int tail = 0;
    static const int offsets[4][2] = {
        { 1, 0 },
        { -1, 0 },
        { 0, 1 },
        { 0, -1 },
    };
    int x;
    int y;
    int i;

    memset(mask, 0, width * height);

    queue = MALLOC(sizeof(*queue) * queue_size);
    if (queue == NULL) {
        return false;
    }

    for (x = 0; x < width; x++) {
        mask[x] = 1;
        queue[tail++] = x;

        if (height > 1) {
            mask[(height - 1) * width + x] = 1;
            queue[tail++] = (height - 1) * width + x;
        }
    }

    for (y = 1; y < height - 1; y++) {
        mask[y * width] = 1;
        queue[tail++] = y * width;

        if (width > 1) {
            mask[y * width + (width - 1)] = 1;
            queue[tail++] = y * width + (width - 1);
        }
    }

    while (head < tail) {
        int index = queue[head++];
        int curr_x = index % width;
        int curr_y = index / width;
        uint32_t base_color = slide_ui_bmp_color_at(slide_bmp, curr_x, curr_y);

        for (i = 0; i < 4; i++) {
            int nx = curr_x + offsets[i][0];
            int ny = curr_y + offsets[i][1];
            int nindex;

            if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
                continue;
            }

            nindex = ny * width + nx;
            if (mask[nindex] != 0) {
                continue;
            }

            if (!slide_ui_colors_match(base_color, slide_ui_bmp_color_at(slide_bmp, nx, ny))) {
                continue;
            }

            mask[nindex] = 1;
            queue[tail++] = nindex;
        }
    }

    FREE(queue);
    return true;
}

static uint32_t slide_ui_bmp_color_at(TigBmp* bmp, int x, int y)
{
    if (bmp->bpp == 8) {
        uint8_t index = *((uint8_t*)bmp->pixels + y * bmp->pitch + x);
        return bmp->palette[index];
    }

    if (bmp->bpp == 24) {
        uint8_t* src = (uint8_t*)bmp->pixels + y * bmp->pitch + x * 3;
        return tig_color_make(src[2], src[1], src[0]);
    }

    return 0;
}

static bool slide_ui_colors_match(uint32_t a, uint32_t b)
{
    int ar = (a >> 16) & 0xFF;
    int ag = (a >> 8) & 0xFF;
    int ab = a & 0xFF;
    int br = (b >> 16) & 0xFF;
    int bg = (b >> 8) & 0xFF;
    int bb = b & 0xFF;

    return abs(ar - br) <= 10
        && abs(ag - bg) <= 10
        && abs(ab - bb) <= 10;
}

/**
 * Retrieves asset paths for a slide from the message file.
 *
 * 0x569930
 */
bool slide_ui_get_assets(int slide, char* bmp_path, char* speech_path)
{
    MesFileEntry mes_file_entry;
    char temp[MAX_STRING];
    char* tok;

    // Retrieve message file entry.
    mes_file_entry.num = slide;
    if (!mes_search(slide_ui_slide_mes_file, &mes_file_entry)) {
        return false;
    }

    // Each slide contains BMP file name and MP3 voiceover file name. Each piece
    // is mandatory.
    strcpy(temp, mes_file_entry.str);

    // Build BMP path.
    tok = strtok(temp, " \t\n");
    if (tok == NULL) {
        return false;
    }
    sprintf(bmp_path, "slide\\%s", tok);

    // Build MP3 path.
    tok = strtok(NULL, " \t\n");
    if (tok == NULL) {
        return false;
    }
    sprintf(speech_path, "sound\\speech\\slide\\%s", tok);

    return true;
}

/**
 * Performs a fade-out effect for the slide UI.
 *
 * 0x5699F0
 */
void slide_ui_fade_out(void)
{
    FadeData fade_data;

    fade_data.flags = 0;
    fade_data.duration = 2.0f;
    fade_data.steps = 48;
    fade_data.color = tig_color_make(0, 0, 0);
    gfade_run(&fade_data);
}

/**
 * Performs a fade-in effect for the slide UI.
 *
 * 0x569A60
 */
void slide_ui_fade_in(void)
{
    FadeData fade_data;

    fade_data.flags = FADE_IN;
    fade_data.duration = 2.0f;
    fade_data.steps = 48;
    gfade_run(&fade_data);
}

/**
 * Clears the slide queue.
 *
 * 0x569A90
 */
void slide_ui_queue_clear(void)
{
    slide_ui_queue_size = 0;
}

/**
 * Queues a slide for display.
 *
 * 0x569AA0
 */
void slide_ui_enqueue(int slide)
{
    if (slide_ui_queue_size < MAX_QUEUE_SIZE) {
        slide_ui_queue[slide_ui_queue_size++] = slide;
    }
}
