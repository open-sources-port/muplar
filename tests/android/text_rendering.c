// Build with the Android NDK compiler and -ldl -lm, then run with elfuse
// against an Android sysroot, as for message_queue_wait.c.
#include <assert.h>
#include "../../platform/android-aarch64/art-shim/muplar_android_art_shim.c"

static void *released_pointer;

static void record_release(void *pointer)
{
    released_pointer = pointer;
}

static int drawn_width(struct muplar_bitmap_state *bitmap)
{
    int left = bitmap->width;
    int right = -1;
    for (int y = 0; y < bitmap->height; ++y) {
        for (int x = 0; x < bitmap->width; ++x) {
            if (bitmap->pixels[y * bitmap->width + x] >> 24) {
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
    }
    return right >= left ? right - left + 1 : 0;
}

int main(void)
{
    struct muplar_bitmap_state *bitmap = muplar_alloc_bitmap(400, 120);
    struct muplar_canvas_state *canvas = muplar_alloc_canvas(bitmap->token);
    jlong paint = Java_android_graphics_Paint_nInit(NULL, NULL);
    Java_android_graphics_Paint_nSetTextSize(paint, 16);
    Java_android_graphics_Paint_nSetTextScaleX(paint, 1);
    Java_android_graphics_Paint_nSetTextSkewX(paint, 0);
    Java_android_graphics_Paint_nSetLetterSpacing(paint, 0);
    Java_android_graphics_Paint_nSetWordSpacing(paint, 0);
    assert(Java_android_graphics_Paint_nGetTextSize(paint) == 16);
    assert(Java_android_graphics_Paint_nGetTextScaleX(paint) == 1);
    assert(Java_android_graphics_Paint_nGetWordSpacing(paint) == 0);
    Java_android_graphics_Paint_nSetTextAlign(paint, 1);
    assert(Java_android_graphics_Paint_nGetTextAlign(paint) == 1);
    Java_android_graphics_Paint_nSetTextAlign(paint, 0);
    int line_height = Java_android_graphics_Paint_nGetFontMetricsIntWithFallback(
        NULL, NULL, paint, NULL, JNI_FALSE);
    assert(line_height > 0);
    float advance = muplar_character_advance(paint, 'M');
    muplar_draw_text(NULL, NULL, canvas->token, "Menu", 4, 10, 60, paint);
    int small_width = drawn_width(bitmap);
    assert(small_width > 10);

    memset(bitmap->pixels, 0, 400 * 120 * sizeof(uint32_t));
    Java_android_graphics_Paint_nSetTextSize(paint, 48);
    assert(muplar_character_advance(paint, 'M') == advance * 3);
    assert(Java_android_graphics_Paint_nGetFontMetricsIntWithFallback(
        NULL, NULL, paint, NULL, JNI_TRUE) == line_height * 3);
    muplar_draw_text(NULL, NULL, canvas->token, "Menu", 4, 10, 60, paint);
    int large_width = drawn_width(bitmap);
    assert(large_width > small_width * 2 && large_width < small_width * 4);

    jlong copy = Java_android_graphics_Paint_nInitWithPaint(NULL, NULL, paint);
    assert(Java_android_graphics_Paint_nGetTextSize(copy) == 48);
    Java_android_graphics_Paint_nSetLetterSpacing(copy, 0.1f);
    assert(Java_android_graphics_Paint_nGetTextSize(copy) == 48);
    assert(Java_android_graphics_Paint_nGetLetterSpacing(copy) == 0.1f);

    memset(bitmap->pixels, 0, 400 * 120 * sizeof(uint32_t));
    Java_android_graphics_Paint_nSetAlpha(paint, 0);
    muplar_draw_text(NULL, NULL, canvas->token, "Menu", 4, 10, 60, paint);
    assert(drawn_width(bitmap) == 0);

    Java_android_graphics_Paint_nSetAlpha(paint, 255);
    Java_android_graphics_Canvas_nDrawRoundRect(
        NULL, NULL, canvas->token, 10, 10, 110, 60, 20, 20, paint);
    assert(bitmap->pixels[10 * 400 + 10] == 0);
    assert(bitmap->pixels[35 * 400 + 60] >> 24);
    // NativeAllocationRegistry invokes these callbacks when text layouts are
    // collected. Synthetic handles still require a callable release function.
    jlong releases[] = {
        Java_android_content_res_AssetManager_nativeGetThemeFreeFunction(NULL, NULL),
        Java_android_graphics_Typeface_nativeGetReleaseFunc(),
        Java_android_graphics_text_LineBreaker_nGetReleaseFunc(NULL, NULL),
        Java_android_graphics_text_LineBreaker_nGetReleaseResultFunc(NULL, NULL),
        Java_android_graphics_text_MeasuredText_nGetReleaseFunc(NULL, NULL)
    };
    for (size_t i = 0; i < sizeof(releases) / sizeof(releases[0]); ++i) {
        assert(releases[i] != 0);
        ((void (*)(void *)) (uintptr_t) releases[i])((void *) 0x100);
        muplar_apply_free_function(NULL, NULL, releases[i], 0x100);
    }
    muplar_apply_free_function(NULL, NULL, 0, 0x100);
    muplar_apply_free_function(NULL, NULL,
        (jlong) (uintptr_t) record_release, 0x1234);
    assert(released_pointer == (void *) 0x1234);
    puts("text rendering and text allocation release callbacks passed");
    return 0;
}
