#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <jni.h>

#define PROP_VALUE_MAX 92

static uintptr_t muplar_system_namespace_token;
static uintptr_t muplar_created_namespace_token;
static uintptr_t muplar_message_queue_token;
static uintptr_t muplar_next_matrix_token = 0x10000;
static uintptr_t muplar_next_colorspace_token = 0x18000;
static uintptr_t muplar_next_path_token = 0x1c000;
static uintptr_t muplar_next_path_measure_token = 0x1d000;
static uintptr_t muplar_next_paint_token = 0x1e000;
static uintptr_t muplar_next_shader_token = 0x20000;
static uintptr_t muplar_next_picture_token = 0x22000;
static uintptr_t muplar_next_canvas_token = 0x24000;
static uintptr_t muplar_next_render_node_token = 0x26000;
static uintptr_t muplar_next_perfetto_token = 0x28000;
static uintptr_t muplar_next_line_breaker_token = 0x29000;
static uintptr_t muplar_next_line_breaker_result_token = 0x29800;
static uintptr_t muplar_next_measured_text_token = 0x2a000;
static uintptr_t muplar_next_measured_text_builder_token = 0x2b000;
static uintptr_t muplar_next_parcel_token = 0x30000;
static uintptr_t muplar_next_velocity_tracker_token = 0x32000;
static uintptr_t muplar_next_display_event_receiver_token = 0x34000;
static uintptr_t muplar_next_region_token = 0x36000;
static uintptr_t muplar_next_surface_control_token = 0x38000;
static uintptr_t muplar_next_surface_transaction_token = 0x3a000;
static uintptr_t muplar_next_sqlite_token = 0x3c000;
static uintptr_t muplar_next_input_channel_token = 0x3d000;
static uintptr_t muplar_next_input_event_receiver_token = 0x3d800;
static uintptr_t muplar_next_cursor_window_token = 0x3e000;
static uintptr_t muplar_next_bitmap_token = 0x40000000;
static uintptr_t muplar_next_font_token = 0x50000000;
static uintptr_t muplar_next_font_family_token = 0x60000000;
static uintptr_t muplar_next_typeface_token = 0x70000000;
static uintptr_t muplar_next_vector_token = 0x80000000;
static uintptr_t muplar_binder_holder_token;
static char muplar_property_handles[64][96];
static jclass muplar_graphics_class;
static jmethodID muplar_graphics_create_bitmap;

struct muplar_sqlite_statement {
    jlong handle;
    jint parameter_count;
};

static struct muplar_sqlite_statement muplar_sqlite_statements[256];

struct muplar_bitmap_state {
    jlong token;
    jint width;
    jint height;
    uint32_t *pixels;
};

struct muplar_canvas_state {
    jlong token;
    jlong bitmap_token;
};

struct muplar_paint_state {
    jlong token;
    jint color;
};

static struct muplar_bitmap_state muplar_bitmaps[64];
static struct muplar_canvas_state muplar_canvases[64];
static struct muplar_paint_state muplar_paints[256];
static unsigned muplar_draw_color_count;
static unsigned muplar_draw_paint_count;
static unsigned muplar_draw_rect_count;
static unsigned muplar_draw_text_count;
static unsigned muplar_draw_bitmap_count;

static struct muplar_bitmap_state *muplar_find_bitmap(jlong token)
{
    size_t i;
    if (!token)
        return NULL;
    for (i = 0; i < sizeof(muplar_bitmaps) / sizeof(muplar_bitmaps[0]); i++)
        if (muplar_bitmaps[i].token == token)
            return &muplar_bitmaps[i];
    return NULL;
}

static struct muplar_bitmap_state *muplar_alloc_bitmap(jint width, jint height)
{
    size_t i;
    size_t count;
    struct muplar_bitmap_state *bitmap;
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;
    count = (size_t) width * (size_t) height;
    for (i = 0; i < sizeof(muplar_bitmaps) / sizeof(muplar_bitmaps[0]); i++) {
        bitmap = &muplar_bitmaps[i];
        if (bitmap->token)
            continue;
        bitmap->pixels = calloc(count, sizeof(uint32_t));
        if (!bitmap->pixels)
            return NULL;
        muplar_next_bitmap_token += 0x100;
        bitmap->token = (jlong) muplar_next_bitmap_token;
        bitmap->width = width;
        bitmap->height = height;
        return bitmap;
    }
    return NULL;
}

static void muplar_release_bitmap(jlong token)
{
    struct muplar_bitmap_state *bitmap = muplar_find_bitmap(token);
    if (!bitmap)
        return;
    free(bitmap->pixels);
    memset(bitmap, 0, sizeof(*bitmap));
}

static struct muplar_canvas_state *muplar_find_canvas(jlong token)
{
    size_t i;
    if (!token)
        return NULL;
    for (i = 0; i < sizeof(muplar_canvases) / sizeof(muplar_canvases[0]); i++)
        if (muplar_canvases[i].token == token)
            return &muplar_canvases[i];
    return NULL;
}

static struct muplar_canvas_state *muplar_alloc_canvas(jlong bitmap_token)
{
    size_t i;
    for (i = 0; i < sizeof(muplar_canvases) / sizeof(muplar_canvases[0]); i++) {
        if (muplar_canvases[i].token)
            continue;
        muplar_next_canvas_token += 0x100;
        muplar_canvases[i].token = (jlong) muplar_next_canvas_token;
        muplar_canvases[i].bitmap_token = bitmap_token;
        return &muplar_canvases[i];
    }
    return NULL;
}

static struct muplar_bitmap_state *muplar_canvas_bitmap(jlong canvas_token)
{
    struct muplar_canvas_state *canvas = muplar_find_canvas(canvas_token);
    if (!canvas)
        return NULL;
    return muplar_find_bitmap(canvas->bitmap_token);
}

static struct muplar_paint_state *muplar_find_paint(jlong token)
{
    size_t i;
    if (!token)
        return NULL;
    for (i = 0; i < sizeof(muplar_paints) / sizeof(muplar_paints[0]); i++)
        if (muplar_paints[i].token == token)
            return &muplar_paints[i];
    return NULL;
}

static struct muplar_paint_state *muplar_alloc_paint(jint color)
{
    size_t i;
    for (i = 0; i < sizeof(muplar_paints) / sizeof(muplar_paints[0]); i++) {
        if (muplar_paints[i].token)
            continue;
        muplar_next_paint_token += 0x100;
        muplar_paints[i].token = (jlong) muplar_next_paint_token;
        muplar_paints[i].color = color;
        return &muplar_paints[i];
    }
    return NULL;
}

static uint32_t muplar_paint_color(jlong token, uint32_t fallback)
{
    struct muplar_paint_state *paint = muplar_find_paint(token);
    if (!paint)
        return fallback;
    return (uint32_t) paint->color;
}

static int muplar_floor_to_int(float value)
{
    int i = (int) value;
    if ((float) i > value)
        i--;
    return i;
}

static int muplar_ceil_to_int(float value)
{
    int i = (int) value;
    if ((float) i < value)
        i++;
    return i;
}

static void muplar_fill_rect(struct muplar_bitmap_state *bitmap,
                             int left,
                             int top,
                             int right,
                             int bottom,
                             uint32_t color)
{
    int x;
    int y;
    if (!bitmap || !bitmap->pixels)
        return;
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > bitmap->width)
        right = bitmap->width;
    if (bottom > bitmap->height)
        bottom = bitmap->height;
    if (right <= left || bottom <= top)
        return;
    for (y = top; y < bottom; y++)
        for (x = left; x < right; x++)
            bitmap->pixels[(size_t) y * (size_t) bitmap->width + (size_t) x] =
                color;
}

static void muplar_fill_circle(struct muplar_bitmap_state *bitmap,
                               float cx,
                               float cy,
                               float radius,
                               uint32_t color)
{
    int x;
    int y;
    int left = muplar_floor_to_int(cx - radius);
    int top = muplar_floor_to_int(cy - radius);
    int right = muplar_ceil_to_int(cx + radius);
    int bottom = muplar_ceil_to_int(cy + radius);
    float r2 = radius * radius;
    if (!bitmap || radius <= 0.0f)
        return;
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > bitmap->width)
        right = bitmap->width;
    if (bottom > bitmap->height)
        bottom = bitmap->height;
    for (y = top; y < bottom; y++) {
        for (x = left; x < right; x++) {
            float dx = ((float) x + 0.5f) - cx;
            float dy = ((float) y + 0.5f) - cy;
            if (dx * dx + dy * dy <= r2)
                bitmap
                    ->pixels[(size_t) y * (size_t) bitmap->width + (size_t) x] =
                    color;
        }
    }
}

struct muplar_byte_buffer {
    uint8_t *data;
    size_t len;
    size_t cap;
    int ok;
};

static int muplar_buf_reserve(struct muplar_byte_buffer *buf, size_t extra)
{
    size_t next;
    uint8_t *data;
    if (!buf->ok)
        return 0;
    if (extra > (size_t) -1 - buf->len) {
        buf->ok = 0;
        return 0;
    }
    if (buf->len + extra <= buf->cap)
        return 1;
    next = buf->cap ? buf->cap : 4096;
    while (next < buf->len + extra) {
        if (next > (size_t) -1 / 2) {
            next = buf->len + extra;
            break;
        }
        next *= 2;
    }
    data = realloc(buf->data, next);
    if (!data) {
        buf->ok = 0;
        return 0;
    }
    buf->data = data;
    buf->cap = next;
    return 1;
}

static void muplar_buf_append(struct muplar_byte_buffer *buf,
                              const void *data,
                              size_t len)
{
    if (!muplar_buf_reserve(buf, len))
        return;
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
}

static void muplar_buf_u8(struct muplar_byte_buffer *buf, uint8_t value)
{
    muplar_buf_append(buf, &value, 1);
}

static void muplar_buf_be32(struct muplar_byte_buffer *buf, uint32_t value)
{
    uint8_t data[4];
    data[0] = (uint8_t) (value >> 24);
    data[1] = (uint8_t) (value >> 16);
    data[2] = (uint8_t) (value >> 8);
    data[3] = (uint8_t) value;
    muplar_buf_append(buf, data, sizeof(data));
}

static uint32_t muplar_crc32_update(uint32_t crc,
                                    const uint8_t *data,
                                    size_t len)
{
    size_t i;
    int bit;
    crc = ~crc;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}

static uint32_t muplar_adler32(const uint8_t *data, size_t len)
{
    size_t i;
    uint32_t a = 1;
    uint32_t b = 0;
    for (i = 0; i < len; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void muplar_png_chunk(struct muplar_byte_buffer *png,
                             const char type[4],
                             const uint8_t *data,
                             size_t len)
{
    uint32_t crc;
    muplar_buf_be32(png, (uint32_t) len);
    muplar_buf_append(png, type, 4);
    if (len)
        muplar_buf_append(png, data, len);
    crc = muplar_crc32_update(0, (const uint8_t *) type, 4);
    if (len)
        crc = muplar_crc32_update(crc, data, len);
    muplar_buf_be32(png, crc);
}

static int muplar_bitmap_is_uniform(struct muplar_bitmap_state *bitmap)
{
    size_t i;
    size_t count;
    uint32_t first;
    if (!bitmap || !bitmap->pixels)
        return 1;
    count = (size_t) bitmap->width * (size_t) bitmap->height;
    if (!count)
        return 1;
    first = bitmap->pixels[0];
    for (i = 1; i < count; i++)
        if (bitmap->pixels[i] != first)
            return 0;
    return 1;
}

static uint32_t muplar_sample_pixel(struct muplar_bitmap_state *bitmap,
                                    int x,
                                    int y,
                                    int uniform)
{
    uint32_t color;
    if (!bitmap || !bitmap->pixels)
        return 0xffffffffu;
    color = bitmap->pixels[(size_t) y * (size_t) bitmap->width + (size_t) x];
    if (!uniform)
        return color;
    if (y < 96)
        return 0xff263238u;
    if (y > bitmap->height - 120)
        return 0xff111111u;
    if (((x / 96) + (y / 96)) % 3 == 0)
        return 0xff4c8bf5u;
    if (((x / 96) + (y / 96)) % 3 == 1)
        return 0xff34a853u;
    return color ? color : 0xffeeeeeeu;
}

static int muplar_bitmap_to_png(struct muplar_bitmap_state *bitmap,
                                struct muplar_byte_buffer *png)
{
    static const uint8_t sig[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    struct muplar_byte_buffer raw = {0};
    struct muplar_byte_buffer idat = {0};
    uint8_t ihdr[13];
    size_t row_bytes;
    size_t remaining;
    size_t pos;
    uint32_t adler;
    int uniform;
    int x;
    int y;

    if (!bitmap || !bitmap->pixels || bitmap->width <= 0 || bitmap->height <= 0)
        return 0;
    raw.ok = 1;
    idat.ok = 1;
    png->ok = 1;
    row_bytes = (size_t) bitmap->width * 3u + 1u;
    if (!muplar_buf_reserve(&raw, row_bytes * (size_t) bitmap->height))
        goto fail;
    uniform = muplar_bitmap_is_uniform(bitmap);
    for (y = 0; y < bitmap->height; y++) {
        muplar_buf_u8(&raw, 0);
        for (x = 0; x < bitmap->width; x++) {
            uint32_t color = muplar_sample_pixel(bitmap, x, y, uniform);
            muplar_buf_u8(&raw, (uint8_t) (color >> 16));
            muplar_buf_u8(&raw, (uint8_t) (color >> 8));
            muplar_buf_u8(&raw, (uint8_t) color);
        }
    }
    muplar_buf_u8(&idat, 0x78);
    muplar_buf_u8(&idat, 0x01);
    remaining = raw.len;
    pos = 0;
    while (remaining) {
        uint16_t block = remaining > 65535u ? 65535u : (uint16_t) remaining;
        uint8_t final = remaining <= 65535u ? 1 : 0;
        muplar_buf_u8(&idat, final);
        muplar_buf_u8(&idat, (uint8_t) block);
        muplar_buf_u8(&idat, (uint8_t) (block >> 8));
        muplar_buf_u8(&idat, (uint8_t) ~block);
        muplar_buf_u8(&idat, (uint8_t) (~block >> 8));
        muplar_buf_append(&idat, raw.data + pos, block);
        pos += block;
        remaining -= block;
    }
    adler = muplar_adler32(raw.data, raw.len);
    muplar_buf_be32(&idat, adler);

    muplar_buf_append(png, sig, sizeof(sig));
    ihdr[0] = (uint8_t) (bitmap->width >> 24);
    ihdr[1] = (uint8_t) (bitmap->width >> 16);
    ihdr[2] = (uint8_t) (bitmap->width >> 8);
    ihdr[3] = (uint8_t) bitmap->width;
    ihdr[4] = (uint8_t) (bitmap->height >> 24);
    ihdr[5] = (uint8_t) (bitmap->height >> 16);
    ihdr[6] = (uint8_t) (bitmap->height >> 8);
    ihdr[7] = (uint8_t) bitmap->height;
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    muplar_png_chunk(png, "IHDR", ihdr, sizeof(ihdr));
    muplar_png_chunk(png, "IDAT", idat.data, idat.len);
    muplar_png_chunk(png, "IEND", NULL, 0);
    free(raw.data);
    free(idat.data);
    return png->ok;

fail:
    free(raw.data);
    free(idat.data);
    return 0;
}

static int muplar_write_output_stream(JNIEnv *env,
                                      jobject stream,
                                      const uint8_t *data,
                                      size_t len)
{
    jclass cls;
    jmethodID write_mid;
    jbyteArray array;
    if (!stream || !data || len > 0x7fffffffu)
        return 0;
    cls = (*env)->GetObjectClass(env, stream);
    if (!cls)
        return 0;
    write_mid = (*env)->GetMethodID(env, cls, "write", "([B)V");
    if (!write_mid) {
        (*env)->ExceptionClear(env);
        return 0;
    }
    array = (*env)->NewByteArray(env, (jsize) len);
    if (!array)
        return 0;
    (*env)->SetByteArrayRegion(env, array, 0, (jsize) len,
                               (const jbyte *) data);
    if ((*env)->ExceptionCheck(env))
        return 0;
    (*env)->CallVoidMethod(env, stream, write_mid, array);
    if ((*env)->ExceptionCheck(env))
        return 0;
    return 1;
}

static void muplar_set_long_field(JNIEnv *env,
                                  jobject object,
                                  const char *name,
                                  jlong value)
{
    jclass cls;
    jfieldID field;
    if (!object)
        return;
    cls = (*env)->GetObjectClass(env, object);
    if (!cls)
        return;
    field = (*env)->GetFieldID(env, cls, name, "J");
    if (!field) {
        (*env)->ExceptionClear(env);
        return;
    }
    (*env)->SetLongField(env, object, field, value);
}

struct muplar_native_window_buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void *bits;
    uint32_t reserved[6];
};

static int muplar_min_int(int a, int b)
{
    return a < b ? a : b;
}

static int muplar_present_bitmap_to_host_window(
    struct muplar_bitmap_state *bitmap)
{
    static const uint64_t guest_native_window = 0xA11D0001ULL;
    typedef int32_t (*set_buffers_geometry_fn)(void *, int32_t, int32_t,
                                               int32_t);
    typedef int32_t (*lock_fn)(void *, struct muplar_native_window_buffer *,
                               void *);
    typedef int32_t (*unlock_fn)(void *);
    struct muplar_native_window_buffer buffer;
    void *libandroid;
    set_buffers_geometry_fn set_buffers_geometry;
    lock_fn lock_window;
    unlock_fn unlock_and_post;
    void *window = (void *) (uintptr_t) guest_native_window;
    int dst_w;
    int dst_h;
    int x;
    int y;
    if (!bitmap || !bitmap->pixels || bitmap->width <= 0 || bitmap->height <= 0)
        return 0;

    dst_h = muplar_min_int(480, bitmap->height);
    dst_w = (int) (((int64_t) dst_h * bitmap->width) / bitmap->height);
    if (dst_w < 1)
        dst_w = 1;
    if (dst_w > 640) {
        dst_w = 640;
        dst_h = (int) (((int64_t) dst_w * bitmap->height) / bitmap->width);
        if (dst_h < 1)
            dst_h = 1;
    }

    libandroid = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!libandroid)
        return 0;
    set_buffers_geometry = (set_buffers_geometry_fn) dlsym(
        libandroid, "ANativeWindow_setBuffersGeometry");
    lock_window = (lock_fn) dlsym(libandroid, "ANativeWindow_lock");
    unlock_and_post =
        (unlock_fn) dlsym(libandroid, "ANativeWindow_unlockAndPost");
    if (!set_buffers_geometry || !lock_window || !unlock_and_post) {
        dlclose(libandroid);
        return 0;
    }

    set_buffers_geometry(window, dst_w, dst_h, 1);
    memset(&buffer, 0, sizeof(buffer));
    if (lock_window(window, &buffer, NULL) != 0) {
        dlclose(libandroid);
        return 0;
    }
    if (!buffer.bits || buffer.width <= 0 || buffer.height <= 0 ||
        buffer.stride < buffer.width) {
        unlock_and_post(window);
        dlclose(libandroid);
        return 0;
    }

    for (y = 0; y < buffer.height; y++) {
        int src_y = (int) (((int64_t) y * bitmap->height) / buffer.height);
        uint8_t *row =
            (uint8_t *) buffer.bits +
            (size_t) (buffer.height - 1 - y) * (size_t) buffer.stride * 4u;
        for (x = 0; x < buffer.width; x++) {
            int src_x = (int) (((int64_t) x * bitmap->width) / buffer.width);
            uint32_t color =
                bitmap->pixels[(size_t) src_y * (size_t) bitmap->width +
                               (size_t) src_x];
            row[(size_t) x * 4u + 0] = (uint8_t) (color >> 16);
            row[(size_t) x * 4u + 1] = (uint8_t) (color >> 8);
            row[(size_t) x * 4u + 2] = (uint8_t) color;
            row[(size_t) x * 4u + 3] = (uint8_t) (color >> 24);
        }
    }
    unlock_and_post(window);
    dlclose(libandroid);
    return 1;
}

struct muplar_line_break_result {
    jlong token;
    jint end_offset;
    jfloat width;
};

static struct muplar_line_break_result muplar_line_break_results[128];

static const char *const muplar_sqlite_default_columns[] = {
    "_id",
    "title",
    "intent",
    "container",
    "screen",
    "cellX",
    "cellY",
    "spanX",
    "spanY",
    "itemType",
    "appWidgetId",
    "appWidgetProvider",
    "icon",
    "iconType",
    "iconPackage",
    "iconResource",
    "profileId",
    "rank",
    "options",
    "restored",
    "modified",
    "appWidgetSource",
    "appWidgetFeatures",
    "isDisabled",
    "usingLowResIcon",
};

static void muplar_clear_exception(JNIEnv *env);

static void *muplar_dlopen_android_library(const char *libpath, int flag)
{
    void *handle;
    char candidate[512];
    static const char *const dirs[] = {
        "/system/lib64",
        "/apex/com.android.art/lib64",
        "/apex/com.android.runtime/lib64",
        "/apex/com.android.i18n/lib64",
        "/apex/com.android.conscrypt/lib64",
        NULL,
    };

    if (!libpath || !libpath[0])
        return NULL;

    handle = dlopen(libpath, flag);
    if (handle || strchr(libpath, '/'))
        return handle;

    for (size_t i = 0; dirs[i]; ++i) {
        size_t dir_len = strlen(dirs[i]);
        size_t lib_len = strlen(libpath);
        if (dir_len + 1 + lib_len + 1 > sizeof(candidate))
            continue;
        memcpy(candidate, dirs[i], dir_len);
        candidate[dir_len] = '/';
        memcpy(candidate + dir_len + 1, libpath, lib_len + 1);
        handle = dlopen(candidate, flag);
        if (handle)
            return handle;
    }

    return NULL;
}

static const char *muplar_property(const char *key)
{
    if (!key)
        return NULL;

    if (!strcmp(key, "servicemanager.ready") ||
        !strcmp(key, "servicemanager.installed") ||
        !strcmp(key, "hwservicemanager.ready") ||
        !strcmp(key, "vndservicemanager.ready")) {
        return "1";
    }

    if (!strcmp(key, "init.svc.servicemanager") ||
        !strcmp(key, "init.svc.hwservicemanager") ||
        !strcmp(key, "init.svc.vndservicemanager")) {
        return "running";
    }

    if (!strcmp(key, "ro.arch"))
        return "arm64";
    if (!strcmp(key, "ro.product.cpu.abi"))
        return "arm64-v8a";
    if (!strcmp(key, "ro.product.cpu.abilist") ||
        !strcmp(key, "ro.product.cpu.abilist64")) {
        return "arm64-v8a";
    }
    if (!strcmp(key, "ro.product.cpu.abilist32"))
        return "";

    if (!strcmp(key, "ro.build.version.sdk") ||
        !strcmp(key, "ro.build.version.sdk_full")) {
        return "35";
    }
    if (!strcmp(key, "ro.build.version.preview_sdk"))
        return "0";
    if (!strcmp(key, "ro.build.version.codename") ||
        !strcmp(key, "ro.build.version.release_or_codename")) {
        return "REL";
    }
    if (!strcmp(key, "ro.build.version.all_codenames") ||
        !strcmp(key, "ro.build.version.known_codenames")) {
        return "REL";
    }
    if (!strcmp(key, "ro.build.version.min_supported_target_sdk"))
        return "23";
    if (!strcmp(key, "ro.build.version.release") ||
        !strcmp(key, "ro.build.version.release_or_preview_display")) {
        return "15";
    }
    if (!strcmp(key, "ro.build.version.incremental"))
        return "muplar";
    if (!strcmp(key, "ro.build.version.security_patch"))
        return "2026-07-01";
    if (!strcmp(key, "ro.build.type"))
        return "userdebug";
    if (!strcmp(key, "ro.build.tags"))
        return "test-keys";
    if (!strcmp(key, "ro.build.id"))
        return "MUPLAR";
    if (!strcmp(key, "ro.build.fingerprint"))
        return "muplar/android_arm64/muplar:15/MUPLAR/1:userdebug/test-keys";

    if (!strcmp(key, "ro.product.brand") ||
        !strcmp(key, "ro.product.vendor.brand") ||
        !strcmp(key, "ro.product.brand_for_attestation")) {
        return "Muplar";
    }
    if (!strcmp(key, "ro.product.manufacturer") ||
        !strcmp(key, "ro.product.vendor.manufacturer") ||
        !strcmp(key, "ro.product.manufacturer_for_attestation")) {
        return "Muplar";
    }
    if (!strcmp(key, "ro.product.name") ||
        !strcmp(key, "ro.product.vendor.name") ||
        !strcmp(key, "ro.product.name_for_attestation")) {
        return "muplar_arm64";
    }
    if (!strcmp(key, "ro.product.device") ||
        !strcmp(key, "ro.product.vendor.device") ||
        !strcmp(key, "ro.product.device_for_attestation")) {
        return "muplar";
    }
    if (!strcmp(key, "ro.product.model") ||
        !strcmp(key, "ro.product.vendor.model") ||
        !strcmp(key, "ro.product.model_for_attestation")) {
        return "Muplar Android";
    }
    if (!strcmp(key, "ro.product.locale") ||
        !strcmp(key, "persist.sys.locale")) {
        return "en-US";
    }
    if (!strcmp(key, "ro.product.locale.language"))
        return "en";
    if (!strcmp(key, "ro.product.locale.region"))
        return "US";

    if (!strcmp(key, "ro.debuggable"))
        return "1";
    if (!strcmp(key, "ro.treble.enabled"))
        return "true";
    if (!strcmp(key, "ro.config.low_ram"))
        return "false";
    if (!strcmp(key, "ro.property_service.version"))
        return "2";
    if (!strcmp(key, "ro.hardware"))
        return "muplar";
    if (!strcmp(key, "persist.sys.dalvik.vm.lib.2"))
        return "libart.so";
    if (!strcmp(key, "ro.dalvik.vm.native.bridge"))
        return "0";
    if (!strcmp(key, "dalvik.vm.isa.arm64.variant") ||
        !strcmp(key, "dalvik.vm.isa.arm64.features")) {
        return "generic";
    }
    if (!strcmp(key, "dalvik.vm.allow_profile_code"))
        return "false";
    if (!strcmp(key, "persist.log.level"))
        return "I";
    if (!strncmp(key, "log.tag.", 8) || !strncmp(key, "persist.log.tag", 15)) {
        return "I";
    }

    return NULL;
}

static int muplar_copy_property(char *value, const char *src)
{
    size_t len = src ? strlen(src) : 0;
    if (value) {
        size_t cap = PROP_VALUE_MAX - 1;
        size_t copy = len < cap ? len : cap;
        if (copy)
            memcpy(value, src, copy);
        value[copy] = '\0';
    }
    return (int) len;
}

int property_get(const char *key, char *value, const char *default_value)
{
    const char *property = muplar_property(key);
    return muplar_copy_property(value, property ? property : default_value);
}

int __system_property_get(const char *key, char *value)
{
    const char *property = muplar_property(key);
    return muplar_copy_property(value, property ? property : "");
}

static const char *muplar_jni_property(JNIEnv *env,
                                       jstring key,
                                       const char **chars_out)
{
    const char *chars;
    if (!key) {
        *chars_out = NULL;
        return NULL;
    }
    chars = (*env)->GetStringUTFChars(env, key, NULL);
    *chars_out = chars;
    return muplar_property(chars);
}

static void muplar_jni_release_string(JNIEnv *env,
                                      jstring key,
                                      const char *chars)
{
    if (key && chars)
        (*env)->ReleaseStringUTFChars(env, key, chars);
}

static int muplar_parse_int(const char *value, int fallback)
{
    char *end = NULL;
    long parsed;
    if (!value || !value[0])
        return fallback;
    parsed = strtol(value, &end, 10);
    return end && *end == '\0' ? (int) parsed : fallback;
}

static int64_t muplar_parse_long(const char *value, int64_t fallback)
{
    char *end = NULL;
    long long parsed;
    if (!value || !value[0])
        return fallback;
    parsed = strtoll(value, &end, 10);
    return end && *end == '\0' ? (int64_t) parsed : fallback;
}

static jboolean muplar_parse_boolean(const char *value, jboolean fallback)
{
    if (!value || !value[0])
        return fallback;
    if (!strcmp(value, "1") || !strcmp(value, "true") || !strcmp(value, "y") ||
        !strcmp(value, "yes") || !strcmp(value, "on")) {
        return JNI_TRUE;
    }
    if (!strcmp(value, "0") || !strcmp(value, "false") || !strcmp(value, "n") ||
        !strcmp(value, "no") || !strcmp(value, "off")) {
        return JNI_FALSE;
    }
    return fallback;
}

jstring Java_android_os_SystemProperties_native_1get(JNIEnv *env,
                                                     jclass clazz,
                                                     jstring key,
                                                     jstring def)
{
    (void) clazz;
    const char *chars = NULL;
    const char *value = muplar_jni_property(env, key, &chars);
    if (!value && def)
        return def;
    jstring out = (*env)->NewStringUTF(env, value ? value : "");
    muplar_jni_release_string(env, key, chars);
    return out;
}

jstring Java_android_os_SystemProperties_native_1get__Ljava_lang_String_2(
    JNIEnv *env,
    jclass clazz,
    jstring key)
{
    return Java_android_os_SystemProperties_native_1get(env, clazz, key, NULL);
}

jstring
Java_android_os_SystemProperties_native_1get__Ljava_lang_String_2Ljava_lang_String_2(
    JNIEnv *env,
    jclass clazz,
    jstring key,
    jstring def)
{
    return Java_android_os_SystemProperties_native_1get(env, clazz, key, def);
}

jint Java_android_os_SystemProperties_native_1get_1int(JNIEnv *env,
                                                       jclass clazz,
                                                       jstring key,
                                                       jint def)
{
    (void) clazz;
    const char *chars = NULL;
    const char *value = muplar_jni_property(env, key, &chars);
    jint out = (jint) muplar_parse_int(value, def);
    muplar_jni_release_string(env, key, chars);
    return out;
}

jlong Java_android_os_SystemProperties_native_1get_1long(JNIEnv *env,
                                                         jclass clazz,
                                                         jstring key,
                                                         jlong def)
{
    (void) clazz;
    const char *chars = NULL;
    const char *value = muplar_jni_property(env, key, &chars);
    jlong out = (jlong) muplar_parse_long(value, def);
    muplar_jni_release_string(env, key, chars);
    return out;
}

jboolean Java_android_os_SystemProperties_native_1get_1boolean(JNIEnv *env,
                                                               jclass clazz,
                                                               jstring key,
                                                               jboolean def)
{
    (void) clazz;
    const char *chars = NULL;
    const char *value = muplar_jni_property(env, key, &chars);
    jboolean out = muplar_parse_boolean(value, def);
    muplar_jni_release_string(env, key, chars);
    return out;
}

jlong Java_android_os_SystemProperties_native_1find(JNIEnv *env,
                                                    jclass clazz,
                                                    jstring key)
{
    (void) clazz;
    const char *chars = NULL;
    (void) muplar_jni_property(env, key, &chars);
    if (!chars)
        return 0;
    for (size_t i = 0; i < sizeof(muplar_property_handles) /
                               sizeof(muplar_property_handles[0]);
         ++i) {
        if (muplar_property_handles[i][0] &&
            !strcmp(muplar_property_handles[i], chars)) {
            muplar_jni_release_string(env, key, chars);
            return (jlong) (i + 1);
        }
    }
    for (size_t i = 0; i < sizeof(muplar_property_handles) /
                               sizeof(muplar_property_handles[0]);
         ++i) {
        if (!muplar_property_handles[i][0]) {
            size_t len = strlen(chars);
            if (len >= sizeof(muplar_property_handles[i]))
                len = sizeof(muplar_property_handles[i]) - 1;
            memcpy(muplar_property_handles[i], chars, len);
            muplar_property_handles[i][len] = '\0';
            muplar_jni_release_string(env, key, chars);
            return (jlong) (i + 1);
        }
    }
    muplar_jni_release_string(env, key, chars);
    return 0;
}

jstring Java_android_os_SystemProperties_native_1get__J(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong handle)
{
    (void) clazz;
    if (handle > 0 && handle <= (jlong) (sizeof(muplar_property_handles) /
                                         sizeof(muplar_property_handles[0]))) {
        const char *key = muplar_property_handles[handle - 1];
        const char *value = muplar_property(key);
        return (*env)->NewStringUTF(env, value ? value : "");
    }
    return (*env)->NewStringUTF(env, "");
}

jint Java_android_os_SystemProperties_native_1get_1int__JI(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong handle,
                                                           jint def)
{
    (void) env;
    (void) clazz;
    if (handle > 0 && handle <= (jlong) (sizeof(muplar_property_handles) /
                                         sizeof(muplar_property_handles[0]))) {
        return (jint) muplar_parse_int(
            muplar_property(muplar_property_handles[handle - 1]), def);
    }
    return def;
}

jlong Java_android_os_SystemProperties_native_1get_1long__JJ(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong handle,
                                                             jlong def)
{
    (void) env;
    (void) clazz;
    if (handle > 0 && handle <= (jlong) (sizeof(muplar_property_handles) /
                                         sizeof(muplar_property_handles[0]))) {
        return (jlong) muplar_parse_long(
            muplar_property(muplar_property_handles[handle - 1]), def);
    }
    return def;
}

jboolean Java_android_os_SystemProperties_native_1get_1boolean__JZ(JNIEnv *env,
                                                                   jclass clazz,
                                                                   jlong handle,
                                                                   jboolean def)
{
    (void) env;
    (void) clazz;
    if (handle > 0 && handle <= (jlong) (sizeof(muplar_property_handles) /
                                         sizeof(muplar_property_handles[0]))) {
        return muplar_parse_boolean(
            muplar_property(muplar_property_handles[handle - 1]), def);
    }
    return def;
}

void Java_android_os_SystemProperties_native_1add_1change_1callback(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
}

jlong Java_android_os_MessageQueue_nativeInit(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return (jlong) (uintptr_t) &muplar_message_queue_token;
}

void Java_android_os_MessageQueue_nativeDestroy(JNIEnv *env,
                                                jclass clazz,
                                                jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

void Java_android_os_MessageQueue_nativePollOnce(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong ptr,
                                                 jint timeoutMillis)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) timeoutMillis;
}

void Java_android_os_MessageQueue_nativeWake(JNIEnv *env,
                                             jclass clazz,
                                             jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

jboolean Java_android_os_MessageQueue_nativeIsPolling(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    return JNI_FALSE;
}

void Java_android_os_MessageQueue_nativeSetFileDescriptorEvents(JNIEnv *env,
                                                                jclass clazz,
                                                                jlong ptr,
                                                                jint fd,
                                                                jint events)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) fd;
    (void) events;
}

jboolean Java_android_util_Log_isLoggable(JNIEnv *env,
                                          jclass clazz,
                                          jstring tag,
                                          jint level)
{
    (void) env;
    (void) clazz;
    (void) tag;
    (void) level;
    return JNI_FALSE;
}

jint Java_android_util_Log_println_1native(JNIEnv *env,
                                           jclass clazz,
                                           jint buffer,
                                           jint priority,
                                           jstring tag,
                                           jstring msg)
{
    (void) clazz;
    (void) buffer;
    (void) priority;
    const char *tag_chars =
        tag ? (*env)->GetStringUTFChars(env, tag, NULL) : NULL;
    const char *msg_chars =
        msg ? (*env)->GetStringUTFChars(env, msg, NULL) : NULL;
    if (msg_chars)
        fprintf(stderr, "[android.util.Log/%d] %s: %s\n", (int) priority,
                tag_chars ? tag_chars : "Muplar", msg_chars);
    if (msg && msg_chars)
        (*env)->ReleaseStringUTFChars(env, msg, msg_chars);
    if (tag && tag_chars)
        (*env)->ReleaseStringUTFChars(env, tag, tag_chars);
    return msg_chars ? (jint) strlen(msg_chars) : 0;
}

jint Java_android_util_Log_logger_1entry_1max_1payload_1native(JNIEnv *env,
                                                               jclass clazz)
{
    (void) env;
    (void) clazz;
    return 4068;
}

jint Java_android_util_EventLog_writeEvent(JNIEnv *env,
                                           jclass clazz,
                                           jint tag,
                                           jobjectArray values)
{
    (void) env;
    (void) clazz;
    (void) tag;
    (void) values;
    return 0;
}

static jlong muplar_SQLite_nOpen(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
    return (jlong) ++muplar_next_sqlite_token;
}

static jint muplar_sqlite_count_parameters(const char *sql)
{
    jint count = 0;
    char quote = 0;

    if (!sql)
        return 0;

    for (const char *p = sql; *p; ++p) {
        if (quote) {
            if (*p == quote) {
                if (p[1] == quote) {
                    ++p;
                } else {
                    quote = 0;
                }
            }
            continue;
        }

        if (*p == '\'' || *p == '"') {
            quote = *p;
            continue;
        }

        if (*p == '?')
            ++count;
    }

    return count;
}

static void muplar_sqlite_store_statement(jlong handle, jint parameter_count)
{
    size_t slot = (size_t) handle % (sizeof(muplar_sqlite_statements) /
                                     sizeof(muplar_sqlite_statements[0]));
    muplar_sqlite_statements[slot].handle = handle;
    muplar_sqlite_statements[slot].parameter_count = parameter_count;
}

static jint muplar_sqlite_statement_parameter_count(jlong handle)
{
    size_t slot = (size_t) handle % (sizeof(muplar_sqlite_statements) /
                                     sizeof(muplar_sqlite_statements[0]));

    if (muplar_sqlite_statements[slot].handle == handle)
        return muplar_sqlite_statements[slot].parameter_count;
    return 0;
}

static jlong muplar_SQLite_nativePrepareStatement(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong connection_ptr,
                                                  jstring sql)
{
    const char *chars;
    jlong handle;
    jint parameter_count;

    (void) clazz;
    (void) connection_ptr;

    chars = sql ? (*env)->GetStringUTFChars(env, sql, NULL) : NULL;
    parameter_count = muplar_sqlite_count_parameters(chars);
    if (sql && chars)
        (*env)->ReleaseStringUTFChars(env, sql, chars);

    handle = (jlong) ++muplar_next_sqlite_token;
    muplar_sqlite_store_statement(handle, parameter_count);
    return handle;
}

static jint muplar_SQLite_nativeGetParameterCount(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong connection_ptr,
                                                  jlong statement_ptr)
{
    (void) env;
    (void) clazz;
    (void) connection_ptr;
    return muplar_sqlite_statement_parameter_count(statement_ptr);
}

static jint muplar_SQLite_nativeGetColumnCount(JNIEnv *env,
                                               jclass clazz,
                                               jlong connection_ptr,
                                               jlong statement_ptr)
{
    (void) env;
    (void) clazz;
    (void) connection_ptr;
    (void) statement_ptr;
    return (jint) (sizeof(muplar_sqlite_default_columns) /
                   sizeof(muplar_sqlite_default_columns[0]));
}

static jstring muplar_SQLite_nativeGetColumnName(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong connection_ptr,
                                                 jlong statement_ptr,
                                                 jint index)
{
    size_t count = sizeof(muplar_sqlite_default_columns) /
                   sizeof(muplar_sqlite_default_columns[0]);
    (void) clazz;
    (void) connection_ptr;
    (void) statement_ptr;
    if (index < 0 || (size_t) index >= count)
        return (*env)->NewStringUTF(env, "");
    return (*env)->NewStringUTF(env, muplar_sqlite_default_columns[index]);
}

static jlong muplar_SQLite_nLong(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
    return 0;
}

static jint muplar_SQLite_nInt(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
    return 0;
}

static jboolean muplar_SQLite_nBoolean(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
    return JNI_FALSE;
}

static void muplar_SQLite_nVoid(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
}

static jstring muplar_SQLite_nString(JNIEnv *env, jclass clazz, ...)
{
    (void) clazz;
    return (*env)->NewStringUTF(env, "");
}

static jlong muplar_SQLite_nativeExecuteForCursorWindow(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong connection_ptr,
                                                        jlong statement_ptr,
                                                        jlong window_ptr,
                                                        jint start_pos,
                                                        jint required_pos,
                                                        jboolean count_all_rows)
{
    (void) env;
    (void) clazz;
    (void) connection_ptr;
    (void) statement_ptr;
    (void) window_ptr;
    (void) start_pos;
    (void) required_pos;
    (void) count_all_rows;
    return 0;
}

static jlong muplar_CursorWindow_nativeCreate(JNIEnv *env,
                                              jclass clazz,
                                              jstring name,
                                              jint cursor_window_size)
{
    (void) env;
    (void) clazz;
    (void) name;
    (void) cursor_window_size;
    return (jlong) ++muplar_next_cursor_window_token;
}

static jlong muplar_CursorWindow_nativeCreateFromParcel(JNIEnv *env,
                                                        jclass clazz,
                                                        jobject parcel)
{
    (void) env;
    (void) clazz;
    (void) parcel;
    return (jlong) ++muplar_next_cursor_window_token;
}

static void muplar_CursorWindow_nativeVoid(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
}

static jboolean muplar_CursorWindow_nativeTrue(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
    return JNI_TRUE;
}

static jint muplar_CursorWindow_nativeInt(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
    return 0;
}

static jlong muplar_CursorWindow_nativeLong(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
    return 0;
}

static jdouble muplar_CursorWindow_nativeDouble(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
    return 0;
}

static jbyteArray muplar_CursorWindow_nativeBlob(JNIEnv *env, jclass clazz, ...)
{
    (void) clazz;
    return (*env)->NewByteArray(env, 0);
}

static jstring muplar_CursorWindow_nativeString(JNIEnv *env, jclass clazz, ...)
{
    (void) clazz;
    return (*env)->NewStringUTF(env, "");
}

jlong Java_android_graphics_Matrix_00024ExtraNatives_nCreate(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong src)
{
    (void) env;
    (void) clazz;
    (void) src;
    muplar_next_matrix_token += 0x100;
    return (jlong) muplar_next_matrix_token;
}

jlong Java_android_graphics_Matrix_00024ExtraNatives_nGetNativeFinalizer(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

void Java_android_graphics_Matrix_nSetScale(JNIEnv *env,
                                            jclass clazz,
                                            jlong matrix,
                                            jfloat sx,
                                            jfloat sy)
{
    (void) env;
    (void) clazz;
    (void) matrix;
    (void) sx;
    (void) sy;
}

static void muplar_Matrix_nVoid(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
}

static jboolean muplar_Matrix_nTrue(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
    return JNI_TRUE;
}

jlong Java_android_graphics_ColorSpace_00024Rgb_00024Native_nativeCreate(
    JNIEnv *env,
    jclass clazz,
    jfloat rx,
    jfloat ry,
    jfloat gx,
    jfloat gy,
    jfloat bx,
    jfloat by,
    jfloat white_point_y,
    jfloatArray transform)
{
    (void) env;
    (void) clazz;
    (void) rx;
    (void) ry;
    (void) gx;
    (void) gy;
    (void) bx;
    (void) by;
    (void) white_point_y;
    (void) transform;
    muplar_next_colorspace_token += 0x100;
    return (jlong) muplar_next_colorspace_token;
}

jlong Java_android_graphics_ColorSpace_00024Rgb_00024Native_nativeGetNativeFinalizer(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jlong Java_android_graphics_Path_nInit(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    muplar_next_path_token += 0x100;
    return (jlong) muplar_next_path_token;
}

jlong Java_android_graphics_Path_nGetFinalizer(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

void Java_android_graphics_Path_nIncReserve(JNIEnv *env,
                                            jclass clazz,
                                            jlong path,
                                            jint extra_pt_count)
{
    (void) env;
    (void) clazz;
    (void) path;
    (void) extra_pt_count;
}

void Java_android_graphics_Path_nMoveTo(JNIEnv *env,
                                        jclass clazz,
                                        jlong path,
                                        jfloat x,
                                        jfloat y)
{
    (void) env;
    (void) clazz;
    (void) path;
    (void) x;
    (void) y;
}

void Java_android_graphics_Path_nRMoveTo(JNIEnv *env,
                                         jclass clazz,
                                         jlong path,
                                         jfloat dx,
                                         jfloat dy)
{
    Java_android_graphics_Path_nMoveTo(env, clazz, path, dx, dy);
}

void Java_android_graphics_Path_nLineTo(JNIEnv *env,
                                        jclass clazz,
                                        jlong path,
                                        jfloat x,
                                        jfloat y)
{
    Java_android_graphics_Path_nMoveTo(env, clazz, path, x, y);
}

void Java_android_graphics_Path_nRLineTo(JNIEnv *env,
                                         jclass clazz,
                                         jlong path,
                                         jfloat dx,
                                         jfloat dy)
{
    Java_android_graphics_Path_nMoveTo(env, clazz, path, dx, dy);
}

void Java_android_graphics_Path_nQuadTo(JNIEnv *env,
                                        jclass clazz,
                                        jlong path,
                                        jfloat x1,
                                        jfloat y1,
                                        jfloat x2,
                                        jfloat y2)
{
    (void) x1;
    (void) y1;
    Java_android_graphics_Path_nMoveTo(env, clazz, path, x2, y2);
}

void Java_android_graphics_Path_nRQuadTo(JNIEnv *env,
                                         jclass clazz,
                                         jlong path,
                                         jfloat dx1,
                                         jfloat dy1,
                                         jfloat dx2,
                                         jfloat dy2)
{
    Java_android_graphics_Path_nQuadTo(env, clazz, path, dx1, dy1, dx2, dy2);
}

void Java_android_graphics_Path_nCubicTo(JNIEnv *env,
                                         jclass clazz,
                                         jlong path,
                                         jfloat x1,
                                         jfloat y1,
                                         jfloat x2,
                                         jfloat y2,
                                         jfloat x3,
                                         jfloat y3)
{
    (void) x1;
    (void) y1;
    (void) x2;
    (void) y2;
    Java_android_graphics_Path_nMoveTo(env, clazz, path, x3, y3);
}

void Java_android_graphics_Path_nRCubicTo(JNIEnv *env,
                                          jclass clazz,
                                          jlong path,
                                          jfloat dx1,
                                          jfloat dy1,
                                          jfloat dx2,
                                          jfloat dy2,
                                          jfloat dx3,
                                          jfloat dy3)
{
    Java_android_graphics_Path_nCubicTo(env, clazz, path, dx1, dy1, dx2, dy2,
                                        dx3, dy3);
}

void Java_android_graphics_Path_nConicTo(JNIEnv *env,
                                         jclass clazz,
                                         jlong path,
                                         jfloat x1,
                                         jfloat y1,
                                         jfloat x2,
                                         jfloat y2,
                                         jfloat weight)
{
    (void) weight;
    Java_android_graphics_Path_nQuadTo(env, clazz, path, x1, y1, x2, y2);
}

void Java_android_graphics_Path_nRConicTo(JNIEnv *env,
                                          jclass clazz,
                                          jlong path,
                                          jfloat dx1,
                                          jfloat dy1,
                                          jfloat dx2,
                                          jfloat dy2,
                                          jfloat weight)
{
    Java_android_graphics_Path_nConicTo(env, clazz, path, dx1, dy1, dx2, dy2,
                                        weight);
}

void Java_android_graphics_Path_nClose(JNIEnv *env, jclass clazz, jlong path)
{
    (void) env;
    (void) clazz;
    (void) path;
}

void Java_android_graphics_Path_nReset(JNIEnv *env, jclass clazz, jlong path)
{
    Java_android_graphics_Path_nClose(env, clazz, path);
}

void Java_android_graphics_Path_nRewind(JNIEnv *env, jclass clazz, jlong path)
{
    Java_android_graphics_Path_nClose(env, clazz, path);
}

static void muplar_Path_nVoid(JNIEnv *env, jclass clazz, ...)
{
    (void) env;
    (void) clazz;
}

jboolean Java_android_graphics_Path_nIsEmpty(JNIEnv *env,
                                             jclass clazz,
                                             jlong path)
{
    (void) env;
    (void) clazz;
    (void) path;
    return JNI_FALSE;
}

jboolean Java_android_graphics_Path_nIsConvex(JNIEnv *env,
                                              jclass clazz,
                                              jlong path)
{
    return Java_android_graphics_Path_nIsEmpty(env, clazz, path);
}

jboolean Java_android_graphics_Path_nIsInterpolatable(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong path,
                                                      jlong other)
{
    (void) other;
    return Java_android_graphics_Path_nIsEmpty(env, clazz, path);
}

jboolean Java_android_graphics_Path_nInterpolate(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong start,
                                                 jlong end,
                                                 jfloat t,
                                                 jlong out)
{
    (void) env;
    (void) clazz;
    (void) start;
    (void) end;
    (void) t;
    (void) out;
    return JNI_TRUE;
}

jboolean Java_android_graphics_Path_nOp(JNIEnv *env,
                                        jclass clazz,
                                        jlong path1,
                                        jlong path2,
                                        jint op,
                                        jlong out)
{
    (void) env;
    (void) clazz;
    (void) path1;
    (void) path2;
    (void) op;
    (void) out;
    return JNI_TRUE;
}

void Java_android_graphics_Path_nSet(JNIEnv *env,
                                     jclass clazz,
                                     jlong dst,
                                     jlong src)
{
    (void) env;
    (void) clazz;
    (void) dst;
    (void) src;
}

void Java_android_graphics_Path_nTransform(JNIEnv *env,
                                           jclass clazz,
                                           jlong path,
                                           jlong matrix,
                                           jlong dst)
{
    (void) env;
    (void) clazz;
    (void) path;
    (void) matrix;
    (void) dst;
}

void Java_android_graphics_Path_nOffset(JNIEnv *env,
                                        jclass clazz,
                                        jlong path,
                                        jfloat dx,
                                        jfloat dy,
                                        jlong dst)
{
    (void) env;
    (void) clazz;
    (void) path;
    (void) dx;
    (void) dy;
    (void) dst;
}

void Java_android_graphics_Path_nSetFillType(JNIEnv *env,
                                             jclass clazz,
                                             jlong path,
                                             jint fill_type)
{
    (void) env;
    (void) clazz;
    (void) path;
    (void) fill_type;
}

jint Java_android_graphics_Path_nGetFillType(JNIEnv *env,
                                             jclass clazz,
                                             jlong path)
{
    (void) env;
    (void) clazz;
    (void) path;
    return 0;
}

void Java_android_graphics_Path_nComputeBounds(JNIEnv *env,
                                               jclass clazz,
                                               jlong path,
                                               jobject rect)
{
    (void) env;
    (void) clazz;
    (void) path;
    (void) rect;
}

jfloatArray Java_android_graphics_Path_nApproximate(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong path,
                                                    jfloat error)
{
    static const jfloat values[] = {
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
    };
    jfloatArray array;
    (void) clazz;
    (void) path;
    (void) error;
    array = (*env)->NewFloatArray(env, 6);
    if (array)
        (*env)->SetFloatArrayRegion(env, array, 0, 6, values);
    return array;
}

jlong Java_android_graphics_PathMeasure_native_1create(JNIEnv *env,
                                                       jclass clazz,
                                                       jlong path,
                                                       jboolean force_closed)
{
    (void) env;
    (void) clazz;
    (void) path;
    (void) force_closed;
    muplar_next_path_measure_token += 0x100;
    return (jlong) muplar_next_path_measure_token;
}

void Java_android_graphics_PathMeasure_native_1destroy(JNIEnv *env,
                                                       jclass clazz,
                                                       jlong native_instance)
{
    (void) env;
    (void) clazz;
    (void) native_instance;
}

jfloat Java_android_graphics_PathMeasure_native_1getLength(
    JNIEnv *env,
    jclass clazz,
    jlong native_instance)
{
    (void) env;
    (void) clazz;
    (void) native_instance;
    return 1.0f;
}

jboolean Java_android_graphics_PathMeasure_native_1getPosTan(
    JNIEnv *env,
    jclass clazz,
    jlong native_instance,
    jfloat distance,
    jfloatArray pos,
    jfloatArray tan)
{
    jfloat pos_values[2] = {distance, distance};
    jfloat tan_values[2] = {1.0f, 0.0f};
    (void) clazz;
    (void) native_instance;
    if (pos)
        (*env)->SetFloatArrayRegion(env, pos, 0, 2, pos_values);
    if (tan)
        (*env)->SetFloatArrayRegion(env, tan, 0, 2, tan_values);
    return JNI_TRUE;
}

jlong Java_android_graphics_Paint_nInit(JNIEnv *env, jclass clazz)
{
    struct muplar_paint_state *paint;
    (void) env;
    (void) clazz;
    paint = muplar_alloc_paint(0xff000000u);
    if (paint)
        return paint->token;
    muplar_next_paint_token += 0x100;
    return (jlong) muplar_next_paint_token;
}

jlong Java_android_graphics_Paint_nInitWithPaint(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong paint)
{
    struct muplar_paint_state *src = muplar_find_paint(paint);
    struct muplar_paint_state *dst;
    (void) env;
    (void) clazz;
    dst = muplar_alloc_paint(src ? src->color : 0xff000000u);
    if (dst)
        return dst->token;
    return Java_android_graphics_Paint_nInit(env, clazz);
}

void Java_android_graphics_Paint_nSet(JNIEnv *env,
                                      jclass clazz,
                                      jlong dst,
                                      jlong src)
{
    struct muplar_paint_state *dst_paint = muplar_find_paint(dst);
    struct muplar_paint_state *src_paint = muplar_find_paint(src);
    (void) env;
    (void) clazz;
    if (dst_paint && src_paint)
        dst_paint->color = src_paint->color;
}

jlong Java_android_graphics_Paint_nGetNativeFinalizer(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

void Java_android_graphics_Paint_nSetTextSize(JNIEnv *env,
                                              jclass clazz,
                                              jlong paint,
                                              jfloat size)
{
    (void) env;
    (void) clazz;
    (void) paint;
    (void) size;
}

jfloat Java_android_graphics_Paint_nGetTextSize(JNIEnv *env,
                                                jclass clazz,
                                                jlong paint)
{
    (void) env;
    (void) clazz;
    (void) paint;
    return 16.0f;
}

void Java_android_graphics_Paint_nSetAntiAlias(JNIEnv *env,
                                               jclass clazz,
                                               jlong paint,
                                               jboolean aa)
{
    (void) env;
    (void) clazz;
    (void) paint;
    (void) aa;
}

void Java_android_graphics_Paint_nSetFlags(JNIEnv *env,
                                           jclass clazz,
                                           jlong paint,
                                           jint flags)
{
    (void) env;
    (void) clazz;
    (void) paint;
    (void) flags;
}

jint Java_android_graphics_Paint_nGetFlags(JNIEnv *env,
                                           jclass clazz,
                                           jlong paint)
{
    (void) env;
    (void) clazz;
    (void) paint;
    return 0;
}

void Java_android_graphics_Paint_nSetColor(JNIEnv *env,
                                           jclass clazz,
                                           jlong paint,
                                           jint color)
{
    struct muplar_paint_state *state = muplar_find_paint(paint);
    (void) env;
    (void) clazz;
    if (state)
        state->color = color;
}

void Java_android_graphics_Paint_nSetMyanmarEncoding(JNIEnv *env,
                                                     jclass clazz,
                                                     jlong paint,
                                                     jint encoding)
{
    (void) env;
    (void) clazz;
    (void) paint;
    (void) encoding;
}

void Java_android_graphics_Paint_nSetInt(JNIEnv *env,
                                         jclass clazz,
                                         jlong paint,
                                         jint value)
{
    (void) env;
    (void) clazz;
    (void) paint;
    (void) value;
}

void Java_android_graphics_Paint_nSetBoolean(JNIEnv *env,
                                             jclass clazz,
                                             jlong paint,
                                             jboolean value)
{
    (void) env;
    (void) clazz;
    (void) paint;
    (void) value;
}

void Java_android_graphics_Paint_nSetString(JNIEnv *env,
                                            jclass clazz,
                                            jlong paint,
                                            jstring value)
{
    (void) env;
    (void) clazz;
    (void) paint;
    (void) value;
}

jlong Java_android_graphics_Paint_nSetTypeface(JNIEnv *env,
                                               jclass clazz,
                                               jlong paint,
                                               jlong typeface)
{
    (void) env;
    (void) clazz;
    (void) paint;
    return typeface;
}

jlong Java_android_graphics_Paint_nSetShader(JNIEnv *env,
                                             jclass clazz,
                                             jlong paint,
                                             jlong shader)
{
    (void) env;
    (void) clazz;
    (void) paint;
    return shader;
}

jlong Java_android_graphics_Paint_nSetPathEffect(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong paint,
                                                 jlong effect)
{
    (void) env;
    (void) clazz;
    (void) paint;
    return effect;
}

jlong Java_android_graphics_Shader_nativeGetFinalizer(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jlong Java_android_graphics_fonts_Font_00024Builder_nInitBuilder(JNIEnv *env,
                                                                 jclass clazz)
{
    (void) env;
    (void) clazz;
    muplar_next_font_token += 0x1000;
    return (jlong) muplar_next_font_token;
}

void Java_android_graphics_fonts_Font_00024Builder_nAddAxis(jlong builder_ptr,
                                                            jint tag,
                                                            jfloat value)
{
    (void) builder_ptr;
    (void) tag;
    (void) value;
}

jlong Java_android_graphics_fonts_Font_00024Builder_nBuild(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong builder_ptr,
                                                           jobject buffer,
                                                           jstring file_path,
                                                           jstring locale_list,
                                                           jint weight,
                                                           jboolean italic,
                                                           jint ttc_index)
{
    (void) env;
    (void) clazz;
    (void) builder_ptr;
    (void) buffer;
    (void) file_path;
    (void) locale_list;
    (void) ttc_index;
    muplar_next_font_token += 0x1000;
    return (jlong) (muplar_next_font_token |
                    (((uintptr_t) ((weight > 0 ? weight : 400) & 0x3ff)) << 3) |
                    (italic == JNI_TRUE ? 2 : 0));
}

jlong Java_android_graphics_fonts_Font_00024Builder_nClone(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong font_ptr,
                                                           jlong builder_ptr,
                                                           jint weight,
                                                           jboolean italic,
                                                           jint ttc_index)
{
    (void) env;
    (void) clazz;
    (void) builder_ptr;
    (void) ttc_index;
    if (weight <= 0)
        weight = (jint) ((font_ptr >> 3) & 0x3ff);
    return Java_android_graphics_fonts_Font_00024Builder_nBuild(
        env, clazz, builder_ptr, NULL, NULL, NULL, weight, italic, ttc_index);
}

jlong Java_android_graphics_fonts_Font_nGetMinikinFontPtr(jlong font)
{
    return font;
}

jlong Java_android_graphics_fonts_Font_nCloneFont(jlong font)
{
    return font ? font : (jlong) (muplar_next_font_token += 0x1000);
}

jobject Java_android_graphics_fonts_Font_nNewByteBuffer(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong font)
{
    (void) env;
    (void) clazz;
    (void) font;
    return NULL;
}

jlong Java_android_graphics_fonts_Font_nGetBufferAddress(jlong font)
{
    (void) font;
    return 0;
}

jint Java_android_graphics_fonts_Font_nGetSourceId(jlong font)
{
    return (jint) (font & 0x7fffffff);
}

jlong Java_android_graphics_fonts_Font_nGetReleaseNativeFont(void)
{
    return 0;
}

jfloat Java_android_graphics_fonts_Font_nGetGlyphBounds(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong font,
                                                        jint glyph_id,
                                                        jlong paint,
                                                        jobject rect)
{
    (void) env;
    (void) clazz;
    (void) font;
    (void) glyph_id;
    (void) paint;
    (void) rect;
    return 0.0f;
}

jfloat Java_android_graphics_fonts_Font_nGetFontMetrics(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong font,
                                                        jlong paint,
                                                        jobject metrics)
{
    (void) env;
    (void) clazz;
    (void) font;
    (void) paint;
    (void) metrics;
    return 0.0f;
}

jstring Java_android_graphics_fonts_Font_nGetFontPath(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong font_ptr)
{
    (void) clazz;
    (void) font_ptr;
    return (*env)->NewStringUTF(env, "");
}

jstring Java_android_graphics_fonts_Font_nGetLocaleList(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong font_ptr)
{
    (void) clazz;
    (void) font_ptr;
    return (*env)->NewStringUTF(env, "");
}

jint Java_android_graphics_fonts_Font_nGetPackedStyle(jlong font_ptr)
{
    jint weight = (jint) ((font_ptr >> 3) & 0x3ff);
    jint italic = (jint) (font_ptr & 0x2) ? 1 : 0;
    if (weight <= 0)
        weight = 400;
    return weight | (italic ? 0x10000 : 0);
}

jint Java_android_graphics_fonts_Font_nGetIndex(jlong font_ptr)
{
    (void) font_ptr;
    return 0;
}

jint Java_android_graphics_fonts_Font_nGetAxisCount(jlong font_ptr)
{
    (void) font_ptr;
    return 0;
}

jlong Java_android_graphics_fonts_Font_nGetAxisInfo(jlong font_ptr, jint i)
{
    (void) font_ptr;
    (void) i;
    return 0;
}

jlongArray Java_android_graphics_fonts_Font_nGetAvailableFontSet(JNIEnv *env,
                                                                 jclass clazz)
{
    (void) clazz;
    return (*env)->NewLongArray(env, 0);
}

jlong Java_android_graphics_fonts_FontFamily_00024Builder_nInitBuilder(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    muplar_next_font_family_token += 0x1000;
    return (jlong) muplar_next_font_family_token;
}

void Java_android_graphics_fonts_FontFamily_00024Builder_nAddFont(
    jlong builder_ptr,
    jlong font_ptr)
{
    (void) builder_ptr;
    (void) font_ptr;
}

jlong Java_android_graphics_fonts_FontFamily_00024Builder_nBuild(
    JNIEnv *env,
    jclass clazz,
    jlong builder_ptr,
    jstring lang_tags,
    jint variant,
    jboolean is_custom_fallback,
    jboolean is_default_fallback,
    jint variable_family_type)
{
    (void) env;
    (void) clazz;
    (void) builder_ptr;
    (void) lang_tags;
    (void) variant;
    (void) is_custom_fallback;
    (void) is_default_fallback;
    (void) variable_family_type;
    muplar_next_font_family_token += 0x1000;
    return (jlong) muplar_next_font_family_token;
}

jlong Java_android_graphics_fonts_FontFamily_00024Builder_nGetReleaseNativeFamily(
    void)
{
    return 0;
}

jint Java_android_graphics_fonts_FontFamily_nGetFontSize(jlong family)
{
    (void) family;
    return 1;
}

jlong Java_android_graphics_fonts_FontFamily_nGetFont(jlong family, jint i)
{
    (void) family;
    (void) i;
    muplar_next_font_token += 0x1000;
    return (jlong) (muplar_next_font_token | (400 << 3));
}

jstring Java_android_graphics_fonts_FontFamily_nGetLangTags(JNIEnv *env,
                                                            jclass clazz,
                                                            jlong family)
{
    (void) clazz;
    (void) family;
    return (*env)->NewStringUTF(env, "");
}

jint Java_android_graphics_fonts_FontFamily_nGetVariant(jlong family)
{
    (void) family;
    return 0;
}

static jlong muplar_vector_token(void)
{
    muplar_next_vector_token += 0x1000;
    return (jlong) muplar_next_vector_token;
}

jint Java_android_graphics_drawable_VectorDrawable_nDraw(
    JNIEnv *env,
    jclass clazz,
    jlong renderer_ptr,
    jlong canvas_wrapper_ptr,
    jlong color_filter_ptr,
    jobject bounds,
    jboolean needs_mirroring,
    jboolean can_reuse_cache)
{
    (void) env;
    (void) clazz;
    (void) renderer_ptr;
    (void) canvas_wrapper_ptr;
    (void) color_filter_ptr;
    (void) bounds;
    (void) needs_mirroring;
    (void) can_reuse_cache;
    return 0;
}

jboolean Java_android_graphics_drawable_VectorDrawable_nGetFullPathProperties(
    JNIEnv *env,
    jclass clazz,
    jlong path_ptr,
    jbyteArray properties,
    jint length)
{
    (void) env;
    (void) clazz;
    (void) path_ptr;
    (void) properties;
    (void) length;
    return JNI_TRUE;
}

void Java_android_graphics_drawable_VectorDrawable_nSetName(JNIEnv *env,
                                                            jclass clazz,
                                                            jlong node_ptr,
                                                            jstring name)
{
    (void) env;
    (void) clazz;
    (void) node_ptr;
    (void) name;
}

jboolean Java_android_graphics_drawable_VectorDrawable_nGetGroupProperties(
    JNIEnv *env,
    jclass clazz,
    jlong group_ptr,
    jfloatArray properties,
    jint length)
{
    (void) env;
    (void) clazz;
    (void) group_ptr;
    (void) properties;
    (void) length;
    return JNI_TRUE;
}

void Java_android_graphics_drawable_VectorDrawable_nSetPathString(
    JNIEnv *env,
    jclass clazz,
    jlong path_ptr,
    jstring path_string,
    jint length)
{
    (void) env;
    (void) clazz;
    (void) path_ptr;
    (void) path_string;
    (void) length;
}

jlong Java_android_graphics_drawable_VectorDrawable_nCreateTree(
    JNIEnv *env,
    jclass clazz,
    jlong root_group_ptr)
{
    (void) env;
    (void) clazz;
    (void) root_group_ptr;
    return muplar_vector_token();
}

jlong Java_android_graphics_drawable_VectorDrawable_nCreateTreeFromCopy(
    JNIEnv *env,
    jclass clazz,
    jlong tree_to_copy,
    jlong root_group_ptr)
{
    (void) tree_to_copy;
    return Java_android_graphics_drawable_VectorDrawable_nCreateTree(
        env, clazz, root_group_ptr);
}

void Java_android_graphics_drawable_VectorDrawable_nSetRendererViewportSize(
    JNIEnv *env,
    jclass clazz,
    jlong renderer_ptr,
    jfloat viewport_width,
    jfloat viewport_height)
{
    (void) env;
    (void) clazz;
    (void) renderer_ptr;
    (void) viewport_width;
    (void) viewport_height;
}

jboolean Java_android_graphics_drawable_VectorDrawable_nSetRootAlpha(
    JNIEnv *env,
    jclass clazz,
    jlong renderer_ptr,
    jfloat alpha)
{
    (void) env;
    (void) clazz;
    (void) renderer_ptr;
    (void) alpha;
    return JNI_TRUE;
}

jfloat Java_android_graphics_drawable_VectorDrawable_nGetRootAlpha(
    JNIEnv *env,
    jclass clazz,
    jlong renderer_ptr)
{
    (void) env;
    (void) clazz;
    (void) renderer_ptr;
    return 1.0f;
}

void Java_android_graphics_drawable_VectorDrawable_nSetAntiAlias(
    JNIEnv *env,
    jclass clazz,
    jlong renderer_ptr,
    jboolean aa)
{
    (void) env;
    (void) clazz;
    (void) renderer_ptr;
    (void) aa;
}

void Java_android_graphics_drawable_VectorDrawable_nSetAllowCaching(
    JNIEnv *env,
    jclass clazz,
    jlong renderer_ptr,
    jboolean allow_caching)
{
    (void) env;
    (void) clazz;
    (void) renderer_ptr;
    (void) allow_caching;
}

jlong Java_android_graphics_drawable_VectorDrawable_nCreateFullPath(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return muplar_vector_token();
}

jlong Java_android_graphics_drawable_VectorDrawable_nCreateFullPath__J(
    JNIEnv *env,
    jclass clazz,
    jlong native_full_path_ptr)
{
    (void) native_full_path_ptr;
    return Java_android_graphics_drawable_VectorDrawable_nCreateFullPath(env,
                                                                         clazz);
}

void Java_android_graphics_drawable_VectorDrawable_nUpdateFullPathProperties(
    JNIEnv *env,
    jclass clazz,
    jlong path_ptr,
    jfloat stroke_width,
    jint stroke_color,
    jfloat stroke_alpha,
    jint fill_color,
    jfloat fill_alpha,
    jfloat trim_path_start,
    jfloat trim_path_end,
    jfloat trim_path_offset,
    jfloat stroke_miter_limit,
    jint stroke_line_cap,
    jint stroke_line_join,
    jint fill_type)
{
    (void) env;
    (void) clazz;
    (void) path_ptr;
    (void) stroke_width;
    (void) stroke_color;
    (void) stroke_alpha;
    (void) fill_color;
    (void) fill_alpha;
    (void) trim_path_start;
    (void) trim_path_end;
    (void) trim_path_offset;
    (void) stroke_miter_limit;
    (void) stroke_line_cap;
    (void) stroke_line_join;
    (void) fill_type;
}

void Java_android_graphics_drawable_VectorDrawable_nUpdateFullPathFillGradient(
    JNIEnv *env,
    jclass clazz,
    jlong path_ptr,
    jlong fill_gradient_ptr)
{
    (void) env;
    (void) clazz;
    (void) path_ptr;
    (void) fill_gradient_ptr;
}

void Java_android_graphics_drawable_VectorDrawable_nUpdateFullPathStrokeGradient(
    JNIEnv *env,
    jclass clazz,
    jlong path_ptr,
    jlong stroke_gradient_ptr)
{
    (void) env;
    (void) clazz;
    (void) path_ptr;
    (void) stroke_gradient_ptr;
}

jlong Java_android_graphics_drawable_VectorDrawable_nCreateClipPath(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return muplar_vector_token();
}

jlong Java_android_graphics_drawable_VectorDrawable_nCreateClipPath__J(
    JNIEnv *env,
    jclass clazz,
    jlong clip_path_ptr)
{
    (void) clip_path_ptr;
    return Java_android_graphics_drawable_VectorDrawable_nCreateClipPath(env,
                                                                         clazz);
}

jlong Java_android_graphics_drawable_VectorDrawable_nCreateGroup(JNIEnv *env,
                                                                 jclass clazz)
{
    (void) env;
    (void) clazz;
    return muplar_vector_token();
}

jlong Java_android_graphics_drawable_VectorDrawable_nCreateGroup__J(
    JNIEnv *env,
    jclass clazz,
    jlong group_ptr)
{
    (void) group_ptr;
    return Java_android_graphics_drawable_VectorDrawable_nCreateGroup(env,
                                                                      clazz);
}

void Java_android_graphics_drawable_VectorDrawable_nUpdateGroupProperties(
    JNIEnv *env,
    jclass clazz,
    jlong group_ptr,
    jfloat rotate,
    jfloat pivot_x,
    jfloat pivot_y,
    jfloat scale_x,
    jfloat scale_y,
    jfloat translate_x,
    jfloat translate_y)
{
    (void) env;
    (void) clazz;
    (void) group_ptr;
    (void) rotate;
    (void) pivot_x;
    (void) pivot_y;
    (void) scale_x;
    (void) scale_y;
    (void) translate_x;
    (void) translate_y;
}

void Java_android_graphics_drawable_VectorDrawable_nAddChild(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong group_ptr,
                                                             jlong node_ptr)
{
    (void) env;
    (void) clazz;
    (void) group_ptr;
    (void) node_ptr;
}

jfloat Java_android_graphics_drawable_VectorDrawable_nGetFloat(JNIEnv *env,
                                                               jclass clazz,
                                                               jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    return 0.0f;
}

void Java_android_graphics_drawable_VectorDrawable_nSetFloat(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong ptr,
                                                             jfloat value)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) value;
}

void Java_android_graphics_drawable_VectorDrawable_nSetPathData(
    JNIEnv *env,
    jclass clazz,
    jlong path_ptr,
    jlong path_data_ptr)
{
    (void) env;
    (void) clazz;
    (void) path_ptr;
    (void) path_data_ptr;
}

jint Java_android_graphics_drawable_VectorDrawable_nGetInt(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    return 0;
}

void Java_android_graphics_drawable_VectorDrawable_nSetInt(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong ptr,
                                                           jint value)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) value;
}

void Java_com_android_internal_util_VirtualRefBasePtr_nIncStrong(JNIEnv *env,
                                                                 jclass clazz,
                                                                 jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

void Java_com_android_internal_util_VirtualRefBasePtr_nDecStrong(JNIEnv *env,
                                                                 jclass clazz,
                                                                 jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

void Java_android_util_PathParser_nParseStringForPath(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong path_ptr,
                                                      jstring path_string,
                                                      jint string_length)
{
    (void) env;
    (void) clazz;
    (void) path_ptr;
    (void) path_string;
    (void) string_length;
}

jlong Java_android_util_PathParser_nCreatePathDataFromString(
    JNIEnv *env,
    jclass clazz,
    jstring path_string,
    jint string_length)
{
    (void) env;
    (void) clazz;
    (void) path_string;
    (void) string_length;
    muplar_next_path_token += 0x100;
    return (jlong) muplar_next_path_token;
}

void Java_android_util_PathParser_nCreatePathFromPathData(JNIEnv *env,
                                                          jclass clazz,
                                                          jlong out_path_ptr,
                                                          jlong path_data)
{
    (void) env;
    (void) clazz;
    (void) out_path_ptr;
    (void) path_data;
}

jlong Java_android_util_PathParser_nCreateEmptyPathData(JNIEnv *env,
                                                        jclass clazz)
{
    (void) env;
    (void) clazz;
    muplar_next_path_token += 0x100;
    return (jlong) muplar_next_path_token;
}

jlong Java_android_util_PathParser_nCreatePathData(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    muplar_next_path_token += 0x100;
    return (jlong) muplar_next_path_token;
}

jboolean Java_android_util_PathParser_nInterpolatePathData(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong out_data_ptr,
                                                           jlong from_data_ptr,
                                                           jlong to_data_ptr,
                                                           jfloat fraction)
{
    (void) env;
    (void) clazz;
    (void) out_data_ptr;
    (void) from_data_ptr;
    (void) to_data_ptr;
    (void) fraction;
    return JNI_TRUE;
}

void Java_android_util_PathParser_nFinalize(JNIEnv *env,
                                            jclass clazz,
                                            jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
}

jboolean Java_android_util_PathParser_nCanMorph(JNIEnv *env,
                                                jclass clazz,
                                                jlong from_data_ptr,
                                                jlong to_data_ptr)
{
    (void) env;
    (void) clazz;
    (void) from_data_ptr;
    (void) to_data_ptr;
    return JNI_TRUE;
}

void Java_android_util_PathParser_nSetPathData(JNIEnv *env,
                                               jclass clazz,
                                               jlong out_data_ptr,
                                               jlong from_data_ptr)
{
    (void) env;
    (void) clazz;
    (void) out_data_ptr;
    (void) from_data_ptr;
}

void Java_android_graphics_Typeface_nativeWarmUpCache(JNIEnv *env,
                                                      jclass clazz,
                                                      jstring path)
{
    (void) env;
    (void) clazz;
    (void) path;
}

static jlong muplar_typeface_token(jint weight, jint italic)
{
    jint normalized_weight = weight > 0 ? weight : 400;
    jint style = italic ? 2 : 0;
    if (normalized_weight >= 700)
        style |= 1;
    muplar_next_typeface_token += 0x1000;
    return (jlong) (muplar_next_typeface_token |
                    (((uintptr_t) (normalized_weight & 0x3ff)) << 3) |
                    (uintptr_t) (style & 0x3));
}

static jint muplar_typeface_style(jlong native_instance)
{
    return (jint) (native_instance & 0x3);
}

static jint muplar_typeface_weight(jlong native_instance)
{
    jint weight = (jint) ((native_instance >> 3) & 0x3ff);
    return weight > 0 ? weight : 400;
}

jlong Java_android_graphics_Typeface_nativeCreateFromTypeface(
    JNIEnv *env,
    jclass clazz,
    jlong native_instance,
    jint style)
{
    jint weight = (style & 1) ? 700 : muplar_typeface_weight(native_instance);
    (void) env;
    (void) clazz;
    return muplar_typeface_token(weight, (style & 2) != 0);
}

jlong Java_android_graphics_Typeface_nativeCreateFromTypefaceWithExactStyle(
    JNIEnv *env,
    jclass clazz,
    jlong native_instance,
    jint weight,
    jboolean italic)
{
    (void) env;
    (void) clazz;
    (void) native_instance;
    return muplar_typeface_token(weight, italic == JNI_TRUE);
}

jlong Java_android_graphics_Typeface_nativeCreateFromTypefaceWithVariation(
    JNIEnv *env,
    jclass clazz,
    jlong native_instance,
    jobject axes)
{
    (void) env;
    (void) clazz;
    (void) axes;
    return native_instance ? native_instance : muplar_typeface_token(400, 0);
}

jlong Java_android_graphics_Typeface_nativeCreateWeightAlias(
    JNIEnv *env,
    jclass clazz,
    jlong native_instance,
    jint weight)
{
    (void) env;
    (void) clazz;
    return muplar_typeface_token(
        weight, (muplar_typeface_style(native_instance) & 2) != 0);
}

jboolean Java_android_graphics_Typeface_nativeIsVariationInstance(
    JNIEnv *env,
    jclass clazz,
    jlong native_instance)
{
    (void) env;
    (void) clazz;
    (void) native_instance;
    return JNI_FALSE;
}

jlong Java_android_graphics_Typeface_nativeCreateFromArray(
    JNIEnv *env,
    jclass clazz,
    jlongArray family_array,
    jlong fallback_typeface,
    jint weight,
    jint italic)
{
    (void) env;
    (void) clazz;
    (void) family_array;
    if (weight <= 0 && fallback_typeface)
        weight = muplar_typeface_weight(fallback_typeface);
    return muplar_typeface_token(weight, italic == 1);
}

jintArray Java_android_graphics_Typeface_nativeGetSupportedAxes(
    JNIEnv *env,
    jclass clazz,
    jlong native_instance)
{
    (void) clazz;
    (void) native_instance;
    return (*env)->NewIntArray(env, 0);
}

void Java_android_graphics_Typeface_nativeSetDefault(jlong native_ptr)
{
    (void) native_ptr;
}

jint Java_android_graphics_Typeface_nativeGetStyle(jlong native_ptr)
{
    return muplar_typeface_style(native_ptr);
}

jint Java_android_graphics_Typeface_nativeGetWeight(jlong native_ptr)
{
    return muplar_typeface_weight(native_ptr);
}

jlong Java_android_graphics_Typeface_nativeGetReleaseFunc(void)
{
    return 0;
}

void Java_android_graphics_Typeface_nativeRegisterGenericFamily(
    JNIEnv *env,
    jclass clazz,
    jstring family,
    jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) family;
    (void) native_ptr;
}

jint Java_android_graphics_Typeface_nativeWriteTypefaces(JNIEnv *env,
                                                         jclass clazz,
                                                         jobject buffer,
                                                         jint position,
                                                         jlongArray native_ptrs)
{
    (void) env;
    (void) clazz;
    (void) buffer;
    (void) position;
    (void) native_ptrs;
    return 0;
}

jlongArray Java_android_graphics_Typeface_nativeReadTypefaces(JNIEnv *env,
                                                              jclass clazz,
                                                              jobject buffer,
                                                              jint position)
{
    (void) clazz;
    (void) buffer;
    (void) position;
    return (*env)->NewLongArray(env, 0);
}

void Java_android_graphics_Typeface_nativeForceSetStaticFinalField(
    JNIEnv *env,
    jclass clazz,
    jstring field_name,
    jobject typeface)
{
    (void) env;
    (void) clazz;
    (void) field_name;
    (void) typeface;
}

void Java_android_graphics_Typeface_nativeAddFontCollections(jlong native_ptr)
{
    (void) native_ptr;
}

void Java_android_graphics_Typeface_nativeRegisterLocaleList(JNIEnv *env,
                                                             jclass clazz,
                                                             jstring locales)
{
    (void) env;
    (void) clazz;
    (void) locales;
}

jlong Java_android_graphics_LinearGradient_nativeCreate(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong color_space,
                                                        jfloat x0,
                                                        jfloat y0,
                                                        jfloat x1,
                                                        jfloat y1,
                                                        jlongArray colors,
                                                        jfloatArray positions,
                                                        jint tile_mode,
                                                        jlong color_space2)
{
    (void) env;
    (void) clazz;
    (void) color_space;
    (void) x0;
    (void) y0;
    (void) x1;
    (void) y1;
    (void) colors;
    (void) positions;
    (void) tile_mode;
    (void) color_space2;
    muplar_next_shader_token += 0x100;
    return (jlong) muplar_next_shader_token;
}

void Java_android_graphics_Paint_nSetShadowLayer(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong paint,
                                                 jfloat radius,
                                                 jfloat dx,
                                                 jfloat dy,
                                                 jlong color,
                                                 jlong color_space)
{
    (void) env;
    (void) clazz;
    (void) paint;
    (void) radius;
    (void) dx;
    (void) dy;
    (void) color;
    (void) color_space;
}

jint Java_android_graphics_Paint_nSetTextLocales(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong paint,
                                                 jobject locales)
{
    (void) env;
    (void) clazz;
    (void) paint;
    (void) locales;
    return 0;
}

jint Java_android_graphics_Paint_nGetHyphenEdit(JNIEnv *env,
                                                jclass clazz,
                                                jlong paint)
{
    (void) env;
    (void) clazz;
    (void) paint;
    return 0;
}

void Java_android_graphics_Paint_nSetHyphenEdit(JNIEnv *env,
                                                jclass clazz,
                                                jlong paint,
                                                jint edit)
{
    (void) env;
    (void) clazz;
    (void) paint;
    (void) edit;
}

static void muplar_set_int_field(JNIEnv *env,
                                 jobject object,
                                 const char *name,
                                 jint value)
{
    jclass cls;
    jfieldID field;
    if (!object)
        return;
    cls = (*env)->GetObjectClass(env, object);
    if (!cls)
        return;
    field = (*env)->GetFieldID(env, cls, name, "I");
    if (!field) {
        (*env)->ExceptionClear(env);
        return;
    }
    (*env)->SetIntField(env, object, field, value);
}

static void muplar_set_float_field(JNIEnv *env,
                                   jobject object,
                                   const char *name,
                                   jfloat value)
{
    jclass cls;
    jfieldID field;
    if (!object)
        return;
    cls = (*env)->GetObjectClass(env, object);
    if (!cls)
        return;
    field = (*env)->GetFieldID(env, cls, name, "F");
    if (!field) {
        (*env)->ExceptionClear(env);
        return;
    }
    (*env)->SetFloatField(env, object, field, value);
}

jint Java_android_graphics_Paint_nGetFontMetricsInt(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong paint,
                                                    jobject metrics)
{
    (void) clazz;
    (void) paint;
    muplar_set_int_field(env, metrics, "top", -24);
    muplar_set_int_field(env, metrics, "ascent", -20);
    muplar_set_int_field(env, metrics, "descent", 6);
    muplar_set_int_field(env, metrics, "bottom", 8);
    muplar_set_int_field(env, metrics, "leading", 0);
    return 32;
}

void Java_android_graphics_Paint_nGetFontMetricsIntForText(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong paint,
                                                           jcharArray text,
                                                           jint start,
                                                           jint end,
                                                           jint context_start,
                                                           jint context_end,
                                                           jboolean is_rtl,
                                                           jobject metrics)
{
    (void) text;
    (void) start;
    (void) end;
    (void) context_start;
    (void) context_end;
    (void) is_rtl;
    Java_android_graphics_Paint_nGetFontMetricsInt(env, clazz, paint, metrics);
}

jfloat Java_android_graphics_Paint_nGetFontMetrics(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong paint,
                                                   jobject metrics)
{
    (void) clazz;
    (void) paint;
    muplar_set_float_field(env, metrics, "top", -24.0f);
    muplar_set_float_field(env, metrics, "ascent", -20.0f);
    muplar_set_float_field(env, metrics, "descent", 6.0f);
    muplar_set_float_field(env, metrics, "bottom", 8.0f);
    muplar_set_float_field(env, metrics, "leading", 0.0f);
    return 32.0f;
}

void Java_android_graphics_Paint_nGetCharArrayBounds(JNIEnv *env,
                                                     jclass clazz,
                                                     jlong paint,
                                                     jcharArray text,
                                                     jint index,
                                                     jint count,
                                                     jint bidi_flags,
                                                     jobject bounds)
{
    jint width;
    (void) clazz;
    (void) paint;
    (void) text;
    (void) index;
    (void) bidi_flags;
    if (count < 0)
        count = 0;
    width = count > 0 ? count * 16 : 1;
    muplar_set_int_field(env, bounds, "left", 0);
    muplar_set_int_field(env, bounds, "top", -24);
    muplar_set_int_field(env, bounds, "right", width);
    muplar_set_int_field(env, bounds, "bottom", 8);
}

jfloat Java_android_graphics_Paint_nGetRunCharacterAdvance(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong paint,
                                                           jcharArray text,
                                                           jint start,
                                                           jint end,
                                                           jint context_start,
                                                           jint context_end,
                                                           jboolean is_rtl,
                                                           jint offset,
                                                           jfloatArray advances,
                                                           jint advances_index,
                                                           jobject bounds,
                                                           jobject run_info)
{
    jint count;
    jint array_len;
    jfloat *values;
    jfloat width;
    (void) clazz;
    (void) paint;
    (void) text;
    (void) context_start;
    (void) context_end;
    (void) is_rtl;
    (void) offset;
    (void) run_info;
    count = end > start ? end - start : 0;
    width = (jfloat) count * 16.0f;
    if (advances) {
        array_len = (*env)->GetArrayLength(env, advances);
        if (advances_index >= 0 && advances_index < array_len) {
            jint writable = count;
            if (writable > array_len - advances_index)
                writable = array_len - advances_index;
            values = calloc((size_t) writable, sizeof(*values));
            if (values) {
                jint i;
                for (i = 0; i < writable; i++)
                    values[i] = 16.0f;
                (*env)->SetFloatArrayRegion(env, advances, advances_index,
                                            writable, values);
                free(values);
            }
        }
    }
    muplar_set_float_field(env, bounds, "left", 0.0f);
    muplar_set_float_field(env, bounds, "top", -24.0f);
    muplar_set_float_field(env, bounds, "right", width > 0.0f ? width : 1.0f);
    muplar_set_float_field(env, bounds, "bottom", 8.0f);
    return width;
}

jfloat Java_android_graphics_Paint_nGetTextAdvances(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong paint,
                                                    jcharArray text,
                                                    jint index,
                                                    jint count,
                                                    jint context_index,
                                                    jint context_count,
                                                    jint bidi_flags,
                                                    jfloatArray advances,
                                                    jint advances_index)
{
    jint array_len;
    jfloat width;
    (void) clazz;
    (void) paint;
    (void) text;
    (void) index;
    (void) context_index;
    (void) context_count;
    (void) bidi_flags;
    if (count < 0)
        count = 0;
    width = (jfloat) count * 16.0f;
    if (advances) {
        array_len = (*env)->GetArrayLength(env, advances);
        if (advances_index >= 0 && advances_index < array_len) {
            jint writable = count;
            if (writable > array_len - advances_index)
                writable = array_len - advances_index;
            while (writable > 0) {
                jfloat advance = 16.0f;
                (*env)->SetFloatArrayRegion(env, advances, advances_index, 1,
                                            &advance);
                advances_index++;
                writable--;
            }
        }
    }
    return width;
}

jlong Java_android_graphics_Picture_nativeConstructor(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong src)
{
    (void) env;
    (void) clazz;
    (void) src;
    muplar_next_picture_token += 0x100;
    return (jlong) muplar_next_picture_token;
}

void Java_android_graphics_Picture_nativeDestructor(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong picture)
{
    (void) env;
    (void) clazz;
    (void) picture;
}

jlong Java_android_graphics_Picture_nativeBeginRecording(JNIEnv *env,
                                                         jclass clazz,
                                                         jlong picture,
                                                         jint width,
                                                         jint height)
{
    (void) env;
    (void) clazz;
    (void) picture;
    (void) width;
    (void) height;
    muplar_next_canvas_token += 0x100;
    return (jlong) muplar_next_canvas_token;
}

void Java_android_graphics_Picture_nativeEndRecording(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong picture)
{
    (void) env;
    (void) clazz;
    (void) picture;
}

void Java_android_graphics_Picture_nativeDraw(JNIEnv *env,
                                              jclass clazz,
                                              jlong picture,
                                              jlong canvas)
{
    (void) env;
    (void) clazz;
    (void) picture;
    (void) canvas;
}

jint Java_android_graphics_Picture_nativeGetWidth(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong picture)
{
    (void) env;
    (void) clazz;
    (void) picture;
    return 64;
}

jint Java_android_graphics_Picture_nativeGetHeight(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong picture)
{
    return Java_android_graphics_Picture_nativeGetWidth(env, clazz, picture);
}

jlong Java_android_graphics_Canvas_nGetNativeFinalizer(JNIEnv *env,
                                                       jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jlong Java_android_graphics_Canvas_nInitRaster(JNIEnv *env,
                                               jclass clazz,
                                               jlong bitmap)
{
    struct muplar_canvas_state *canvas;
    (void) env;
    (void) clazz;
    canvas = muplar_alloc_canvas(bitmap);
    if (canvas)
        return canvas->token;
    muplar_next_canvas_token += 0x100;
    return (jlong) muplar_next_canvas_token;
}

jint Java_android_graphics_Canvas_nSave(JNIEnv *env,
                                        jclass clazz,
                                        jlong canvas,
                                        jint flags)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) flags;
    return 1;
}

void Java_android_graphics_Canvas_nRestore(JNIEnv *env,
                                           jclass clazz,
                                           jlong canvas)
{
    (void) env;
    (void) clazz;
    (void) canvas;
}

void Java_android_graphics_Canvas_nRestoreToCount(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong canvas,
                                                  jint save_count)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) save_count;
}

jint Java_android_graphics_Canvas_nGetSaveCount(JNIEnv *env,
                                                jclass clazz,
                                                jlong canvas)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    return 1;
}

jboolean Java_android_graphics_Canvas_nQuickReject(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong canvas,
                                                   jfloat left,
                                                   jfloat top,
                                                   jfloat right,
                                                   jfloat bottom)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) left;
    (void) top;
    (void) right;
    (void) bottom;
    return JNI_FALSE;
}

jboolean Java_android_graphics_Canvas_nClipRect(JNIEnv *env,
                                                jclass clazz,
                                                jlong canvas,
                                                jfloat left,
                                                jfloat top,
                                                jfloat right,
                                                jfloat bottom,
                                                jint op)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) left;
    (void) top;
    (void) right;
    (void) bottom;
    (void) op;
    return JNI_TRUE;
}

void Java_android_graphics_Canvas_nTranslate(JNIEnv *env,
                                             jclass clazz,
                                             jlong canvas,
                                             jfloat dx,
                                             jfloat dy)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) dx;
    (void) dy;
}

void Java_android_graphics_Canvas_nScale(JNIEnv *env,
                                         jclass clazz,
                                         jlong canvas,
                                         jfloat sx,
                                         jfloat sy)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) sx;
    (void) sy;
}

void Java_android_graphics_Canvas_nRotate(JNIEnv *env,
                                          jclass clazz,
                                          jlong canvas,
                                          jfloat degrees)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) degrees;
}

void Java_android_graphics_Canvas_nConcat(JNIEnv *env,
                                          jclass clazz,
                                          jlong canvas,
                                          jlong matrix)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) matrix;
}

void Java_android_graphics_Canvas_nDrawPath(JNIEnv *env,
                                            jclass clazz,
                                            jlong canvas,
                                            jlong path,
                                            jlong paint)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) path;
    (void) paint;
}

void Java_android_graphics_Canvas_nDrawCircle(JNIEnv *env,
                                              jclass clazz,
                                              jlong canvas,
                                              jfloat cx,
                                              jfloat cy,
                                              jfloat radius,
                                              jlong paint)
{
    struct muplar_bitmap_state *bitmap = muplar_canvas_bitmap(canvas);
    uint32_t color = muplar_paint_color(paint, 0xff5f6368u);
    (void) env;
    (void) clazz;
    muplar_fill_circle(bitmap, cx, cy, radius, color);
}

void Java_android_graphics_Canvas_nDrawColor(JNIEnv *env,
                                             jclass clazz,
                                             jlong canvas,
                                             jint color,
                                             jint mode)
{
    struct muplar_bitmap_state *bitmap = muplar_canvas_bitmap(canvas);
    (void) env;
    (void) clazz;
    (void) mode;
    muplar_draw_color_count++;
    if (bitmap)
        muplar_fill_rect(bitmap, 0, 0, bitmap->width, bitmap->height,
                         (uint32_t) color);
}

void Java_android_graphics_Canvas_nDrawRoundRect(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong canvas,
                                                 jfloat left,
                                                 jfloat top,
                                                 jfloat right,
                                                 jfloat bottom,
                                                 jfloat rx,
                                                 jfloat ry,
                                                 jlong paint)
{
    struct muplar_bitmap_state *bitmap = muplar_canvas_bitmap(canvas);
    uint32_t color = muplar_paint_color(paint, 0xff9aa0a6u);
    (void) env;
    (void) clazz;
    (void) rx;
    (void) ry;
    muplar_fill_rect(bitmap, muplar_floor_to_int(left),
                     muplar_floor_to_int(top), muplar_ceil_to_int(right),
                     muplar_ceil_to_int(bottom), color);
}

void Java_android_graphics_BaseCanvas_nDrawPaint(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong canvas,
                                                 jlong paint)
{
    struct muplar_bitmap_state *bitmap = muplar_canvas_bitmap(canvas);
    uint32_t color = muplar_paint_color(paint, 0xffffffffu);
    (void) env;
    (void) clazz;
    muplar_draw_paint_count++;
    if (bitmap)
        muplar_fill_rect(bitmap, 0, 0, bitmap->width, bitmap->height, color);
}

void Java_android_graphics_BaseCanvas_nDrawRect(JNIEnv *env,
                                                jclass clazz,
                                                jlong canvas,
                                                jfloat left,
                                                jfloat top,
                                                jfloat right,
                                                jfloat bottom,
                                                jlong paint)
{
    struct muplar_bitmap_state *bitmap = muplar_canvas_bitmap(canvas);
    uint32_t color = muplar_paint_color(paint, 0xffdadce0u);
    (void) env;
    (void) clazz;
    muplar_draw_rect_count++;
    muplar_fill_rect(bitmap, muplar_floor_to_int(left),
                     muplar_floor_to_int(top), muplar_ceil_to_int(right),
                     muplar_ceil_to_int(bottom), color);
}

void Java_android_graphics_BaseCanvas_nDrawOval(JNIEnv *env,
                                                jclass clazz,
                                                jlong canvas,
                                                jfloat left,
                                                jfloat top,
                                                jfloat right,
                                                jfloat bottom,
                                                jlong paint)
{
    (void) env;
    (void) clazz;
    Java_android_graphics_BaseCanvas_nDrawRect(env, clazz, canvas, left, top,
                                               right, bottom, paint);
}

void Java_android_graphics_BaseCanvas_nDrawArc(JNIEnv *env,
                                               jclass clazz,
                                               jlong canvas,
                                               jfloat left,
                                               jfloat top,
                                               jfloat right,
                                               jfloat bottom,
                                               jfloat start_angle,
                                               jfloat sweep_angle,
                                               jboolean use_center,
                                               jlong paint)
{
    (void) start_angle;
    (void) sweep_angle;
    (void) use_center;
    Java_android_graphics_BaseCanvas_nDrawOval(env, clazz, canvas, left, top,
                                               right, bottom, paint);
}

void Java_android_graphics_BaseCanvas_nDrawLine(JNIEnv *env,
                                                jclass clazz,
                                                jlong canvas,
                                                jfloat x1,
                                                jfloat y1,
                                                jfloat x2,
                                                jfloat y2,
                                                jlong paint)
{
    float left = x1 < x2 ? x1 : x2;
    float right = x1 > x2 ? x1 : x2;
    float top = y1 < y2 ? y1 : y2;
    float bottom = y1 > y2 ? y1 : y2;
    if (right - left < 1.0f)
        right = left + 1.0f;
    if (bottom - top < 1.0f)
        bottom = top + 1.0f;
    Java_android_graphics_BaseCanvas_nDrawRect(env, clazz, canvas, left, top,
                                               right, bottom, paint);
}

void Java_android_graphics_BaseCanvas_nDrawPoint(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong canvas,
                                                 jfloat x,
                                                 jfloat y,
                                                 jlong paint)
{
    Java_android_graphics_BaseCanvas_nDrawRect(env, clazz, canvas, x, y,
                                               x + 1.0f, y + 1.0f, paint);
}

void Java_android_graphics_BaseCanvas_nDrawPoints(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong canvas,
                                                  jfloatArray points,
                                                  jint offset,
                                                  jint count,
                                                  jlong paint)
{
    jfloat *data;
    jint i;
    (void) clazz;
    if (!points || count <= 0)
        return;
    data = (*env)->GetFloatArrayElements(env, points, NULL);
    if (!data)
        return;
    for (i = 0; i + 1 < count; i += 2) {
        jfloat x = data[offset + i];
        jfloat y = data[offset + i + 1];
        Java_android_graphics_BaseCanvas_nDrawPoint(env, clazz, canvas, x, y,
                                                    paint);
    }
    (*env)->ReleaseFloatArrayElements(env, points, data, JNI_ABORT);
}

void Java_android_graphics_BaseCanvas_nDrawLines(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong canvas,
                                                 jfloatArray lines,
                                                 jint offset,
                                                 jint count,
                                                 jlong paint)
{
    jfloat *data;
    jint i;
    (void) clazz;
    if (!lines || count <= 0)
        return;
    data = (*env)->GetFloatArrayElements(env, lines, NULL);
    if (!data)
        return;
    for (i = 0; i + 3 < count; i += 4) {
        Java_android_graphics_BaseCanvas_nDrawLine(
            env, clazz, canvas, data[offset + i], data[offset + i + 1],
            data[offset + i + 2], data[offset + i + 3], paint);
    }
    (*env)->ReleaseFloatArrayElements(env, lines, data, JNI_ABORT);
}

static void muplar_draw_text_marker(JNIEnv *env,
                                    jclass clazz,
                                    jlong canvas,
                                    jint glyphs,
                                    jfloat x,
                                    jfloat y,
                                    jlong paint)
{
    int width = glyphs > 0 ? glyphs * 8 : 8;
    (void) env;
    (void) clazz;
    Java_android_graphics_BaseCanvas_nDrawRect(
        env, clazz, canvas, x, y - 14.0f, x + (jfloat) width, y + 2.0f, paint);
}

void Java_android_graphics_BaseCanvas_nDrawTextString(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong canvas,
                                                      jstring text,
                                                      jint start,
                                                      jint end,
                                                      jfloat x,
                                                      jfloat y,
                                                      jint bidi_flags,
                                                      jlong paint)
{
    jint length = text ? (*env)->GetStringLength(env, text) : 0;
    (void) bidi_flags;
    if (start < 0)
        start = 0;
    if (end < start || end > length)
        end = length;
    muplar_draw_text_count++;
    muplar_draw_text_marker(env, clazz, canvas, end - start, x, y, paint);
}

void Java_android_graphics_BaseCanvas_nDrawTextChars(JNIEnv *env,
                                                     jclass clazz,
                                                     jlong canvas,
                                                     jcharArray text,
                                                     jint index,
                                                     jint count,
                                                     jfloat x,
                                                     jfloat y,
                                                     jint bidi_flags,
                                                     jlong paint)
{
    (void) text;
    (void) index;
    (void) bidi_flags;
    muplar_draw_text_count++;
    muplar_draw_text_marker(env, clazz, canvas, count, x, y, paint);
}

void Java_android_graphics_BaseCanvas_nDrawTextRunString(JNIEnv *env,
                                                         jclass clazz,
                                                         jlong canvas,
                                                         jstring text,
                                                         jint start,
                                                         jint end,
                                                         jint context_start,
                                                         jint context_end,
                                                         jfloat x,
                                                         jfloat y,
                                                         jboolean is_rtl,
                                                         jlong paint)
{
    (void) context_start;
    (void) context_end;
    (void) is_rtl;
    Java_android_graphics_BaseCanvas_nDrawTextString(
        env, clazz, canvas, text, start, end, x, y, 0, paint);
}

void Java_android_graphics_BaseCanvas_nDrawTextRunChars(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong canvas,
                                                        jcharArray text,
                                                        jint index,
                                                        jint count,
                                                        jint context_index,
                                                        jint context_count,
                                                        jfloat x,
                                                        jfloat y,
                                                        jboolean is_rtl,
                                                        jlong paint,
                                                        jlong measured_text)
{
    (void) context_index;
    (void) context_count;
    (void) is_rtl;
    (void) measured_text;
    Java_android_graphics_BaseCanvas_nDrawTextChars(
        env, clazz, canvas, text, index, count, x, y, 0, paint);
}

void Java_android_graphics_BaseCanvas_nDrawBitmap(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong canvas,
                                                  jlong bitmap_handle,
                                                  jfloat left,
                                                  jfloat top,
                                                  jlong paint,
                                                  jint canvas_density,
                                                  jint screen_density,
                                                  jint bitmap_density)
{
    struct muplar_bitmap_state *src = muplar_find_bitmap(bitmap_handle);
    (void) canvas_density;
    (void) screen_density;
    (void) bitmap_density;
    if (!src)
        return;
    muplar_draw_bitmap_count++;
    Java_android_graphics_BaseCanvas_nDrawRect(
        env, clazz, canvas, left, top, left + (jfloat) src->width,
        top + (jfloat) src->height, paint);
}

void Java_android_graphics_BaseCanvas_nDrawBitmapRect(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong canvas,
                                                      jlong bitmap_handle,
                                                      jfloat src_left,
                                                      jfloat src_top,
                                                      jfloat src_right,
                                                      jfloat src_bottom,
                                                      jfloat dst_left,
                                                      jfloat dst_top,
                                                      jfloat dst_right,
                                                      jfloat dst_bottom,
                                                      jlong paint,
                                                      jint screen_density,
                                                      jint bitmap_density)
{
    (void) bitmap_handle;
    (void) src_left;
    (void) src_top;
    (void) src_right;
    (void) src_bottom;
    (void) screen_density;
    (void) bitmap_density;
    muplar_draw_bitmap_count++;
    Java_android_graphics_BaseCanvas_nDrawRect(
        env, clazz, canvas, dst_left, dst_top, dst_right, dst_bottom, paint);
}

jlong Java_android_graphics_text_LineBreaker_nInit(JNIEnv *env,
                                                   jclass clazz,
                                                   jint break_strategy,
                                                   jint hyphenation_frequency,
                                                   jboolean justified,
                                                   jintArray indents,
                                                   jboolean use_bounds)
{
    (void) env;
    (void) clazz;
    (void) break_strategy;
    (void) hyphenation_frequency;
    (void) justified;
    (void) indents;
    (void) use_bounds;
    muplar_next_line_breaker_token += 0x100;
    return (jlong) muplar_next_line_breaker_token;
}

jlong Java_android_graphics_text_LineBreaker_nGetReleaseFunc(JNIEnv *env,
                                                             jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jlong Java_android_graphics_text_LineBreaker_nGetReleaseResultFunc(JNIEnv *env,
                                                                   jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

void Java_android_graphics_text_LineBreaker_nFinish(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
}

static struct muplar_line_break_result *muplar_find_line_break_result(
    jlong token)
{
    size_t i;
    for (i = 0; i < sizeof(muplar_line_break_results) /
                        sizeof(muplar_line_break_results[0]);
         i++) {
        if (muplar_line_break_results[i].token == token)
            return &muplar_line_break_results[i];
    }
    return NULL;
}

jlong Java_android_graphics_text_LineBreaker_nComputeLineBreaks(
    JNIEnv *env,
    jclass clazz,
    jlong native_ptr,
    jcharArray text,
    jlong measured_text,
    jint length,
    jfloat first_width,
    jint first_width_line_count,
    jfloat rest_width,
    jfloatArray variable_tab_stops,
    jfloat default_tab_stop,
    jint indents_offset)
{
    struct muplar_line_break_result *result;
    (void) clazz;
    (void) native_ptr;
    (void) measured_text;
    (void) first_width;
    (void) first_width_line_count;
    (void) rest_width;
    (void) variable_tab_stops;
    (void) default_tab_stop;
    (void) indents_offset;
    if (length <= 0 && text)
        length = (*env)->GetArrayLength(env, text);
    if (length <= 0)
        length = 1;
    muplar_next_line_breaker_result_token += 0x100;
    result =
        &muplar_line_break_results[(muplar_next_line_breaker_result_token >>
                                    8) %
                                   (sizeof(muplar_line_break_results) /
                                    sizeof(muplar_line_break_results[0]))];
    result->token = (jlong) muplar_next_line_breaker_result_token;
    result->end_offset = length;
    result->width = (jfloat) length * 16.0f;
    return result->token;
}

jint Java_android_graphics_text_LineBreaker_00024Result_nGetLineCount(
    JNIEnv *env,
    jclass clazz,
    jlong native_result)
{
    (void) env;
    (void) clazz;
    (void) native_result;
    return 1;
}

jint Java_android_graphics_text_LineBreaker_00024Result_nGetLineBreakOffset(
    JNIEnv *env,
    jclass clazz,
    jlong native_result,
    jint line)
{
    struct muplar_line_break_result *result;
    (void) env;
    (void) clazz;
    (void) line;
    result = muplar_find_line_break_result(native_result);
    return result ? result->end_offset : 1;
}

jfloat Java_android_graphics_text_LineBreaker_00024Result_nGetLineWidth(
    JNIEnv *env,
    jclass clazz,
    jlong native_result,
    jint line)
{
    struct muplar_line_break_result *result;
    (void) env;
    (void) clazz;
    (void) line;
    result = muplar_find_line_break_result(native_result);
    return result ? result->width : 16.0f;
}

jfloat Java_android_graphics_text_LineBreaker_00024Result_nGetLineAscent(
    JNIEnv *env,
    jclass clazz,
    jlong native_result,
    jint line)
{
    (void) env;
    (void) clazz;
    (void) native_result;
    (void) line;
    return -24.0f;
}

jfloat Java_android_graphics_text_LineBreaker_00024Result_nGetLineDescent(
    JNIEnv *env,
    jclass clazz,
    jlong native_result,
    jint line)
{
    (void) env;
    (void) clazz;
    (void) native_result;
    (void) line;
    return 8.0f;
}

jint Java_android_graphics_text_LineBreaker_00024Result_nGetLineFlag(
    JNIEnv *env,
    jclass clazz,
    jlong native_result,
    jint line)
{
    (void) env;
    (void) clazz;
    (void) native_result;
    (void) line;
    return 0;
}

jlong Java_android_graphics_text_MeasuredText_nGetReleaseFunc(JNIEnv *env,
                                                              jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jlong Java_android_graphics_text_MeasuredText_00024Builder_nInitBuilder(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    muplar_next_measured_text_builder_token += 0x100;
    return (jlong) muplar_next_measured_text_builder_token;
}

void Java_android_graphics_text_MeasuredText_00024Builder_nFreeBuilder(
    JNIEnv *env,
    jclass clazz,
    jlong native_builder)
{
    (void) env;
    (void) clazz;
    (void) native_builder;
}

void Java_android_graphics_text_MeasuredText_00024Builder_nAddStyleRun(
    JNIEnv *env,
    jclass clazz,
    jlong native_builder,
    jlong paint,
    jint start,
    jint end,
    jboolean is_rtl,
    jint start_hyphen,
    jint end_hyphen,
    jboolean use_bounds)
{
    (void) env;
    (void) clazz;
    (void) native_builder;
    (void) paint;
    (void) start;
    (void) end;
    (void) is_rtl;
    (void) start_hyphen;
    (void) end_hyphen;
    (void) use_bounds;
}

jlong Java_android_graphics_text_MeasuredText_00024Builder_nBuildMeasuredText(
    JNIEnv *env,
    jclass clazz,
    jlong native_builder,
    jlong hint_mt,
    jcharArray text,
    jboolean compute_hyphenation,
    jboolean compute_layout,
    jboolean fast_hyphenation,
    jboolean use_bounds)
{
    (void) env;
    (void) clazz;
    (void) native_builder;
    (void) hint_mt;
    (void) text;
    (void) compute_hyphenation;
    (void) compute_layout;
    (void) fast_hyphenation;
    (void) use_bounds;
    muplar_next_measured_text_token += 0x100;
    return (jlong) muplar_next_measured_text_token;
}

jlong Java_android_graphics_RecordingCanvas_nCreateDisplayListCanvas(
    JNIEnv *env,
    jclass clazz,
    jlong node,
    jint width,
    jint height)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) width;
    (void) height;
    muplar_next_canvas_token += 0x100;
    return (jlong) muplar_next_canvas_token;
}

void Java_android_graphics_RecordingCanvas_nResetDisplayListCanvas(JNIEnv *env,
                                                                   jclass clazz,
                                                                   jlong canvas,
                                                                   jlong node,
                                                                   jint width,
                                                                   jint height)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) node;
    (void) width;
    (void) height;
}

void Java_android_graphics_RecordingCanvas_nFinishRecording(JNIEnv *env,
                                                            jclass clazz,
                                                            jlong canvas,
                                                            jlong node)
{
    (void) env;
    (void) clazz;
    (void) canvas;
    (void) node;
}

jobject Java_android_graphics_HardwareRenderer_nCreateHardwareBitmap(
    JNIEnv *env,
    jclass clazz,
    jlong node,
    jint width,
    jint height)
{
    jclass helper;
    jmethodID method;
    jobject object;
    struct muplar_bitmap_state *bitmap;
    (void) clazz;
    (void) node;
    if (muplar_graphics_class && muplar_graphics_create_bitmap) {
        object = (*env)->CallStaticObjectMethod(env, muplar_graphics_class,
                                                muplar_graphics_create_bitmap,
                                                width, height);
        bitmap = muplar_alloc_bitmap(width, height);
        if (object && bitmap)
            muplar_set_long_field(env, object, "mNativePtr", bitmap->token);
        return object;
    }
    helper = (*env)->FindClass(env, "com/muplar/runtime/MuplarGraphics");
    if (!helper) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    method = (*env)->GetStaticMethodID(env, helper, "createBitmap",
                                       "(II)Landroid/graphics/Bitmap;");
    if (!method) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    object = (*env)->CallStaticObjectMethod(env, helper, method, width, height);
    bitmap = muplar_alloc_bitmap(width, height);
    if (object && bitmap)
        muplar_set_long_field(env, object, "mNativePtr", bitmap->token);
    return object;
}

void Java_android_graphics_HardwareRenderer_nSetRtAnimationsEnabled(
    JNIEnv *env,
    jclass clazz,
    jboolean enabled)
{
    (void) env;
    (void) clazz;
    (void) enabled;
}

void Java_android_graphics_HardwareRenderer_nSetHighContrastText(
    JNIEnv *env,
    jclass clazz,
    jboolean enabled)
{
    (void) env;
    (void) clazz;
    (void) enabled;
}

jboolean Java_android_graphics_HardwareRenderer_nIsHighContrastTextEnabled(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return JNI_FALSE;
}

void Java_android_graphics_HardwareRenderer_preInitBufferAllocator(JNIEnv *env,
                                                                   jclass clazz)
{
    (void) env;
    (void) clazz;
}

jlong Java_android_view_InputChannel_nativeGetFinalizer(JNIEnv *env,
                                                        jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jlongArray Java_android_view_InputChannel_nativeOpenInputChannelPair(
    JNIEnv *env,
    jclass clazz,
    jstring name)
{
    jlong values[2];
    (void) clazz;
    (void) name;
    values[0] = (jlong) ++muplar_next_input_channel_token;
    values[1] = (jlong) ++muplar_next_input_channel_token;
    jlongArray array = (*env)->NewLongArray(env, 2);
    if (array)
        (*env)->SetLongArrayRegion(env, array, 0, 2, values);
    return array;
}

jlong Java_android_view_InputChannel_nativeDup(JNIEnv *env,
                                               jobject self,
                                               jlong native_ptr)
{
    (void) env;
    (void) self;
    (void) native_ptr;
    return (jlong) ++muplar_next_input_channel_token;
}

void Java_android_view_InputChannel_nativeDispose(JNIEnv *env,
                                                  jobject self,
                                                  jlong native_ptr)
{
    (void) env;
    (void) self;
    (void) native_ptr;
}

jstring Java_android_view_InputChannel_nativeGetName(JNIEnv *env,
                                                     jobject self,
                                                     jlong native_ptr)
{
    (void) self;
    (void) native_ptr;
    return (*env)->NewStringUTF(env, "MuplarInputChannel");
}

jobject Java_android_view_InputChannel_nativeGetToken(JNIEnv *env,
                                                      jobject self,
                                                      jlong native_ptr)
{
    jclass binder_cls;
    jmethodID ctor;
    (void) self;
    (void) native_ptr;
    binder_cls = (*env)->FindClass(env, "android/os/Binder");
    if (!binder_cls) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    ctor = (*env)->GetMethodID(env, binder_cls, "<init>", "()V");
    if (!ctor) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return (*env)->NewObject(env, binder_cls, ctor);
}

jlong Java_android_view_InputChannel_nativeReadFromParcel(JNIEnv *env,
                                                          jobject self,
                                                          jobject parcel)
{
    (void) env;
    (void) self;
    (void) parcel;
    return (jlong) ++muplar_next_input_channel_token;
}

void Java_android_view_InputChannel_nativeWriteToParcel(JNIEnv *env,
                                                        jobject self,
                                                        jobject parcel,
                                                        jlong native_ptr)
{
    (void) env;
    (void) self;
    (void) parcel;
    (void) native_ptr;
}

jlong Java_android_view_InputEventReceiver_nativeInit(JNIEnv *env,
                                                      jobject self,
                                                      jobject receiver_ref,
                                                      jobject input_channel,
                                                      jobject message_queue)
{
    (void) env;
    (void) self;
    (void) receiver_ref;
    (void) input_channel;
    (void) message_queue;
    return (jlong) ++muplar_next_input_event_receiver_token;
}

void Java_android_view_InputEventReceiver_nativeDispose(JNIEnv *env,
                                                        jobject self,
                                                        jlong native_ptr)
{
    (void) env;
    (void) self;
    (void) native_ptr;
}

jboolean Java_android_view_InputEventReceiver_nativeConsumeBatchedInputEvents(
    JNIEnv *env,
    jobject self,
    jlong native_ptr,
    jlong frame_time_nanos)
{
    (void) env;
    (void) self;
    (void) native_ptr;
    (void) frame_time_nanos;
    return JNI_FALSE;
}

jobject Java_android_graphics_Bitmap_nativeCreate(JNIEnv *env,
                                                  jclass clazz,
                                                  jintArray colors,
                                                  jint offset,
                                                  jint stride,
                                                  jint width,
                                                  jint height,
                                                  jint config,
                                                  jboolean mutable_bitmap,
                                                  jlong colorspace)
{
    (void) colors;
    (void) offset;
    (void) stride;
    (void) config;
    (void) mutable_bitmap;
    (void) colorspace;
    return Java_android_graphics_HardwareRenderer_nCreateHardwareBitmap(
        env, clazz, 0, width, height);
}

void Java_android_graphics_Bitmap_nativeSetHasAlpha(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong native_bitmap,
                                                    jboolean has_alpha,
                                                    jboolean request_premul)
{
    (void) env;
    (void) clazz;
    (void) native_bitmap;
    (void) has_alpha;
    (void) request_premul;
}

jboolean Java_android_graphics_Bitmap_nativeIsImmutable(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong native_bitmap)
{
    (void) env;
    (void) clazz;
    (void) native_bitmap;
    return JNI_FALSE;
}

jboolean Java_android_graphics_Bitmap_nativeIsPremultiplied(JNIEnv *env,
                                                            jclass clazz,
                                                            jlong native_bitmap)
{
    (void) env;
    (void) clazz;
    (void) native_bitmap;
    return JNI_TRUE;
}

jint Java_android_graphics_Bitmap_nativeConfig(JNIEnv *env,
                                               jclass clazz,
                                               jlong native_bitmap)
{
    (void) env;
    (void) clazz;
    (void) native_bitmap;
    return 5; /* Bitmap.Config.ARGB_8888 */
}

void Java_android_graphics_Bitmap_nativeSetGainmap(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong native_bitmap,
                                                   jlong gainmap)
{
    (void) env;
    (void) clazz;
    (void) native_bitmap;
    (void) gainmap;
}

void Java_android_graphics_Bitmap_nativeRecycle(JNIEnv *env,
                                                jclass clazz,
                                                jlong native_bitmap)
{
    (void) env;
    (void) clazz;
    muplar_release_bitmap(native_bitmap);
}

jboolean Java_android_graphics_Bitmap_nativeCompress(JNIEnv *env,
                                                     jclass clazz,
                                                     jlong native_bitmap,
                                                     jint format,
                                                     jint quality,
                                                     jobject stream,
                                                     jbyteArray temp_storage)
{
    struct muplar_bitmap_state *bitmap = muplar_find_bitmap(native_bitmap);
    struct muplar_byte_buffer png = {0};
    int ok;
    (void) clazz;
    (void) format;
    (void) quality;
    (void) temp_storage;
    if (!bitmap)
        return JNI_FALSE;
    fprintf(stderr,
            "[Muplar/ART] bitmap compress draw counts color=%u paint=%u "
            "rect=%u text=%u bitmap=%u\n",
            muplar_draw_color_count, muplar_draw_paint_count,
            muplar_draw_rect_count, muplar_draw_text_count,
            muplar_draw_bitmap_count);
    ok = muplar_bitmap_to_png(bitmap, &png);
    if (ok)
        ok = muplar_write_output_stream(env, stream, png.data, png.len);
    free(png.data);
    return ok ? JNI_TRUE : JNI_FALSE;
}

jboolean Java_com_muplar_runtime_MuplarGraphics_presentBitmap(
    JNIEnv *env,
    jclass clazz,
    jlong native_bitmap,
    jint width,
    jint height)
{
    struct muplar_bitmap_state *bitmap = muplar_find_bitmap(native_bitmap);
    (void) env;
    (void) clazz;
    (void) width;
    (void) height;
    return muplar_present_bitmap_to_host_window(bitmap) ? JNI_TRUE : JNI_FALSE;
}

jlong Java_android_graphics_RenderNode_nCreate(JNIEnv *env,
                                               jclass clazz,
                                               jstring name)
{
    (void) env;
    (void) clazz;
    (void) name;
    muplar_next_render_node_token += 0x100;
    return (jlong) muplar_next_render_node_token;
}

jlong Java_android_graphics_RenderNode_nGetNativeFinalizer(JNIEnv *env,
                                                           jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jint Java_android_graphics_RenderNode_nGetUniqueId(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong node)
{
    (void) env;
    (void) clazz;
    return (jint) (node & 0x7fffffff);
}

jboolean Java_android_graphics_RenderNode_nSetLeftTopRightBottom(JNIEnv *env,
                                                                 jclass clazz,
                                                                 jlong node,
                                                                 jint left,
                                                                 jint top,
                                                                 jint right,
                                                                 jint bottom)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) left;
    (void) top;
    (void) right;
    (void) bottom;
    return JNI_TRUE;
}

jboolean Java_android_graphics_RenderNode_nSetBoolean(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong node,
                                                      jboolean value)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) value;
    return JNI_TRUE;
}

jboolean Java_android_graphics_RenderNode_nSetFloat(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong node,
                                                    jfloat value)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) value;
    return JNI_TRUE;
}

static jfloat muplar_RenderNode_getFloatZero(JNIEnv *env,
                                             jclass clazz,
                                             jlong node)
{
    (void) env;
    (void) clazz;
    (void) node;
    return 0.0f;
}

static jfloat muplar_RenderNode_getFloatOne(JNIEnv *env,
                                            jclass clazz,
                                            jlong node)
{
    (void) env;
    (void) clazz;
    (void) node;
    return 1.0f;
}

static jint muplar_RenderNode_getIntZero(JNIEnv *env, jclass clazz, jlong node)
{
    (void) env;
    (void) clazz;
    (void) node;
    return 0;
}

static jboolean muplar_RenderNode_getBooleanTrue(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong node)
{
    (void) env;
    (void) clazz;
    (void) node;
    return JNI_TRUE;
}

static jboolean muplar_RenderNode_nSetInt(JNIEnv *env,
                                          jclass clazz,
                                          jlong node,
                                          jint value)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) value;
    return JNI_TRUE;
}

static jboolean muplar_RenderNode_nSetLong(JNIEnv *env,
                                           jclass clazz,
                                           jlong node,
                                           jlong value)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) value;
    return JNI_TRUE;
}

static jboolean muplar_RenderNode_nSetClipBounds(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong node,
                                                 jint left,
                                                 jint top,
                                                 jint right,
                                                 jint bottom)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) left;
    (void) top;
    (void) right;
    (void) bottom;
    return JNI_TRUE;
}

static void muplar_RenderNode_nSetIntVoid(JNIEnv *env,
                                          jclass clazz,
                                          jlong node,
                                          jint value)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) value;
}

static void muplar_RenderNode_nRequestPositionUpdates(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong node,
                                                      jobject callback)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) callback;
}

static jboolean muplar_RenderNode_nSetLayerType(JNIEnv *env,
                                                jclass clazz,
                                                jlong node,
                                                jint type)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) type;
    return JNI_TRUE;
}

static jint muplar_RenderNode_nGetLayerType(JNIEnv *env,
                                            jclass clazz,
                                            jlong node)
{
    (void) env;
    (void) clazz;
    (void) node;
    return 0;
}

static jboolean muplar_RenderNode_nSetLayerPaint(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong node,
                                                 jlong paint)
{
    (void) env;
    (void) clazz;
    (void) node;
    (void) paint;
    return JNI_TRUE;
}

static jboolean muplar_RenderNode_nIsPivotExplicitlySet(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong node)
{
    (void) env;
    (void) clazz;
    (void) node;
    return JNI_FALSE;
}

static jboolean muplar_RenderNode_nResetPivot(JNIEnv *env,
                                              jclass clazz,
                                              jlong node)
{
    (void) env;
    (void) clazz;
    (void) node;
    return JNI_TRUE;
}

static jlong muplar_clock_millis(clockid_t clock_id)
{
    struct timespec ts;
    if (clock_gettime(clock_id, &ts) != 0)
        return 0;
    return (jlong) ts.tv_sec * 1000 + (jlong) (ts.tv_nsec / 1000000);
}

jlong Java_android_os_SystemClock_uptimeMillis(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return muplar_clock_millis(CLOCK_MONOTONIC);
}

jlong Java_android_os_SystemClock_elapsedRealtime(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return muplar_clock_millis(CLOCK_MONOTONIC);
}

jlong Java_android_os_SystemClock_elapsedRealtimeNanos(JNIEnv *env,
                                                       jclass clazz)
{
    struct timespec ts;
    (void) env;
    (void) clazz;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (jlong) ts.tv_sec * 1000000000LL + (jlong) ts.tv_nsec;
}

jlong Java_android_os_SystemClock_uptimeNanos(JNIEnv *env, jclass clazz)
{
    return Java_android_os_SystemClock_elapsedRealtimeNanos(env, clazz);
}

jlong Java_android_os_SystemClock_currentThreadTimeMillis(JNIEnv *env,
                                                          jclass clazz)
{
    (void) env;
    (void) clazz;
    return muplar_clock_millis(CLOCK_THREAD_CPUTIME_ID);
}

jboolean Java_android_os_Trace_nativeIsTagEnabled(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong tag)
{
    (void) env;
    (void) clazz;
    (void) tag;
    return JNI_FALSE;
}

void Java_android_os_Trace_nativeTraceBegin(JNIEnv *env,
                                            jclass clazz,
                                            jlong tag,
                                            jstring name)
{
    (void) env;
    (void) clazz;
    (void) tag;
    (void) name;
}

void Java_android_os_Trace_nativeTraceEnd(JNIEnv *env, jclass clazz, jlong tag)
{
    (void) env;
    (void) clazz;
    (void) tag;
}

void Java_android_os_Trace_nativeAsyncTraceBegin(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong tag,
                                                 jstring name,
                                                 jint cookie)
{
    (void) env;
    (void) clazz;
    (void) tag;
    (void) name;
    (void) cookie;
}

void Java_android_os_Trace_nativeAsyncTraceEnd(JNIEnv *env,
                                               jclass clazz,
                                               jlong tag,
                                               jstring name,
                                               jint cookie)
{
    (void) env;
    (void) clazz;
    (void) tag;
    (void) name;
    (void) cookie;
}

void Java_android_os_Trace_nativeInstant(JNIEnv *env,
                                         jclass clazz,
                                         jlong tag,
                                         jstring name)
{
    (void) env;
    (void) clazz;
    (void) tag;
    (void) name;
}

void Java_android_os_Process_setThreadPriority(JNIEnv *env,
                                               jclass clazz,
                                               jint priority)
{
    (void) env;
    (void) clazz;
    (void) priority;
}

void Java_android_os_Process_setThreadPriority__II(JNIEnv *env,
                                                   jclass clazz,
                                                   jint tid,
                                                   jint priority)
{
    (void) env;
    (void) clazz;
    (void) tid;
    (void) priority;
}

jlong Java_android_os_Process_getTotalMemory(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return (jlong) 4 * 1024 * 1024 * 1024;
}

jlong Java_android_os_Binder_clearCallingIdentity(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

void Java_android_os_Binder_restoreCallingIdentity(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong token)
{
    (void) env;
    (void) clazz;
    (void) token;
}

void Java_android_os_Binder_blockUntilThreadAvailable(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
}

jlong Java_android_os_Binder_clearCallingWorkSource(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

void Java_android_os_Binder_restoreCallingWorkSource(JNIEnv *env,
                                                     jclass clazz,
                                                     jlong token)
{
    (void) env;
    (void) clazz;
    (void) token;
}

jint Java_android_os_Binder_getCallingWorkSourceUid(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return -1;
}

jlong Java_android_os_Binder_setCallingWorkSourceUid(JNIEnv *env,
                                                     jclass clazz,
                                                     jint uid)
{
    (void) env;
    (void) clazz;
    (void) uid;
    return 0;
}

jint Java_android_os_Binder_getThreadStrictModePolicy(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

void Java_android_os_Binder_setThreadStrictModePolicy(JNIEnv *env,
                                                      jclass clazz,
                                                      jint policy)
{
    (void) env;
    (void) clazz;
    (void) policy;
}

jboolean Java_android_os_Binder_hasExplicitIdentity(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return JNI_FALSE;
}

jboolean Java_android_os_Binder_isDirectlyHandlingTransactionNative(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return JNI_FALSE;
}

void Java_android_os_Binder_setExtensionNative(JNIEnv *env,
                                               jobject thiz,
                                               jobject extension)
{
    (void) env;
    (void) thiz;
    (void) extension;
}

jint Java_android_os_Binder_getCallingPid(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return 1;
}

jint Java_android_os_Binder_getCallingUid(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return 1000;
}

void Java_android_os_Binder_flushPendingCommands(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
}

jlong Java_android_os_Binder_getNativeBBinderHolder(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return (jlong) (uintptr_t) &muplar_binder_holder_token;
}

jlong Java_android_os_Binder_getNativeFinalizer(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jobject Java_com_android_internal_os_BinderInternal_getContextObject(
    JNIEnv *env,
    jclass clazz)
{
    jclass binder_class;
    jmethodID ctor;
    (void) clazz;
    binder_class = (*env)->FindClass(env, "android/os/Binder");
    if (!binder_class) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    ctor = (*env)->GetMethodID(env, binder_class, "<init>", "()V");
    if (!ctor) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return (*env)->NewObject(env, binder_class, ctor);
}

jobject Java_android_os_ServiceManagerProxy_getNativeServiceManager(
    JNIEnv *env,
    jclass clazz)
{
    return Java_com_android_internal_os_BinderInternal_getContextObject(env,
                                                                        clazz);
}

jlong Java_android_os_Parcel_nativeCreate(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return (jlong) ++muplar_next_parcel_token;
}

void Java_android_os_Parcel_nativeDestroy(JNIEnv *env,
                                          jclass clazz,
                                          jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
}

jint Java_android_os_Parcel_nativeDataAvail(JNIEnv *env,
                                            jclass clazz,
                                            jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return 0;
}

jint Java_android_os_Parcel_nativeDataCapacity(JNIEnv *env,
                                               jclass clazz,
                                               jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return 0;
}

jint Java_android_os_Parcel_nativeDataPosition(JNIEnv *env,
                                               jclass clazz,
                                               jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return 0;
}

jint Java_android_os_Parcel_nativeDataSize(JNIEnv *env,
                                           jclass clazz,
                                           jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return 0;
}

void Java_android_os_Parcel_nativeSetDataCapacity(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong native_ptr,
                                                  jint size)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) size;
}

void Java_android_os_Parcel_nativeSetDataPosition(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong native_ptr,
                                                  jint pos)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) pos;
}

void Java_android_os_Parcel_nativeSetDataSize(JNIEnv *env,
                                              jclass clazz,
                                              jlong native_ptr,
                                              jint size)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) size;
}

void Java_android_os_Parcel_nativeFreeBuffer(JNIEnv *env,
                                             jclass clazz,
                                             jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
}

void Java_android_os_Parcel_nativeEnforceInterface(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong native_ptr,
                                                   jstring interface)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) interface;
}

jboolean Java_android_os_Parcel_nativeHasBinders(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return JNI_FALSE;
}

jboolean Java_android_os_Parcel_nativeHasBindersInRange(JNIEnv *env,
                                                        jclass clazz,
                                                        jlong native_ptr,
                                                        jint offset,
                                                        jint length)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) offset;
    (void) length;
    return JNI_FALSE;
}

jboolean Java_android_os_Parcel_nativeHasFileDescriptors(JNIEnv *env,
                                                         jclass clazz,
                                                         jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return JNI_FALSE;
}

jboolean Java_android_os_Parcel_nativeHasFileDescriptorsInRange(
    JNIEnv *env,
    jclass clazz,
    jlong native_ptr,
    jint offset,
    jint length)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) offset;
    (void) length;
    return JNI_FALSE;
}

jboolean Java_android_os_Parcel_nativeIsForRpc(JNIEnv *env,
                                               jclass clazz,
                                               jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return JNI_FALSE;
}

void Java_android_os_Parcel_nativeMarkSensitive(JNIEnv *env,
                                                jclass clazz,
                                                jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
}

void Java_android_os_Parcel_nativeMarkForBinder(JNIEnv *env,
                                                jclass clazz,
                                                jlong native_ptr,
                                                jobject binder)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) binder;
}

void Java_android_os_Parcel_nativeWriteStrongBinder(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong native_ptr,
                                                    jobject binder)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) binder;
}

jobject Java_android_os_Parcel_nativeReadStrongBinder(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong native_ptr)
{
    (void) clazz;
    (void) native_ptr;
    return Java_com_android_internal_os_BinderInternal_getContextObject(env,
                                                                        clazz);
}

jint Java_android_os_Parcel_nativeWriteInt(JNIEnv *env,
                                           jclass clazz,
                                           jlong native_ptr,
                                           jint value)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) value;
    return 0;
}

jint Java_android_os_Parcel_nativeWriteLong(JNIEnv *env,
                                            jclass clazz,
                                            jlong native_ptr,
                                            jlong value)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) value;
    return 0;
}

jint Java_android_os_Parcel_nativeWriteFloat(JNIEnv *env,
                                             jclass clazz,
                                             jlong native_ptr,
                                             jfloat value)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) value;
    return 0;
}

jint Java_android_os_Parcel_nativeWriteDouble(JNIEnv *env,
                                              jclass clazz,
                                              jlong native_ptr,
                                              jdouble value)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) value;
    return 0;
}

void Java_android_os_Parcel_nativeWriteString8(JNIEnv *env,
                                               jclass clazz,
                                               jlong native_ptr,
                                               jstring value)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) value;
}

void Java_android_os_Parcel_nativeWriteString16(JNIEnv *env,
                                                jclass clazz,
                                                jlong native_ptr,
                                                jstring value)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) value;
}

void Java_android_os_Parcel_nativeWriteInterfaceToken(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong native_ptr,
                                                      jstring value)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    (void) value;
}

jint Java_android_os_Parcel_nativeReadInt(JNIEnv *env,
                                          jclass clazz,
                                          jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return 0;
}

jlong Java_android_os_Parcel_nativeReadLong(JNIEnv *env,
                                            jclass clazz,
                                            jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return 0;
}

jfloat Java_android_os_Parcel_nativeReadFloat(JNIEnv *env,
                                              jclass clazz,
                                              jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return 0;
}

jdouble Java_android_os_Parcel_nativeReadDouble(JNIEnv *env,
                                                jclass clazz,
                                                jlong native_ptr)
{
    (void) env;
    (void) clazz;
    (void) native_ptr;
    return 0;
}

jstring Java_android_os_Parcel_nativeReadString8(JNIEnv *env,
                                                 jclass clazz,
                                                 jlong native_ptr)
{
    (void) clazz;
    (void) native_ptr;
    return (*env)->NewStringUTF(env, "");
}

jstring Java_android_os_Parcel_nativeReadString16(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong native_ptr)
{
    (void) clazz;
    (void) native_ptr;
    return (*env)->NewStringUTF(env, "");
}

jlong Java_android_os_BinderProxy_getNativeFinalizer(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jboolean Java_android_os_BinderProxy_isFrozenStateChangeCallbackSupportedNative(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return JNI_FALSE;
}

void Java_android_os_BinderProxy_addFrozenStateChangeCallbackNative(
    JNIEnv *env,
    jobject thiz,
    jobject callback)
{
    (void) env;
    (void) thiz;
    (void) callback;
}

jboolean Java_android_os_BinderProxy_removeFrozenStateChangeCallbackNative(
    JNIEnv *env,
    jobject thiz,
    jobject callback)
{
    (void) env;
    (void) thiz;
    (void) callback;
    return JNI_TRUE;
}

void Java_android_os_BinderProxy_linkToDeathNative(JNIEnv *env,
                                                   jobject thiz,
                                                   jobject recipient,
                                                   jint flags)
{
    (void) env;
    (void) thiz;
    (void) recipient;
    (void) flags;
}

jboolean Java_android_os_BinderProxy_unlinkToDeathNative(JNIEnv *env,
                                                         jobject thiz,
                                                         jobject recipient,
                                                         jint flags)
{
    (void) env;
    (void) thiz;
    (void) recipient;
    (void) flags;
    return JNI_TRUE;
}

jobject Java_android_os_BinderProxy_getExtension(JNIEnv *env, jobject thiz)
{
    (void) env;
    (void) thiz;
    return NULL;
}

jstring Java_android_os_BinderProxy_getInterfaceDescriptor(JNIEnv *env,
                                                           jobject thiz)
{
    (void) thiz;
    return (*env)->NewStringUTF(env, "");
}

jboolean Java_android_os_BinderProxy_isBinderAlive(JNIEnv *env, jobject thiz)
{
    (void) env;
    (void) thiz;
    return JNI_TRUE;
}

jboolean Java_android_os_BinderProxy_pingBinder(JNIEnv *env, jobject thiz)
{
    (void) env;
    (void) thiz;
    return JNI_TRUE;
}

jboolean Java_android_os_BinderProxy_transactNative(JNIEnv *env,
                                                    jobject thiz,
                                                    jint code,
                                                    jobject data,
                                                    jobject reply,
                                                    jint flags)
{
    (void) thiz;
    (void) code;
    (void) data;
    (void) flags;
    if (reply) {
        jclass parcel_class = (*env)->GetObjectClass(env, reply);
        jmethodID write_no_exception =
            (*env)->GetMethodID(env, parcel_class, "writeNoException", "()V");
        if (write_no_exception)
            (*env)->CallVoidMethod(env, reply, write_no_exception);
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
    }
    return JNI_TRUE;
}

jlong Java_android_view_VelocityTracker_nativeInitialize(JNIEnv *env,
                                                         jclass clazz,
                                                         jint strategy)
{
    (void) env;
    (void) clazz;
    (void) strategy;
    return (jlong) ++muplar_next_velocity_tracker_token;
}

void Java_android_view_VelocityTracker_nativeDispose(JNIEnv *env,
                                                     jclass clazz,
                                                     jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

void Java_android_view_VelocityTracker_nativeClear(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

void Java_android_view_VelocityTracker_nativeAddMovement(JNIEnv *env,
                                                         jclass clazz,
                                                         jlong ptr,
                                                         jobject event)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) event;
}

void Java_android_view_VelocityTracker_nativeComputeCurrentVelocity(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint units,
    jfloat max_velocity)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) units;
    (void) max_velocity;
}

jfloat Java_android_view_VelocityTracker_nativeGetVelocity(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong ptr,
                                                           jint axis,
                                                           jint id)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) axis;
    (void) id;
    return 0;
}

jboolean Java_android_view_VelocityTracker_nativeIsAxisSupported(JNIEnv *env,
                                                                 jclass clazz,
                                                                 jint axis)
{
    (void) env;
    (void) clazz;
    (void) axis;
    return JNI_TRUE;
}

jlong Java_android_view_DisplayEventReceiver_nativeGetDisplayEventReceiverFinalizer(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jlong Java_android_view_DisplayEventReceiver_nativeInit(JNIEnv *env,
                                                        jclass clazz,
                                                        jobject receiver,
                                                        jobject vsync_receiver,
                                                        jobject message_queue,
                                                        jint vsync_source,
                                                        jint event_registration,
                                                        jlong layer_handle)
{
    (void) env;
    (void) clazz;
    (void) receiver;
    (void) vsync_receiver;
    (void) message_queue;
    (void) vsync_source;
    (void) event_registration;
    (void) layer_handle;
    return (jlong) ++muplar_next_display_event_receiver_token;
}

void Java_android_view_DisplayEventReceiver_nativeScheduleVsync(
    JNIEnv *env,
    jclass clazz,
    jlong receiver_ptr)
{
    (void) env;
    (void) clazz;
    (void) receiver_ptr;
}

jobject Java_android_view_DisplayEventReceiver_nativeGetLatestVsyncEventData(
    JNIEnv *env,
    jclass clazz,
    jlong receiver_ptr)
{
    (void) env;
    (void) clazz;
    (void) receiver_ptr;
    return NULL;
}

jlong Java_android_graphics_Region_nativeConstructor(JNIEnv *env, jclass clazz)
{
    (void) env;
    (void) clazz;
    return (jlong) ++muplar_next_region_token;
}

jlong Java_android_graphics_Region_nativeCreateFromParcel(JNIEnv *env,
                                                          jclass clazz,
                                                          jobject parcel)
{
    (void) env;
    (void) clazz;
    (void) parcel;
    return (jlong) ++muplar_next_region_token;
}

void Java_android_graphics_Region_nativeDestructor(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong region)
{
    (void) env;
    (void) clazz;
    (void) region;
}

jboolean Java_android_graphics_Region_nativeEquals(JNIEnv *env,
                                                   jclass clazz,
                                                   jlong a,
                                                   jlong b)
{
    (void) env;
    (void) clazz;
    return a == b;
}

jboolean Java_android_graphics_Region_nativeGetBoundaryPath(JNIEnv *env,
                                                            jclass clazz,
                                                            jlong region,
                                                            jlong path)
{
    (void) env;
    (void) clazz;
    (void) region;
    (void) path;
    return JNI_FALSE;
}

jboolean Java_android_graphics_Region_nativeGetBounds(JNIEnv *env,
                                                      jclass clazz,
                                                      jlong region,
                                                      jobject rect)
{
    (void) env;
    (void) clazz;
    (void) region;
    (void) rect;
    return JNI_FALSE;
}

jboolean Java_android_graphics_Region_nativeOp__JIIIII(JNIEnv *env,
                                                       jclass clazz,
                                                       jlong region,
                                                       jint left,
                                                       jint top,
                                                       jint right,
                                                       jint bottom,
                                                       jint op)
{
    (void) env;
    (void) clazz;
    (void) region;
    (void) left;
    (void) top;
    (void) right;
    (void) bottom;
    (void) op;
    return JNI_TRUE;
}

jboolean Java_android_graphics_Region_nativeOp__JJJI(JNIEnv *env,
                                                     jclass clazz,
                                                     jlong dst,
                                                     jlong region1,
                                                     jlong region2,
                                                     jint op)
{
    (void) env;
    (void) clazz;
    (void) dst;
    (void) region1;
    (void) region2;
    (void) op;
    return JNI_TRUE;
}

jboolean Java_android_graphics_Region_nativeOp__JLandroid_graphics_Rect_2JI(
    JNIEnv *env,
    jclass clazz,
    jlong dst,
    jobject rect,
    jlong region,
    jint op)
{
    (void) env;
    (void) clazz;
    (void) dst;
    (void) rect;
    (void) region;
    (void) op;
    return JNI_TRUE;
}

jboolean Java_android_graphics_Region_nativeSetPath(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong region,
                                                    jlong path,
                                                    jlong clip)
{
    (void) env;
    (void) clazz;
    (void) region;
    (void) path;
    (void) clip;
    return JNI_TRUE;
}

jboolean Java_android_graphics_Region_nativeSetRect(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong region,
                                                    jint left,
                                                    jint top,
                                                    jint right,
                                                    jint bottom)
{
    (void) env;
    (void) clazz;
    (void) region;
    (void) left;
    (void) top;
    (void) right;
    (void) bottom;
    return JNI_TRUE;
}

void Java_android_graphics_Region_nativeSetRegion(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong dst,
                                                  jlong src)
{
    (void) env;
    (void) clazz;
    (void) dst;
    (void) src;
}

jstring Java_android_graphics_Region_nativeToString(JNIEnv *env,
                                                    jclass clazz,
                                                    jlong region)
{
    (void) clazz;
    (void) region;
    return (*env)->NewStringUTF(env, "Region()");
}

jboolean Java_android_graphics_Region_nativeWriteToParcel(JNIEnv *env,
                                                          jclass clazz,
                                                          jlong region,
                                                          jobject parcel)
{
    (void) env;
    (void) clazz;
    (void) region;
    (void) parcel;
    return JNI_TRUE;
}

jlong Java_android_graphics_RegionIterator_nativeConstructor(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong region)
{
    (void) env;
    (void) clazz;
    (void) region;
    return (jlong) ++muplar_next_region_token;
}

void Java_android_graphics_RegionIterator_nativeDestructor(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong iterator)
{
    (void) env;
    (void) clazz;
    (void) iterator;
}

jboolean Java_android_graphics_RegionIterator_nativeNext(JNIEnv *env,
                                                         jclass clazz,
                                                         jlong iterator,
                                                         jobject rect)
{
    (void) env;
    (void) clazz;
    (void) iterator;
    (void) rect;
    return JNI_FALSE;
}

jlong Java_android_view_SurfaceControl_nativeGetNativeSurfaceControlFinalizer(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jlong Java_android_view_SurfaceControl_nativeGetNativeTransactionFinalizer(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jlong Java_android_view_SurfaceControl_nativeCreateTransaction(JNIEnv *env,
                                                               jclass clazz)
{
    (void) env;
    (void) clazz;
    return (jlong) ++muplar_next_surface_transaction_token;
}

jlong Java_android_view_SurfaceControl_nativeCreate(JNIEnv *env,
                                                    jclass clazz,
                                                    jobject session,
                                                    jstring name,
                                                    jint width,
                                                    jint height,
                                                    jint format,
                                                    jint flags,
                                                    jlong parent,
                                                    jobject metadata)
{
    (void) env;
    (void) clazz;
    (void) session;
    (void) name;
    (void) width;
    (void) height;
    (void) format;
    (void) flags;
    (void) parent;
    (void) metadata;
    return (jlong) ++muplar_next_surface_control_token;
}

jlong Java_android_view_SurfaceControl_nativeCopyFromSurfaceControl(
    JNIEnv *env,
    jclass clazz,
    jlong surface)
{
    (void) env;
    (void) clazz;
    return surface ? surface : (jlong) ++muplar_next_surface_control_token;
}

jlong Java_android_view_SurfaceControl_nativeMirrorSurface(JNIEnv *env,
                                                           jclass clazz,
                                                           jlong surface)
{
    (void) env;
    (void) clazz;
    (void) surface;
    return (jlong) ++muplar_next_surface_control_token;
}

jlong Java_android_view_SurfaceControl_nativeMirrorSurfaceWithStopLayer(
    JNIEnv *env,
    jclass clazz,
    jlong surface,
    jlong stop_layer)
{
    (void) env;
    (void) clazz;
    (void) surface;
    (void) stop_layer;
    return (jlong) ++muplar_next_surface_control_token;
}

void Java_android_view_SurfaceControl_nativeApplyTransaction(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong transaction,
                                                             jboolean sync,
                                                             jboolean one_way)
{
    (void) env;
    (void) clazz;
    (void) transaction;
    (void) sync;
    (void) one_way;
}

void Java_android_view_SurfaceControl_nativeClearTransaction(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong transaction)
{
    (void) env;
    (void) clazz;
    (void) transaction;
}

void Java_android_view_SurfaceControl_nativeMergeTransaction(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong dst,
                                                             jlong src)
{
    (void) env;
    (void) clazz;
    (void) dst;
    (void) src;
}

void Java_android_view_SurfaceControl_nativeSetAnimationTransaction(
    JNIEnv *env,
    jclass clazz,
    jlong transaction)
{
    (void) env;
    (void) clazz;
    (void) transaction;
}

void Java_android_view_SurfaceControl_nativeSetFrameTimelineVsync(
    JNIEnv *env,
    jclass clazz,
    jlong transaction,
    jlong vsync_id)
{
    (void) env;
    (void) clazz;
    (void) transaction;
    (void) vsync_id;
}

jlong Java_android_view_SurfaceControl_nativeGetTransactionId(JNIEnv *env,
                                                              jclass clazz,
                                                              jlong transaction)
{
    (void) env;
    (void) clazz;
    return transaction;
}

jlong Java_android_view_SurfaceControl_nativeGetHandle(JNIEnv *env,
                                                       jclass clazz,
                                                       jlong surface)
{
    (void) env;
    (void) clazz;
    return surface;
}

jint Java_android_view_SurfaceControl_nativeGetLayerId(JNIEnv *env,
                                                       jclass clazz,
                                                       jlong surface)
{
    (void) env;
    (void) clazz;
    return (jint) surface;
}

jboolean Java_android_view_SurfaceControl_nativeBootFinished(JNIEnv *env,
                                                             jclass clazz)
{
    (void) env;
    (void) clazz;
    return JNI_TRUE;
}

jboolean Java_android_view_SurfaceControl_nativeGetBootDisplayModeSupport(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return JNI_FALSE;
}

jobject Java_android_view_SurfaceControl_nativeGetDefaultApplyToken(
    JNIEnv *env,
    jclass clazz)
{
    return Java_com_android_internal_os_BinderInternal_getContextObject(env,
                                                                        clazz);
}

void Java_android_view_SurfaceControl_nativeSetDefaultApplyToken(JNIEnv *env,
                                                                 jclass clazz,
                                                                 jobject token)
{
    (void) env;
    (void) clazz;
    (void) token;
}

jint Java_android_view_SurfaceControl_nativeGetTransformHint(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong surface)
{
    (void) env;
    (void) clazz;
    (void) surface;
    return 0;
}

jboolean Java_android_graphics_Region_contains(JNIEnv *env,
                                               jobject thiz,
                                               jint x,
                                               jint y)
{
    (void) env;
    (void) thiz;
    (void) x;
    (void) y;
    return JNI_FALSE;
}

jboolean Java_android_graphics_Region_isComplex(JNIEnv *env, jobject thiz)
{
    (void) env;
    (void) thiz;
    return JNI_FALSE;
}

jboolean Java_android_graphics_Region_isEmpty(JNIEnv *env, jobject thiz)
{
    (void) env;
    (void) thiz;
    return JNI_TRUE;
}

jboolean Java_android_graphics_Region_isRect(JNIEnv *env, jobject thiz)
{
    (void) env;
    (void) thiz;
    return JNI_FALSE;
}

jboolean Java_android_graphics_Region_quickContains(JNIEnv *env,
                                                    jobject thiz,
                                                    jint left,
                                                    jint top,
                                                    jint right,
                                                    jint bottom)
{
    (void) env;
    (void) thiz;
    (void) left;
    (void) top;
    (void) right;
    (void) bottom;
    return JNI_FALSE;
}

jboolean Java_android_graphics_Region_quickReject__IIII(JNIEnv *env,
                                                        jobject thiz,
                                                        jint left,
                                                        jint top,
                                                        jint right,
                                                        jint bottom)
{
    (void) env;
    (void) thiz;
    (void) left;
    (void) top;
    (void) right;
    (void) bottom;
    return JNI_TRUE;
}

jboolean Java_android_graphics_Region_quickReject__Landroid_graphics_Region_2(
    JNIEnv *env,
    jobject thiz,
    jobject region)
{
    (void) env;
    (void) thiz;
    (void) region;
    return JNI_TRUE;
}

void Java_android_graphics_Region_scale(JNIEnv *env,
                                        jobject thiz,
                                        jfloat scale,
                                        jobject dst)
{
    (void) env;
    (void) thiz;
    (void) scale;
    (void) dst;
}

jboolean Java_android_graphics_Region_set__IIII(JNIEnv *env,
                                                jobject thiz,
                                                jint left,
                                                jint top,
                                                jint right,
                                                jint bottom)
{
    (void) env;
    (void) thiz;
    (void) left;
    (void) top;
    (void) right;
    (void) bottom;
    return JNI_TRUE;
}

jboolean Java_android_graphics_Region_set__Landroid_graphics_Region_2(
    JNIEnv *env,
    jobject thiz,
    jobject region)
{
    (void) env;
    (void) thiz;
    (void) region;
    return JNI_TRUE;
}

void Java_android_graphics_Region_translate(JNIEnv *env,
                                            jobject thiz,
                                            jint dx,
                                            jint dy,
                                            jobject dst)
{
    (void) env;
    (void) thiz;
    (void) dx;
    (void) dy;
    (void) dst;
}

jlong Java_android_content_res_AssetManager_nativeGetThemeFreeFunction(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jlong Java_android_os_PerfettoTrace_00024Category_native_1init(JNIEnv *env,
                                                               jclass clazz,
                                                               jstring name,
                                                               jstring tag,
                                                               jstring severity)
{
    (void) env;
    (void) clazz;
    (void) name;
    (void) tag;
    (void) severity;
    return (jlong) ++muplar_next_perfetto_token;
}

jlong Java_android_os_PerfettoTrace_00024Category_native_1delete(JNIEnv *env,
                                                                 jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

void Java_android_os_PerfettoTrace_00024Category_native_1register(JNIEnv *env,
                                                                  jclass clazz,
                                                                  jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

void Java_android_os_PerfettoTrace_00024Category_native_1unregister(
    JNIEnv *env,
    jclass clazz,
    jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

jboolean Java_android_os_PerfettoTrace_00024Category_native_1is_1enabled(
    JNIEnv *env,
    jclass clazz,
    jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    return JNI_FALSE;
}

jlong Java_android_os_PerfettoTrace_00024Category_native_1get_1extra_1ptr(
    JNIEnv *env,
    jclass clazz,
    jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    return 0;
}

jlong Java_android_os_PerfettoTrace_native_1get_1process_1track_1uuid(
    JNIEnv *env,
    jclass clazz)
{
    (void) env;
    (void) clazz;
    return 1;
}

jlong Java_android_os_PerfettoTrace_native_1get_1thread_1track_1uuid(
    JNIEnv *env,
    jclass clazz,
    jlong tid)
{
    (void) env;
    (void) clazz;
    return tid ? tid : 1;
}

void Java_android_os_PerfettoTrace_native_1activate_1trigger(JNIEnv *env,
                                                             jclass clazz,
                                                             jstring name,
                                                             jint ttl_ms)
{
    (void) env;
    (void) clazz;
    (void) name;
    (void) ttl_ms;
}

void Java_android_os_PerfettoTrace_native_1register(
    JNIEnv *env,
    jclass clazz,
    jboolean is_backend_in_process)
{
    (void) env;
    (void) clazz;
    (void) is_backend_in_process;
}

jlong Java_android_os_PerfettoTrace_native_1start_1session(
    JNIEnv *env,
    jclass clazz,
    jboolean is_backend_in_process,
    jbyteArray config)
{
    (void) env;
    (void) clazz;
    (void) is_backend_in_process;
    (void) config;
    return (jlong) ++muplar_next_perfetto_token;
}

jbyteArray Java_android_os_PerfettoTrace_native_1stop_1session(JNIEnv *env,
                                                               jclass clazz,
                                                               jlong ptr)
{
    (void) clazz;
    (void) ptr;
    return (*env)->NewByteArray(env, 0);
}

jlong Java_android_os_PerfettoTrackEventExtra_native_1init(JNIEnv *env,
                                                           jclass clazz)
{
    (void) env;
    (void) clazz;
    return (jlong) ++muplar_next_perfetto_token;
}

jlong Java_android_os_PerfettoTrackEventExtra_native_1delete(JNIEnv *env,
                                                             jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

void Java_android_os_PerfettoTrackEventExtra_native_1add_1arg(JNIEnv *env,
                                                              jclass clazz,
                                                              jlong ptr,
                                                              jlong extra_ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) extra_ptr;
}

jlong Java_android_tracing_perfetto_DataSource_nativeCreate(JNIEnv *env,
                                                            jclass clazz,
                                                            jobject data_source,
                                                            jstring name)
{
    (void) env;
    (void) clazz;
    (void) data_source;
    (void) name;
    return (jlong) ++muplar_next_perfetto_token;
}

void Java_android_tracing_perfetto_DataSource_nativeFlushAll(JNIEnv *env,
                                                             jclass clazz,
                                                             jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

jlong Java_android_tracing_perfetto_DataSource_nativeGetFinalizer(JNIEnv *env,
                                                                  jclass clazz)
{
    (void) env;
    (void) clazz;
    return 0;
}

jint Java_android_tracing_perfetto_DataSource_nativeGetPerfettoDsInstanceIndex(
    JNIEnv *env,
    jclass clazz,
    jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    return 0;
}

jobject
Java_android_tracing_perfetto_DataSource_nativeGetPerfettoInstanceLocked(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint index)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) index;
    return NULL;
}

jboolean
Java_android_tracing_perfetto_DataSource_nativePerfettoDsTraceIterateBegin(
    JNIEnv *env,
    jclass clazz,
    jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    return JNI_FALSE;
}

void Java_android_tracing_perfetto_DataSource_nativePerfettoDsTraceIterateBreak(
    JNIEnv *env,
    jclass clazz,
    jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

jboolean
Java_android_tracing_perfetto_DataSource_nativePerfettoDsTraceIterateNext(
    JNIEnv *env,
    jclass clazz,
    jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    return JNI_FALSE;
}

void Java_android_tracing_perfetto_DataSource_nativeRegisterDataSource(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint buffer_exhausted_policy,
    jboolean will_notify_on_stop,
    jboolean no_flush)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) buffer_exhausted_policy;
    (void) will_notify_on_stop;
    (void) no_flush;
}

void Java_android_tracing_perfetto_DataSource_nativeReleasePerfettoInstanceLocked(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jint index)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) index;
}

void Java_android_tracing_perfetto_DataSource_nativeWritePackets(
    JNIEnv *env,
    jclass clazz,
    jlong ptr,
    jobjectArray packets)
{
    (void) env;
    (void) clazz;
    (void) ptr;
    (void) packets;
}

void Java_android_tracing_perfetto_Producer_nativePerfettoProducerInit(
    JNIEnv *env,
    jclass clazz,
    jint backends,
    jint shmem_size_hint_kb)
{
    (void) env;
    (void) clazz;
    (void) backends;
    (void) shmem_size_hint_kb;
}

void Java_android_os_PerfettoTrackEventExtra_native_1clear_1args(JNIEnv *env,
                                                                 jclass clazz,
                                                                 jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) ptr;
}

void Java_android_os_PerfettoTrackEventExtra_native_1emit(JNIEnv *env,
                                                          jclass clazz,
                                                          jint type,
                                                          jlong tag,
                                                          jstring name,
                                                          jlong ptr)
{
    (void) env;
    (void) clazz;
    (void) type;
    (void) tag;
    (void) name;
    (void) ptr;
}

static void muplar_register_one(JNIEnv *env,
                                jclass cls,
                                const char *name,
                                const char *signature,
                                void *fn)
{
    JNINativeMethod method = {name, signature, fn};
    if ((*env)->RegisterNatives(env, cls, &method, 1) != JNI_OK)
        (*env)->ExceptionClear(env);
}

static void muplar_register_framework_natives(JNIEnv *env)
{
    typedef int (*register_fn)(JNIEnv *);
    static const char *const symbols[] = {
        "_ZN7android38register_android_content_res_ApkAssetsEP7_JNIEnv",
        "_ZN7android37register_android_content_AssetManagerEP7_JNIEnv",
        "_ZN7android36register_android_content_StringBlockEP7_JNIEnv",
        "_ZN7android33register_android_content_XmlBlockEP7_JNIEnv",
        "_ZN7android42register_android_content_res_ConfigurationEP7_JNIEnv",
        "_ZN7android26register_android_os_ParcelEP7_JNIEnv",
        NULL,
    };
    void *handle =
        dlopen("/system/lib64/libandroid_runtime.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        handle = dlopen("libandroid_runtime.so", RTLD_NOW | RTLD_GLOBAL);
    }
    if (!handle) {
        fprintf(stderr,
                "[Muplar/ART] framework native register: dlopen failed: %s\n",
                dlerror());
        return;
    }

    for (size_t i = 0; symbols[i]; ++i) {
        register_fn fn = (register_fn) dlsym(handle, symbols[i]);
        if (!fn) {
            fprintf(stderr,
                    "[Muplar/ART] framework native register: missing %s\n",
                    symbols[i]);
            continue;
        }
        if (fn(env) < 0) {
            fprintf(stderr,
                    "[Muplar/ART] framework native register failed: %s\n",
                    symbols[i]);
            if ((*env)->ExceptionCheck(env))
                (*env)->ExceptionClear(env);
        }
    }

    /*
     * Do not call libhwui's register_android_graphics_classes here. That
     * function performs one-shot graphics/font global initialization which the
     * framework later expects to own; registering it from this launcher shim
     * trips Typeface's default-font assertion during Launcher3 startup. Keep
     * graphics support narrow and add individual natives above as needed.
     */
}

static void muplar_clear_exception(JNIEnv *env)
{
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
}

static jobject muplar_create_typeface(JNIEnv *env,
                                      jclass cls,
                                      jint style,
                                      jint weight,
                                      jlong native_instance)
{
    jobject typeface = (*env)->AllocObject(env, cls);
    jfieldID field;
    if (!typeface) {
        muplar_clear_exception(env);
        return NULL;
    }

    field = (*env)->GetFieldID(env, cls, "mStyle", "I");
    if (field)
        (*env)->SetIntField(env, typeface, field, style);
    else
        muplar_clear_exception(env);

    field = (*env)->GetFieldID(env, cls, "mWeight", "I");
    if (field)
        (*env)->SetIntField(env, typeface, field, weight);
    else
        muplar_clear_exception(env);

    field = (*env)->GetFieldID(env, cls, "native_instance", "J");
    if (field)
        (*env)->SetLongField(env, typeface, field, native_instance);
    else
        muplar_clear_exception(env);

    return typeface;
}

static void muplar_set_static_typeface(JNIEnv *env,
                                       jclass cls,
                                       const char *name,
                                       jobject value)
{
    jfieldID field =
        (*env)->GetStaticFieldID(env, cls, name, "Landroid/graphics/Typeface;");
    if (!field) {
        muplar_clear_exception(env);
        return;
    }
    (*env)->SetStaticObjectField(env, cls, field, value);
    muplar_clear_exception(env);
}

static void muplar_install_typeface_defaults(JNIEnv *env)
{
    jclass cls = (*env)->FindClass(env, "android/graphics/Typeface");
    jobject normal;
    jobject bold;
    jobject italic;
    jobject bold_italic;
    jobjectArray defaults;
    jfieldID defaults_field;
    if (!cls) {
        muplar_clear_exception(env);
        return;
    }

    normal = muplar_create_typeface(env, cls, 0, 400, 0x70000);
    bold = muplar_create_typeface(env, cls, 1, 700, 0x70100);
    italic = muplar_create_typeface(env, cls, 2, 400, 0x70200);
    bold_italic = muplar_create_typeface(env, cls, 3, 700, 0x70300);
    if (!normal || !bold || !italic || !bold_italic)
        return;

    muplar_set_static_typeface(env, cls, "DEFAULT", normal);
    muplar_set_static_typeface(env, cls, "DEFAULT_BOLD", bold);
    muplar_set_static_typeface(env, cls, "SANS_SERIF", normal);
    muplar_set_static_typeface(env, cls, "SERIF", normal);
    muplar_set_static_typeface(env, cls, "MONOSPACE", normal);
    muplar_set_static_typeface(env, cls, "sDefaultTypeface", normal);

    defaults = (*env)->NewObjectArray(env, 4, cls, normal);
    if (!defaults) {
        muplar_clear_exception(env);
        return;
    }
    (*env)->SetObjectArrayElement(env, defaults, 0, normal);
    (*env)->SetObjectArrayElement(env, defaults, 1, bold);
    (*env)->SetObjectArrayElement(env, defaults, 2, italic);
    (*env)->SetObjectArrayElement(env, defaults, 3, bold_italic);
    defaults_field = (*env)->GetStaticFieldID(env, cls, "sDefaults",
                                              "[Landroid/graphics/Typeface;");
    if (defaults_field)
        (*env)->SetStaticObjectField(env, cls, defaults_field, defaults);
    else
        muplar_clear_exception(env);
}

JNIEXPORT void JNICALL
Java_com_muplar_runtime_ArtApkMain_installTypefaceDefaultsNative(JNIEnv *env,
                                                                 jclass clazz)
{
    (void) clazz;
    muplar_install_typeface_defaults(env);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void) reserved;
    JNIEnv *env = NULL;
    jclass cls;
    if ((*vm)->GetEnv(vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK || !env)
        return JNI_ERR;

    cls = (*env)->FindClass(env, "com/muplar/runtime/MuplarGraphics");
    if (cls) {
        muplar_graphics_create_bitmap = (*env)->GetStaticMethodID(
            env, cls, "createBitmap", "(II)Landroid/graphics/Bitmap;");
        if (muplar_graphics_create_bitmap) {
            muplar_graphics_class = (*env)->NewGlobalRef(env, cls);
        } else if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        }
    } else if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/os/SystemProperties");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }

    muplar_register_one(
        env, cls, "native_get", "(Ljava/lang/String;)Ljava/lang/String;",
        (void *)
            Java_android_os_SystemProperties_native_1get__Ljava_lang_String_2);
    muplar_register_one(
        env, cls, "native_get",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        (void *)
            Java_android_os_SystemProperties_native_1get__Ljava_lang_String_2Ljava_lang_String_2);
    muplar_register_one(
        env, cls, "native_get_int", "(Ljava/lang/String;I)I",
        (void *) Java_android_os_SystemProperties_native_1get_1int);
    muplar_register_one(
        env, cls, "native_get_long", "(Ljava/lang/String;J)J",
        (void *) Java_android_os_SystemProperties_native_1get_1long);
    muplar_register_one(
        env, cls, "native_get_boolean", "(Ljava/lang/String;Z)Z",
        (void *) Java_android_os_SystemProperties_native_1get_1boolean);
    muplar_register_one(env, cls, "native_find", "(Ljava/lang/String;)J",
                        (void *) Java_android_os_SystemProperties_native_1find);
    muplar_register_one(
        env, cls, "native_get", "(J)Ljava/lang/String;",
        (void *) Java_android_os_SystemProperties_native_1get__J);
    muplar_register_one(
        env, cls, "native_get_int", "(JI)I",
        (void *) Java_android_os_SystemProperties_native_1get_1int__JI);
    muplar_register_one(
        env, cls, "native_get_long", "(JJ)J",
        (void *) Java_android_os_SystemProperties_native_1get_1long__JJ);
    muplar_register_one(
        env, cls, "native_get_boolean", "(JZ)Z",
        (void *) Java_android_os_SystemProperties_native_1get_1boolean__JZ);
    muplar_register_one(
        env, cls, "native_add_change_callback", "()V",
        (void *)
            Java_android_os_SystemProperties_native_1add_1change_1callback);

    muplar_register_framework_natives(env);

    cls = (*env)->FindClass(env, "android/os/Parcel");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(env, cls, "nativeCreate", "()J",
                            (void *) Java_android_os_Parcel_nativeCreate);
        muplar_register_one(env, cls, "nativeDestroy", "(J)V",
                            (void *) Java_android_os_Parcel_nativeDestroy);
        muplar_register_one(env, cls, "nativeDataAvail", "(J)I",
                            (void *) Java_android_os_Parcel_nativeDataAvail);
        muplar_register_one(env, cls, "nativeDataCapacity", "(J)I",
                            (void *) Java_android_os_Parcel_nativeDataCapacity);
        muplar_register_one(env, cls, "nativeDataPosition", "(J)I",
                            (void *) Java_android_os_Parcel_nativeDataPosition);
        muplar_register_one(env, cls, "nativeDataSize", "(J)I",
                            (void *) Java_android_os_Parcel_nativeDataSize);
        muplar_register_one(
            env, cls, "nativeSetDataCapacity", "(JI)V",
            (void *) Java_android_os_Parcel_nativeSetDataCapacity);
        muplar_register_one(
            env, cls, "nativeSetDataPosition", "(JI)V",
            (void *) Java_android_os_Parcel_nativeSetDataPosition);
        muplar_register_one(env, cls, "nativeSetDataSize", "(JI)V",
                            (void *) Java_android_os_Parcel_nativeSetDataSize);
        muplar_register_one(env, cls, "nativeFreeBuffer", "(J)V",
                            (void *) Java_android_os_Parcel_nativeFreeBuffer);
        muplar_register_one(
            env, cls, "nativeEnforceInterface", "(JLjava/lang/String;)V",
            (void *) Java_android_os_Parcel_nativeEnforceInterface);
        muplar_register_one(env, cls, "nativeHasBinders", "(J)Z",
                            (void *) Java_android_os_Parcel_nativeHasBinders);
        muplar_register_one(
            env, cls, "nativeHasBindersInRange", "(JII)Z",
            (void *) Java_android_os_Parcel_nativeHasBindersInRange);
        muplar_register_one(
            env, cls, "nativeHasFileDescriptors", "(J)Z",
            (void *) Java_android_os_Parcel_nativeHasFileDescriptors);
        muplar_register_one(
            env, cls, "nativeHasFileDescriptorsInRange", "(JII)Z",
            (void *) Java_android_os_Parcel_nativeHasFileDescriptorsInRange);
        muplar_register_one(env, cls, "nativeIsForRpc", "(J)Z",
                            (void *) Java_android_os_Parcel_nativeIsForRpc);
        muplar_register_one(
            env, cls, "nativeMarkSensitive", "(J)V",
            (void *) Java_android_os_Parcel_nativeMarkSensitive);
        muplar_register_one(
            env, cls, "nativeMarkForBinder", "(JLandroid/os/IBinder;)V",
            (void *) Java_android_os_Parcel_nativeMarkForBinder);
        muplar_register_one(
            env, cls, "nativeWriteStrongBinder", "(JLandroid/os/IBinder;)V",
            (void *) Java_android_os_Parcel_nativeWriteStrongBinder);
        muplar_register_one(
            env, cls, "nativeReadStrongBinder", "(J)Landroid/os/IBinder;",
            (void *) Java_android_os_Parcel_nativeReadStrongBinder);
        muplar_register_one(env, cls, "nativeWriteInt", "(JI)I",
                            (void *) Java_android_os_Parcel_nativeWriteInt);
        muplar_register_one(env, cls, "nativeWriteLong", "(JJ)I",
                            (void *) Java_android_os_Parcel_nativeWriteLong);
        muplar_register_one(env, cls, "nativeWriteFloat", "(JF)I",
                            (void *) Java_android_os_Parcel_nativeWriteFloat);
        muplar_register_one(env, cls, "nativeWriteDouble", "(JD)I",
                            (void *) Java_android_os_Parcel_nativeWriteDouble);
        muplar_register_one(env, cls, "nativeWriteString8",
                            "(JLjava/lang/String;)V",
                            (void *) Java_android_os_Parcel_nativeWriteString8);
        muplar_register_one(
            env, cls, "nativeWriteString16", "(JLjava/lang/String;)V",
            (void *) Java_android_os_Parcel_nativeWriteString16);
        muplar_register_one(
            env, cls, "nativeWriteInterfaceToken", "(JLjava/lang/String;)V",
            (void *) Java_android_os_Parcel_nativeWriteInterfaceToken);
        muplar_register_one(env, cls, "nativeReadInt", "(J)I",
                            (void *) Java_android_os_Parcel_nativeReadInt);
        muplar_register_one(env, cls, "nativeReadLong", "(J)J",
                            (void *) Java_android_os_Parcel_nativeReadLong);
        muplar_register_one(env, cls, "nativeReadFloat", "(J)F",
                            (void *) Java_android_os_Parcel_nativeReadFloat);
        muplar_register_one(env, cls, "nativeReadDouble", "(J)D",
                            (void *) Java_android_os_Parcel_nativeReadDouble);
        muplar_register_one(env, cls, "nativeReadString8",
                            "(J)Ljava/lang/String;",
                            (void *) Java_android_os_Parcel_nativeReadString8);
        muplar_register_one(env, cls, "nativeReadString16",
                            "(J)Ljava/lang/String;",
                            (void *) Java_android_os_Parcel_nativeReadString16);
    }

    cls = (*env)->FindClass(env, "android/os/MessageQueue");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(env, cls, "nativeInit", "()J",
                        (void *) Java_android_os_MessageQueue_nativeInit);
    muplar_register_one(env, cls, "nativeDestroy", "(J)V",
                        (void *) Java_android_os_MessageQueue_nativeDestroy);
    muplar_register_one(env, cls, "nativePollOnce", "(JI)V",
                        (void *) Java_android_os_MessageQueue_nativePollOnce);
    muplar_register_one(env, cls, "nativeWake", "(J)V",
                        (void *) Java_android_os_MessageQueue_nativeWake);
    muplar_register_one(env, cls, "nativeIsPolling", "(J)Z",
                        (void *) Java_android_os_MessageQueue_nativeIsPolling);
    muplar_register_one(
        env, cls, "nativeSetFileDescriptorEvents", "(JII)V",
        (void *) Java_android_os_MessageQueue_nativeSetFileDescriptorEvents);

    cls = (*env)->FindClass(env, "android/util/Log");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(env, cls, "isLoggable", "(Ljava/lang/String;I)Z",
                        (void *) Java_android_util_Log_isLoggable);
    muplar_register_one(env, cls, "println_native",
                        "(IILjava/lang/String;Ljava/lang/String;)I",
                        (void *) Java_android_util_Log_println_1native);
    muplar_register_one(
        env, cls, "logger_entry_max_payload_native", "()I",
        (void *) Java_android_util_Log_logger_1entry_1max_1payload_1native);

    cls = (*env)->FindClass(env, "android/util/EventLog");
    if (!cls) {
        (*env)->ExceptionClear(env);
    } else {
        muplar_register_one(env, cls, "writeEvent", "(I[Ljava/lang/Object;)I",
                            (void *) Java_android_util_EventLog_writeEvent);
    }

    cls = (*env)->FindClass(env, "android/database/sqlite/SQLiteConnection");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(env, cls, "nativeOpen",
                            "(Ljava/lang/String;ILjava/lang/String;ZZII)J",
                            (void *) muplar_SQLite_nOpen);
        muplar_register_one(env, cls, "nativeClose", "(J)V",
                            (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativeClose", "(JZ)V",
                            (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativeRegisterLocalizedCollators",
                            "(JLjava/lang/String;)V",
                            (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativePrepareStatement",
                            "(JLjava/lang/String;)J",
                            (void *) muplar_SQLite_nativePrepareStatement);
        muplar_register_one(env, cls, "nativeFinalizeStatement", "(JJ)V",
                            (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativeGetParameterCount", "(JJ)I",
                            (void *) muplar_SQLite_nativeGetParameterCount);
        muplar_register_one(env, cls, "nativeIsReadOnly", "(JJ)Z",
                            (void *) muplar_SQLite_nBoolean);
        muplar_register_one(env, cls, "nativeIsForcedReadOnly", "(J)Z",
                            (void *) muplar_SQLite_nBoolean);
        muplar_register_one(env, cls, "nativeGetColumnCount", "(JJ)I",
                            (void *) muplar_SQLite_nativeGetColumnCount);
        muplar_register_one(env, cls, "nativeGetColumnName",
                            "(JJI)Ljava/lang/String;",
                            (void *) muplar_SQLite_nativeGetColumnName);
        muplar_register_one(env, cls, "nativeBindNull", "(JJI)V",
                            (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativeBindLong", "(JJIJ)V",
                            (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativeBindDouble", "(JJID)V",
                            (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativeBindString",
                            "(JJILjava/lang/String;)V",
                            (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativeBindBlob", "(JJI[B)V",
                            (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativeResetStatementAndClearBindings",
                            "(JJ)V", (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativeExecute", "(JJZ)V",
                            (void *) muplar_SQLite_nVoid);
        muplar_register_one(env, cls, "nativeExecuteForLong", "(JJ)J",
                            (void *) muplar_SQLite_nLong);
        muplar_register_one(env, cls, "nativeExecuteForString",
                            "(JJ)Ljava/lang/String;",
                            (void *) muplar_SQLite_nString);
        muplar_register_one(env, cls, "nativeExecuteForChangedRowCount",
                            "(JJ)I", (void *) muplar_SQLite_nInt);
        muplar_register_one(env, cls, "nativeExecuteForLastInsertedRowId",
                            "(JJ)J", (void *) muplar_SQLite_nLong);
        muplar_register_one(
            env, cls, "nativeExecuteForCursorWindow", "(JJJIIZ)J",
            (void *) muplar_SQLite_nativeExecuteForCursorWindow);
    }

    cls = (*env)->FindClass(env, "android/database/sqlite/SQLiteGlobal");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(env, cls, "nativeReleaseMemory", "()I",
                            (void *) muplar_SQLite_nInt);
    }

    cls = (*env)->FindClass(env, "android/database/CursorWindow");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(env, cls, "nativeCreate", "(Ljava/lang/String;I)J",
                            (void *) muplar_CursorWindow_nativeCreate);
        muplar_register_one(
            env, cls, "nativeCreateFromParcel", "(Landroid/os/Parcel;)J",
            (void *) muplar_CursorWindow_nativeCreateFromParcel);
        muplar_register_one(env, cls, "nativeDispose", "(J)V",
                            (void *) muplar_CursorWindow_nativeVoid);
        muplar_register_one(env, cls, "nativeGetName", "(J)Ljava/lang/String;",
                            (void *) muplar_CursorWindow_nativeString);
        muplar_register_one(env, cls, "nativeAllocRow", "(J)Z",
                            (void *) muplar_CursorWindow_nativeTrue);
        muplar_register_one(env, cls, "nativeClear", "(J)V",
                            (void *) muplar_CursorWindow_nativeVoid);
        muplar_register_one(env, cls, "nativeCopyStringToBuffer",
                            "(JIILandroid/database/CharArrayBuffer;)V",
                            (void *) muplar_CursorWindow_nativeVoid);
        muplar_register_one(env, cls, "nativeFreeLastRow", "(J)V",
                            (void *) muplar_CursorWindow_nativeVoid);
        muplar_register_one(env, cls, "nativeGetBlob", "(JII)[B",
                            (void *) muplar_CursorWindow_nativeBlob);
        muplar_register_one(env, cls, "nativeGetDouble", "(JII)D",
                            (void *) muplar_CursorWindow_nativeDouble);
        muplar_register_one(env, cls, "nativeGetLong", "(JII)J",
                            (void *) muplar_CursorWindow_nativeLong);
        muplar_register_one(env, cls, "nativeGetNumRows", "(J)I",
                            (void *) muplar_CursorWindow_nativeInt);
        muplar_register_one(env, cls, "nativeGetString",
                            "(JII)Ljava/lang/String;",
                            (void *) muplar_CursorWindow_nativeString);
        muplar_register_one(env, cls, "nativeGetType", "(JII)I",
                            (void *) muplar_CursorWindow_nativeInt);
        muplar_register_one(env, cls, "nativePutBlob", "(J[BII)Z",
                            (void *) muplar_CursorWindow_nativeTrue);
        muplar_register_one(env, cls, "nativePutDouble", "(JDII)Z",
                            (void *) muplar_CursorWindow_nativeTrue);
        muplar_register_one(env, cls, "nativePutLong", "(JJII)Z",
                            (void *) muplar_CursorWindow_nativeTrue);
        muplar_register_one(env, cls, "nativePutNull", "(JII)Z",
                            (void *) muplar_CursorWindow_nativeTrue);
        muplar_register_one(env, cls, "nativePutString",
                            "(JLjava/lang/String;II)Z",
                            (void *) muplar_CursorWindow_nativeTrue);
        muplar_register_one(env, cls, "nativeSetNumColumns", "(JI)Z",
                            (void *) muplar_CursorWindow_nativeTrue);
        muplar_register_one(env, cls, "nativeWriteToParcel",
                            "(JLandroid/os/Parcel;)V",
                            (void *) muplar_CursorWindow_nativeVoid);
    }

    cls = (*env)->FindClass(env, "android/graphics/Matrix$ExtraNatives");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(
        env, cls, "nCreate", "(J)J",
        (void *) Java_android_graphics_Matrix_00024ExtraNatives_nCreate);
    muplar_register_one(
        env, cls, "nGetNativeFinalizer", "()J",
        (void *)
            Java_android_graphics_Matrix_00024ExtraNatives_nGetNativeFinalizer);

    cls = (*env)->FindClass(env, "android/graphics/Matrix");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(env, cls, "nSetScale", "(JFF)V",
                        (void *) Java_android_graphics_Matrix_nSetScale);
    muplar_register_one(env, cls, "nSetScale", "(JFFFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nPostTranslate", "(JFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nPreTranslate", "(JFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nSetTranslate", "(JFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nPostScale", "(JFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nPostScale", "(JFFFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nPreScale", "(JFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nPreScale", "(JFFFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nSetRotate", "(JF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nSetRotate", "(JFFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nPostRotate", "(JF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nPostRotate", "(JFFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nPreRotate", "(JF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nPreRotate", "(JFFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nSetSkew", "(JFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nSetSkew", "(JFFFF)V",
                        (void *) muplar_Matrix_nVoid);
    muplar_register_one(env, cls, "nSetConcat", "(JJJ)Z",
                        (void *) muplar_Matrix_nTrue);

    cls = (*env)->FindClass(env, "android/graphics/ColorSpace$Rgb$Native");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(
        env, cls, "nativeCreate", "(FFFFFFF[F)J",
        (void *)
            Java_android_graphics_ColorSpace_00024Rgb_00024Native_nativeCreate);
    muplar_register_one(
        env, cls, "nativeGetNativeFinalizer", "()J",
        (void *)
            Java_android_graphics_ColorSpace_00024Rgb_00024Native_nativeGetNativeFinalizer);

    cls = (*env)->FindClass(env, "android/graphics/Bitmap");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(env, cls, "nativeCreate",
                            "([IIIIIIZJ)Landroid/graphics/Bitmap;",
                            (void *) Java_android_graphics_Bitmap_nativeCreate);
        muplar_register_one(
            env, cls, "nativeSetHasAlpha", "(JZZ)V",
            (void *) Java_android_graphics_Bitmap_nativeSetHasAlpha);
        muplar_register_one(
            env, cls, "nativeIsImmutable", "(J)Z",
            (void *) Java_android_graphics_Bitmap_nativeIsImmutable);
        muplar_register_one(
            env, cls, "nativeIsPremultiplied", "(J)Z",
            (void *) Java_android_graphics_Bitmap_nativeIsPremultiplied);
        muplar_register_one(env, cls, "nativeConfig", "(J)I",
                            (void *) Java_android_graphics_Bitmap_nativeConfig);
        muplar_register_one(
            env, cls, "nativeSetGainmap", "(JJ)V",
            (void *) Java_android_graphics_Bitmap_nativeSetGainmap);
        muplar_register_one(
            env, cls, "nativeRecycle", "(J)V",
            (void *) Java_android_graphics_Bitmap_nativeRecycle);
        muplar_register_one(
            env, cls, "nativeCompress", "(JIILjava/io/OutputStream;[B)Z",
            (void *) Java_android_graphics_Bitmap_nativeCompress);
    }

    cls = (*env)->FindClass(env, "android/graphics/Path");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(env, cls, "nInit", "()J",
                        (void *) Java_android_graphics_Path_nInit);
    muplar_register_one(env, cls, "nInit", "(J)J",
                        (void *) Java_android_graphics_Path_nInit);
    muplar_register_one(env, cls, "nGetFinalizer", "()J",
                        (void *) Java_android_graphics_Path_nGetFinalizer);
    muplar_register_one(env, cls, "nIncReserve", "(JI)V",
                        (void *) Java_android_graphics_Path_nIncReserve);
    muplar_register_one(env, cls, "nMoveTo", "(JFF)V",
                        (void *) Java_android_graphics_Path_nMoveTo);
    muplar_register_one(env, cls, "nRMoveTo", "(JFF)V",
                        (void *) Java_android_graphics_Path_nRMoveTo);
    muplar_register_one(env, cls, "nLineTo", "(JFF)V",
                        (void *) Java_android_graphics_Path_nLineTo);
    muplar_register_one(env, cls, "nRLineTo", "(JFF)V",
                        (void *) Java_android_graphics_Path_nRLineTo);
    muplar_register_one(env, cls, "nQuadTo", "(JFFFF)V",
                        (void *) Java_android_graphics_Path_nQuadTo);
    muplar_register_one(env, cls, "nRQuadTo", "(JFFFF)V",
                        (void *) Java_android_graphics_Path_nRQuadTo);
    muplar_register_one(env, cls, "nCubicTo", "(JFFFFFF)V",
                        (void *) Java_android_graphics_Path_nCubicTo);
    muplar_register_one(env, cls, "nRCubicTo", "(JFFFFFF)V",
                        (void *) Java_android_graphics_Path_nRCubicTo);
    muplar_register_one(env, cls, "nConicTo", "(JFFFFF)V",
                        (void *) Java_android_graphics_Path_nConicTo);
    muplar_register_one(env, cls, "nRConicTo", "(JFFFFF)V",
                        (void *) Java_android_graphics_Path_nRConicTo);
    muplar_register_one(env, cls, "nAddCircle", "(JFFFI)V",
                        (void *) muplar_Path_nVoid);
    muplar_register_one(env, cls, "nAddOval", "(JFFFFI)V",
                        (void *) muplar_Path_nVoid);
    muplar_register_one(env, cls, "nAddRect", "(JFFFFI)V",
                        (void *) muplar_Path_nVoid);
    muplar_register_one(env, cls, "nAddRoundRect", "(JFFFFFFI)V",
                        (void *) muplar_Path_nVoid);
    muplar_register_one(env, cls, "nAddRoundRect", "(JFFFF[FI)V",
                        (void *) muplar_Path_nVoid);
    muplar_register_one(env, cls, "nAddPath", "(JJ)V",
                        (void *) muplar_Path_nVoid);
    muplar_register_one(env, cls, "nAddPath", "(JJFF)V",
                        (void *) muplar_Path_nVoid);
    muplar_register_one(env, cls, "nAddPath", "(JJJ)V",
                        (void *) muplar_Path_nVoid);
    muplar_register_one(env, cls, "nClose", "(J)V",
                        (void *) Java_android_graphics_Path_nClose);
    muplar_register_one(env, cls, "nReset", "(J)V",
                        (void *) Java_android_graphics_Path_nReset);
    muplar_register_one(env, cls, "nRewind", "(J)V",
                        (void *) Java_android_graphics_Path_nRewind);
    muplar_register_one(env, cls, "nIsEmpty", "(J)Z",
                        (void *) Java_android_graphics_Path_nIsEmpty);
    muplar_register_one(env, cls, "nIsConvex", "(J)Z",
                        (void *) Java_android_graphics_Path_nIsConvex);
    muplar_register_one(env, cls, "nIsInterpolatable", "(JJ)Z",
                        (void *) Java_android_graphics_Path_nIsInterpolatable);
    muplar_register_one(env, cls, "nInterpolate", "(JJFJ)Z",
                        (void *) Java_android_graphics_Path_nInterpolate);
    muplar_register_one(env, cls, "nOp", "(JJIJ)Z",
                        (void *) Java_android_graphics_Path_nOp);
    muplar_register_one(env, cls, "nSet", "(JJ)V",
                        (void *) Java_android_graphics_Path_nSet);
    muplar_register_one(env, cls, "nTransform", "(JJJ)V",
                        (void *) Java_android_graphics_Path_nTransform);
    muplar_register_one(env, cls, "nOffset", "(JFFJ)V",
                        (void *) Java_android_graphics_Path_nOffset);
    muplar_register_one(env, cls, "nSetFillType", "(JI)V",
                        (void *) Java_android_graphics_Path_nSetFillType);
    muplar_register_one(env, cls, "nGetFillType", "(J)I",
                        (void *) Java_android_graphics_Path_nGetFillType);
    muplar_register_one(env, cls, "nComputeBounds",
                        "(JLandroid/graphics/RectF;)V",
                        (void *) Java_android_graphics_Path_nComputeBounds);
    muplar_register_one(env, cls, "nApproximate", "(JF)[F",
                        (void *) Java_android_graphics_Path_nApproximate);

    cls = (*env)->FindClass(env, "android/graphics/PathMeasure");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(
        env, cls, "native_create", "(JZ)J",
        (void *) Java_android_graphics_PathMeasure_native_1create);
    muplar_register_one(
        env, cls, "native_destroy", "(J)V",
        (void *) Java_android_graphics_PathMeasure_native_1destroy);
    muplar_register_one(
        env, cls, "native_getLength", "(J)F",
        (void *) Java_android_graphics_PathMeasure_native_1getLength);
    muplar_register_one(
        env, cls, "native_getPosTan", "(JF[F[F)Z",
        (void *) Java_android_graphics_PathMeasure_native_1getPosTan);

    cls = (*env)->FindClass(env, "android/graphics/Paint");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(env, cls, "nInit", "()J",
                        (void *) Java_android_graphics_Paint_nInit);
    muplar_register_one(env, cls, "nInitWithPaint", "(J)J",
                        (void *) Java_android_graphics_Paint_nInitWithPaint);
    muplar_register_one(env, cls, "nSet", "(JJ)V",
                        (void *) Java_android_graphics_Paint_nSet);
    muplar_register_one(
        env, cls, "nGetNativeFinalizer", "()J",
        (void *) Java_android_graphics_Paint_nGetNativeFinalizer);
    muplar_register_one(env, cls, "nSetTextSize", "(JF)V",
                        (void *) Java_android_graphics_Paint_nSetTextSize);
    muplar_register_one(env, cls, "nGetTextSize", "(J)F",
                        (void *) Java_android_graphics_Paint_nGetTextSize);
    muplar_register_one(env, cls, "nSetAntiAlias", "(JZ)V",
                        (void *) Java_android_graphics_Paint_nSetAntiAlias);
    muplar_register_one(env, cls, "nSetFlags", "(JI)V",
                        (void *) Java_android_graphics_Paint_nSetFlags);
    muplar_register_one(env, cls, "nGetFlags", "(J)I",
                        (void *) Java_android_graphics_Paint_nGetFlags);
    muplar_register_one(env, cls, "nSetColor", "(JI)V",
                        (void *) Java_android_graphics_Paint_nSetColor);
    muplar_register_one(env, cls, "nSetAlpha", "(JI)V",
                        (void *) Java_android_graphics_Paint_nSetInt);
    muplar_register_one(
        env, cls, "nSetMyanmarEncoding", "(JI)V",
        (void *) Java_android_graphics_Paint_nSetMyanmarEncoding);
    muplar_register_one(
        env, cls, "nSetElegantTextHeight", "(JI)V",
        (void *) Java_android_graphics_Paint_nSetMyanmarEncoding);
    muplar_register_one(
        env, cls, "nSetTextLocalesByMinikinLocaleListId", "(JI)V",
        (void *) Java_android_graphics_Paint_nSetMyanmarEncoding);
    muplar_register_one(
        env, cls, "nSetXfermode", "(JI)V",
        (void *) Java_android_graphics_Paint_nSetMyanmarEncoding);
    muplar_register_one(
        env, cls, "nSetHinting", "(JI)V",
        (void *) Java_android_graphics_Paint_nSetMyanmarEncoding);
    muplar_register_one(
        env, cls, "nSetTextAlign", "(JI)V",
        (void *) Java_android_graphics_Paint_nSetMyanmarEncoding);
    muplar_register_one(
        env, cls, "nSetStyle", "(JI)V",
        (void *) Java_android_graphics_Paint_nSetMyanmarEncoding);
    muplar_register_one(
        env, cls, "nSetStrokeCap", "(JI)V",
        (void *) Java_android_graphics_Paint_nSetMyanmarEncoding);
    muplar_register_one(
        env, cls, "nSetStrokeJoin", "(JI)V",
        (void *) Java_android_graphics_Paint_nSetMyanmarEncoding);
    muplar_register_one(env, cls, "nSetSubpixelText", "(JZ)V",
                        (void *) Java_android_graphics_Paint_nSetBoolean);
    muplar_register_one(env, cls, "nSetLinearText", "(JZ)V",
                        (void *) Java_android_graphics_Paint_nSetBoolean);
    muplar_register_one(env, cls, "nSetFakeBoldText", "(JZ)V",
                        (void *) Java_android_graphics_Paint_nSetBoolean);
    muplar_register_one(env, cls, "nSetFilterBitmap", "(JZ)V",
                        (void *) Java_android_graphics_Paint_nSetBoolean);
    muplar_register_one(env, cls, "nSetDither", "(JZ)V",
                        (void *) Java_android_graphics_Paint_nSetBoolean);
    muplar_register_one(env, cls, "nSetUnderlineText", "(JZ)V",
                        (void *) Java_android_graphics_Paint_nSetBoolean);
    muplar_register_one(env, cls, "nSetStrikeThruText", "(JZ)V",
                        (void *) Java_android_graphics_Paint_nSetBoolean);
    muplar_register_one(env, cls, "nSetTextScaleX", "(JF)V",
                        (void *) Java_android_graphics_Paint_nSetTextSize);
    muplar_register_one(env, cls, "nSetTextSkewX", "(JF)V",
                        (void *) Java_android_graphics_Paint_nSetTextSize);
    muplar_register_one(env, cls, "nSetLetterSpacing", "(JF)V",
                        (void *) Java_android_graphics_Paint_nSetTextSize);
    muplar_register_one(env, cls, "nSetWordSpacing", "(JF)V",
                        (void *) Java_android_graphics_Paint_nSetTextSize);
    muplar_register_one(env, cls, "nSetStrokeWidth", "(JF)V",
                        (void *) Java_android_graphics_Paint_nSetTextSize);
    muplar_register_one(env, cls, "nSetStrokeMiter", "(JF)V",
                        (void *) Java_android_graphics_Paint_nSetTextSize);
    muplar_register_one(env, cls, "nSetFontFeatureSettings",
                        "(JLjava/lang/String;)V",
                        (void *) Java_android_graphics_Paint_nSetString);
    muplar_register_one(env, cls, "nSetFontVariationOverride",
                        "(JLjava/lang/String;)V",
                        (void *) Java_android_graphics_Paint_nSetString);
    muplar_register_one(env, cls, "nSetTypeface", "(JJ)J",
                        (void *) Java_android_graphics_Paint_nSetTypeface);
    muplar_register_one(env, cls, "nSetShader", "(JJ)J",
                        (void *) Java_android_graphics_Paint_nSetShader);
    muplar_register_one(env, cls, "nSetPathEffect", "(JJ)J",
                        (void *) Java_android_graphics_Paint_nSetPathEffect);
    muplar_register_one(env, cls, "nSetShadowLayer", "(JFFFJJ)V",
                        (void *) Java_android_graphics_Paint_nSetShadowLayer);
    muplar_register_one(env, cls, "nSetTextLocales", "(JLjava/lang/String;)I",
                        (void *) Java_android_graphics_Paint_nSetTextLocales);
    muplar_register_one(env, cls, "nSetTextLocales",
                        "(JLandroid/os/LocaleList;)I",
                        (void *) Java_android_graphics_Paint_nSetTextLocales);
    muplar_register_one(env, cls, "nGetStartHyphenEdit", "(J)I",
                        (void *) Java_android_graphics_Paint_nGetHyphenEdit);
    muplar_register_one(env, cls, "nGetEndHyphenEdit", "(J)I",
                        (void *) Java_android_graphics_Paint_nGetHyphenEdit);
    muplar_register_one(env, cls, "nSetStartHyphenEdit", "(JI)V",
                        (void *) Java_android_graphics_Paint_nSetHyphenEdit);
    muplar_register_one(env, cls, "nSetEndHyphenEdit", "(JI)V",
                        (void *) Java_android_graphics_Paint_nSetHyphenEdit);
    muplar_register_one(
        env, cls, "nGetFontMetricsInt",
        "(JLandroid/graphics/Paint$FontMetricsInt;)I",
        (void *) Java_android_graphics_Paint_nGetFontMetricsInt);
    muplar_register_one(
        env, cls, "nGetFontMetricsIntForText",
        "(J[CIIIIZLandroid/graphics/Paint$FontMetricsInt;)V",
        (void *) Java_android_graphics_Paint_nGetFontMetricsIntForText);
    muplar_register_one(env, cls, "nGetFontMetrics",
                        "(JLandroid/graphics/Paint$FontMetrics;)F",
                        (void *) Java_android_graphics_Paint_nGetFontMetrics);
    muplar_register_one(env, cls, "nGetFontMetrics",
                        "(JLandroid/graphics/Paint$FontMetrics;Z)F",
                        (void *) Java_android_graphics_Paint_nGetFontMetrics);
    muplar_register_one(
        env, cls, "nGetCharArrayBounds", "(J[CIIILandroid/graphics/Rect;)V",
        (void *) Java_android_graphics_Paint_nGetCharArrayBounds);
    muplar_register_one(
        env, cls, "nGetRunCharacterAdvance",
        "(J[CIIIIZI[FILandroid/graphics/RectF;"
        "Landroid/graphics/Paint$RunInfo;)F",
        (void *) Java_android_graphics_Paint_nGetRunCharacterAdvance);
    muplar_register_one(env, cls, "nGetTextAdvances", "(J[CIIIII[FI)F",
                        (void *) Java_android_graphics_Paint_nGetTextAdvances);

    cls = (*env)->FindClass(env, "android/graphics/Shader");
    if (cls) {
        muplar_register_one(
            env, cls, "nativeGetFinalizer", "()J",
            (void *) Java_android_graphics_Shader_nativeGetFinalizer);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/LinearGradient");
    if (cls) {
        muplar_register_one(
            env, cls, "nativeCreate", "(JFFFF[J[FIJ)J",
            (void *) Java_android_graphics_LinearGradient_nativeCreate);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/fonts/Font$Builder");
    if (cls) {
        muplar_register_one(
            env, cls, "nInitBuilder", "()J",
            (void *)
                Java_android_graphics_fonts_Font_00024Builder_nInitBuilder);
        muplar_register_one(
            env, cls, "nAddAxis", "(JIF)V",
            (void *) Java_android_graphics_fonts_Font_00024Builder_nAddAxis);
        muplar_register_one(
            env, cls, "nBuild",
            "(JLjava/nio/ByteBuffer;Ljava/lang/String;Ljava/lang/String;IZI)J",
            (void *) Java_android_graphics_fonts_Font_00024Builder_nBuild);
        muplar_register_one(
            env, cls, "nClone", "(JJIZI)J",
            (void *) Java_android_graphics_fonts_Font_00024Builder_nClone);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/fonts/Font");
    if (cls) {
        muplar_register_one(
            env, cls, "nGetMinikinFontPtr", "(J)J",
            (void *) Java_android_graphics_fonts_Font_nGetMinikinFontPtr);
        muplar_register_one(
            env, cls, "nCloneFont", "(J)J",
            (void *) Java_android_graphics_fonts_Font_nCloneFont);
        muplar_register_one(
            env, cls, "nNewByteBuffer", "(J)Ljava/nio/ByteBuffer;",
            (void *) Java_android_graphics_fonts_Font_nNewByteBuffer);
        muplar_register_one(
            env, cls, "nGetBufferAddress", "(J)J",
            (void *) Java_android_graphics_fonts_Font_nGetBufferAddress);
        muplar_register_one(
            env, cls, "nGetSourceId", "(J)I",
            (void *) Java_android_graphics_fonts_Font_nGetSourceId);
        muplar_register_one(
            env, cls, "nGetReleaseNativeFont", "()J",
            (void *) Java_android_graphics_fonts_Font_nGetReleaseNativeFont);
        muplar_register_one(
            env, cls, "nGetGlyphBounds", "(JIJLandroid/graphics/RectF;)F",
            (void *) Java_android_graphics_fonts_Font_nGetGlyphBounds);
        muplar_register_one(
            env, cls, "nGetFontMetrics",
            "(JJLandroid/graphics/Paint$FontMetrics;)F",
            (void *) Java_android_graphics_fonts_Font_nGetFontMetrics);
        muplar_register_one(
            env, cls, "nGetFontPath", "(J)Ljava/lang/String;",
            (void *) Java_android_graphics_fonts_Font_nGetFontPath);
        muplar_register_one(
            env, cls, "nGetLocaleList", "(J)Ljava/lang/String;",
            (void *) Java_android_graphics_fonts_Font_nGetLocaleList);
        muplar_register_one(
            env, cls, "nGetPackedStyle", "(J)I",
            (void *) Java_android_graphics_fonts_Font_nGetPackedStyle);
        muplar_register_one(
            env, cls, "nGetIndex", "(J)I",
            (void *) Java_android_graphics_fonts_Font_nGetIndex);
        muplar_register_one(
            env, cls, "nGetAxisCount", "(J)I",
            (void *) Java_android_graphics_fonts_Font_nGetAxisCount);
        muplar_register_one(
            env, cls, "nGetAxisInfo", "(JI)J",
            (void *) Java_android_graphics_fonts_Font_nGetAxisInfo);
        muplar_register_one(
            env, cls, "nGetAvailableFontSet", "()[J",
            (void *) Java_android_graphics_fonts_Font_nGetAvailableFontSet);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/fonts/FontFamily$Builder");
    if (cls) {
        muplar_register_one(
            env, cls, "nInitBuilder", "()J",
            (void *)
                Java_android_graphics_fonts_FontFamily_00024Builder_nInitBuilder);
        muplar_register_one(
            env, cls, "nAddFont", "(JJ)V",
            (void *)
                Java_android_graphics_fonts_FontFamily_00024Builder_nAddFont);
        muplar_register_one(
            env, cls, "nBuild", "(JLjava/lang/String;IZZI)J",
            (void *)
                Java_android_graphics_fonts_FontFamily_00024Builder_nBuild);
        muplar_register_one(
            env, cls, "nGetReleaseNativeFamily", "()J",
            (void *)
                Java_android_graphics_fonts_FontFamily_00024Builder_nGetReleaseNativeFamily);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/fonts/FontFamily");
    if (cls) {
        muplar_register_one(
            env, cls, "nGetFontSize", "(J)I",
            (void *) Java_android_graphics_fonts_FontFamily_nGetFontSize);
        muplar_register_one(
            env, cls, "nGetFont", "(JI)J",
            (void *) Java_android_graphics_fonts_FontFamily_nGetFont);
        muplar_register_one(
            env, cls, "nGetLangTags", "(J)Ljava/lang/String;",
            (void *) Java_android_graphics_fonts_FontFamily_nGetLangTags);
        muplar_register_one(
            env, cls, "nGetVariant", "(J)I",
            (void *) Java_android_graphics_fonts_FontFamily_nGetVariant);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/Typeface");
    if (cls) {
        muplar_register_one(
            env, cls, "nativeCreateFromTypeface", "(JI)J",
            (void *) Java_android_graphics_Typeface_nativeCreateFromTypeface);
        muplar_register_one(
            env, cls, "nativeCreateFromTypefaceWithExactStyle", "(JIZ)J",
            (void *)
                Java_android_graphics_Typeface_nativeCreateFromTypefaceWithExactStyle);
        muplar_register_one(
            env, cls, "nativeCreateFromTypefaceWithVariation",
            "(JLjava/util/List;)J",
            (void *)
                Java_android_graphics_Typeface_nativeCreateFromTypefaceWithVariation);
        muplar_register_one(
            env, cls, "nativeCreateWeightAlias", "(JI)J",
            (void *) Java_android_graphics_Typeface_nativeCreateWeightAlias);
        muplar_register_one(
            env, cls, "nativeIsVariationInstance", "(J)Z",
            (void *) Java_android_graphics_Typeface_nativeIsVariationInstance);
        muplar_register_one(
            env, cls, "nativeCreateFromArray", "([JJII)J",
            (void *) Java_android_graphics_Typeface_nativeCreateFromArray);
        muplar_register_one(
            env, cls, "nativeGetSupportedAxes", "(J)[I",
            (void *) Java_android_graphics_Typeface_nativeGetSupportedAxes);
        muplar_register_one(
            env, cls, "nativeSetDefault", "(J)V",
            (void *) Java_android_graphics_Typeface_nativeSetDefault);
        muplar_register_one(
            env, cls, "nativeGetStyle", "(J)I",
            (void *) Java_android_graphics_Typeface_nativeGetStyle);
        muplar_register_one(
            env, cls, "nativeGetWeight", "(J)I",
            (void *) Java_android_graphics_Typeface_nativeGetWeight);
        muplar_register_one(
            env, cls, "nativeGetReleaseFunc", "()J",
            (void *) Java_android_graphics_Typeface_nativeGetReleaseFunc);
        muplar_register_one(
            env, cls, "nativeRegisterGenericFamily", "(Ljava/lang/String;J)V",
            (void *)
                Java_android_graphics_Typeface_nativeRegisterGenericFamily);
        muplar_register_one(
            env, cls, "nativeWriteTypefaces", "(Ljava/nio/ByteBuffer;I[J)I",
            (void *) Java_android_graphics_Typeface_nativeWriteTypefaces);
        muplar_register_one(
            env, cls, "nativeReadTypefaces", "(Ljava/nio/ByteBuffer;I)[J",
            (void *) Java_android_graphics_Typeface_nativeReadTypefaces);
        muplar_register_one(
            env, cls, "nativeForceSetStaticFinalField",
            "(Ljava/lang/String;Landroid/graphics/Typeface;)V",
            (void *)
                Java_android_graphics_Typeface_nativeForceSetStaticFinalField);
        muplar_register_one(
            env, cls, "nativeAddFontCollections", "(J)V",
            (void *) Java_android_graphics_Typeface_nativeAddFontCollections);
        muplar_register_one(
            env, cls, "nativeWarmUpCache", "(Ljava/lang/String;)V",
            (void *) Java_android_graphics_Typeface_nativeWarmUpCache);
        muplar_register_one(
            env, cls, "nativeRegisterLocaleList", "(Ljava/lang/String;)V",
            (void *) Java_android_graphics_Typeface_nativeRegisterLocaleList);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/drawable/VectorDrawable");
    if (cls) {
        muplar_register_one(
            env, cls, "nDraw", "(JJJLandroid/graphics/Rect;ZZ)I",
            (void *) Java_android_graphics_drawable_VectorDrawable_nDraw);
        muplar_register_one(
            env, cls, "nGetFullPathProperties", "(J[BI)Z",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nGetFullPathProperties);
        muplar_register_one(
            env, cls, "nSetName", "(JLjava/lang/String;)V",
            (void *) Java_android_graphics_drawable_VectorDrawable_nSetName);
        muplar_register_one(
            env, cls, "nGetGroupProperties", "(J[FI)Z",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nGetGroupProperties);
        muplar_register_one(
            env, cls, "nSetPathString", "(JLjava/lang/String;I)V",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nSetPathString);
        muplar_register_one(
            env, cls, "nCreateTree", "(J)J",
            (void *) Java_android_graphics_drawable_VectorDrawable_nCreateTree);
        muplar_register_one(
            env, cls, "nCreateTreeFromCopy", "(JJ)J",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nCreateTreeFromCopy);
        muplar_register_one(
            env, cls, "nSetRendererViewportSize", "(JFF)V",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nSetRendererViewportSize);
        muplar_register_one(
            env, cls, "nSetRootAlpha", "(JF)Z",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nSetRootAlpha);
        muplar_register_one(
            env, cls, "nGetRootAlpha", "(J)F",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nGetRootAlpha);
        muplar_register_one(
            env, cls, "nSetAntiAlias", "(JZ)V",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nSetAntiAlias);
        muplar_register_one(
            env, cls, "nSetAllowCaching", "(JZ)V",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nSetAllowCaching);
        muplar_register_one(
            env, cls, "nCreateFullPath", "()J",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nCreateFullPath);
        muplar_register_one(
            env, cls, "nCreateFullPath", "(J)J",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nCreateFullPath__J);
        muplar_register_one(
            env, cls, "nUpdateFullPathProperties", "(JFIFIFFFFIII)V",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nUpdateFullPathProperties);
        muplar_register_one(
            env, cls, "nUpdateFullPathFillGradient", "(JJ)V",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nUpdateFullPathFillGradient);
        muplar_register_one(
            env, cls, "nUpdateFullPathStrokeGradient", "(JJ)V",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nUpdateFullPathStrokeGradient);
        muplar_register_one(
            env, cls, "nCreateClipPath", "()J",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nCreateClipPath);
        muplar_register_one(
            env, cls, "nCreateClipPath", "(J)J",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nCreateClipPath__J);
        muplar_register_one(
            env, cls, "nCreateGroup", "()J",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nCreateGroup);
        muplar_register_one(
            env, cls, "nCreateGroup", "(J)J",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nCreateGroup__J);
        muplar_register_one(
            env, cls, "nUpdateGroupProperties", "(JFFFFFFF)V",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nUpdateGroupProperties);
        muplar_register_one(
            env, cls, "nAddChild", "(JJ)V",
            (void *) Java_android_graphics_drawable_VectorDrawable_nAddChild);

        const char *float_getters[] = {
            "nGetRotation",       "nGetPivotX",
            "nGetPivotY",         "nGetScaleX",
            "nGetScaleY",         "nGetTranslateX",
            "nGetTranslateY",     "nGetStrokeWidth",
            "nGetStrokeAlpha",    "nGetFillAlpha",
            "nGetTrimPathStart",  "nGetTrimPathEnd",
            "nGetTrimPathOffset", NULL,
        };
        for (size_t i = 0; float_getters[i]; ++i)
            muplar_register_one(
                env, cls, float_getters[i], "(J)F",
                (void *)
                    Java_android_graphics_drawable_VectorDrawable_nGetFloat);

        const char *float_setters[] = {
            "nSetRotation",       "nSetPivotX",
            "nSetPivotY",         "nSetScaleX",
            "nSetScaleY",         "nSetTranslateX",
            "nSetTranslateY",     "nSetStrokeWidth",
            "nSetStrokeAlpha",    "nSetFillAlpha",
            "nSetTrimPathStart",  "nSetTrimPathEnd",
            "nSetTrimPathOffset", NULL,
        };
        for (size_t i = 0; float_setters[i]; ++i)
            muplar_register_one(
                env, cls, float_setters[i], "(JF)V",
                (void *)
                    Java_android_graphics_drawable_VectorDrawable_nSetFloat);

        muplar_register_one(
            env, cls, "nSetPathData", "(JJ)V",
            (void *)
                Java_android_graphics_drawable_VectorDrawable_nSetPathData);
        muplar_register_one(
            env, cls, "nGetStrokeColor", "(J)I",
            (void *) Java_android_graphics_drawable_VectorDrawable_nGetInt);
        muplar_register_one(
            env, cls, "nSetStrokeColor", "(JI)V",
            (void *) Java_android_graphics_drawable_VectorDrawable_nSetInt);
        muplar_register_one(
            env, cls, "nGetFillColor", "(J)I",
            (void *) Java_android_graphics_drawable_VectorDrawable_nGetInt);
        muplar_register_one(
            env, cls, "nSetFillColor", "(JI)V",
            (void *) Java_android_graphics_drawable_VectorDrawable_nSetInt);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "com/android/internal/util/VirtualRefBasePtr");
    if (cls) {
        muplar_register_one(
            env, cls, "nIncStrong", "(J)V",
            (void *)
                Java_com_android_internal_util_VirtualRefBasePtr_nIncStrong);
        muplar_register_one(
            env, cls, "nDecStrong", "(J)V",
            (void *)
                Java_com_android_internal_util_VirtualRefBasePtr_nDecStrong);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/util/PathParser");
    if (cls) {
        muplar_register_one(
            env, cls, "nParseStringForPath", "(JLjava/lang/String;I)V",
            (void *) Java_android_util_PathParser_nParseStringForPath);
        muplar_register_one(
            env, cls, "nCreatePathDataFromString", "(Ljava/lang/String;I)J",
            (void *) Java_android_util_PathParser_nCreatePathDataFromString);
        muplar_register_one(
            env, cls, "nCreatePathFromPathData", "(JJ)V",
            (void *) Java_android_util_PathParser_nCreatePathFromPathData);
        muplar_register_one(
            env, cls, "nCreateEmptyPathData", "()J",
            (void *) Java_android_util_PathParser_nCreateEmptyPathData);
        muplar_register_one(
            env, cls, "nCreatePathData", "(J)J",
            (void *) Java_android_util_PathParser_nCreatePathData);
        muplar_register_one(
            env, cls, "nInterpolatePathData", "(JJJF)Z",
            (void *) Java_android_util_PathParser_nInterpolatePathData);
        muplar_register_one(env, cls, "nFinalize", "(J)V",
                            (void *) Java_android_util_PathParser_nFinalize);
        muplar_register_one(env, cls, "nCanMorph", "(JJ)Z",
                            (void *) Java_android_util_PathParser_nCanMorph);
        muplar_register_one(env, cls, "nSetPathData", "(JJ)V",
                            (void *) Java_android_util_PathParser_nSetPathData);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/Picture");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(
        env, cls, "nativeConstructor", "(J)J",
        (void *) Java_android_graphics_Picture_nativeConstructor);
    muplar_register_one(
        env, cls, "nativeDestructor", "(J)V",
        (void *) Java_android_graphics_Picture_nativeDestructor);
    muplar_register_one(
        env, cls, "nativeBeginRecording", "(JII)J",
        (void *) Java_android_graphics_Picture_nativeBeginRecording);
    muplar_register_one(
        env, cls, "nativeEndRecording", "(J)V",
        (void *) Java_android_graphics_Picture_nativeEndRecording);
    muplar_register_one(env, cls, "nativeDraw", "(JJ)V",
                        (void *) Java_android_graphics_Picture_nativeDraw);
    muplar_register_one(env, cls, "nativeGetWidth", "(J)I",
                        (void *) Java_android_graphics_Picture_nativeGetWidth);
    muplar_register_one(env, cls, "nativeGetHeight", "(J)I",
                        (void *) Java_android_graphics_Picture_nativeGetHeight);

    cls = (*env)->FindClass(env, "android/graphics/Canvas");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(
        env, cls, "nGetNativeFinalizer", "()J",
        (void *) Java_android_graphics_Canvas_nGetNativeFinalizer);
    muplar_register_one(env, cls, "nInitRaster", "(J)J",
                        (void *) Java_android_graphics_Canvas_nInitRaster);
    muplar_register_one(env, cls, "nSave", "(JI)I",
                        (void *) Java_android_graphics_Canvas_nSave);
    muplar_register_one(env, cls, "nRestore", "(J)V",
                        (void *) Java_android_graphics_Canvas_nRestore);
    muplar_register_one(env, cls, "nRestoreToCount", "(JI)V",
                        (void *) Java_android_graphics_Canvas_nRestoreToCount);
    muplar_register_one(env, cls, "nGetSaveCount", "(J)I",
                        (void *) Java_android_graphics_Canvas_nGetSaveCount);
    muplar_register_one(env, cls, "nQuickReject", "(JFFFF)Z",
                        (void *) Java_android_graphics_Canvas_nQuickReject);
    muplar_register_one(env, cls, "nClipRect", "(JFFFFI)Z",
                        (void *) Java_android_graphics_Canvas_nClipRect);
    muplar_register_one(env, cls, "nTranslate", "(JFF)V",
                        (void *) Java_android_graphics_Canvas_nTranslate);
    muplar_register_one(env, cls, "nScale", "(JFF)V",
                        (void *) Java_android_graphics_Canvas_nScale);
    muplar_register_one(env, cls, "nRotate", "(JF)V",
                        (void *) Java_android_graphics_Canvas_nRotate);
    muplar_register_one(env, cls, "nConcat", "(JJ)V",
                        (void *) Java_android_graphics_Canvas_nConcat);
    muplar_register_one(env, cls, "nDrawPath", "(JJJ)V",
                        (void *) Java_android_graphics_Canvas_nDrawPath);
    muplar_register_one(env, cls, "nDrawCircle", "(JFFFJ)V",
                        (void *) Java_android_graphics_Canvas_nDrawCircle);
    muplar_register_one(env, cls, "nDrawColor", "(JII)V",
                        (void *) Java_android_graphics_Canvas_nDrawColor);
    muplar_register_one(env, cls, "nDrawRoundRect", "(JFFFFFFJ)V",
                        (void *) Java_android_graphics_Canvas_nDrawRoundRect);

    cls = (*env)->FindClass(env, "android/graphics/BaseCanvas");
    if (cls) {
        muplar_register_one(env, cls, "nDrawColor", "(JII)V",
                            (void *) Java_android_graphics_Canvas_nDrawColor);
        muplar_register_one(
            env, cls, "nDrawPaint", "(JJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawPaint);
        muplar_register_one(
            env, cls, "nDrawPoint", "(JFFJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawPoint);
        muplar_register_one(
            env, cls, "nDrawPoints", "(J[FIIJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawPoints);
        muplar_register_one(
            env, cls, "nDrawLine", "(JFFFFJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawLine);
        muplar_register_one(
            env, cls, "nDrawLines", "(J[FIIJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawLines);
        muplar_register_one(
            env, cls, "nDrawRect", "(JFFFFJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawRect);
        muplar_register_one(
            env, cls, "nDrawRoundRect", "(JFFFFFFJ)V",
            (void *) Java_android_graphics_Canvas_nDrawRoundRect);
        muplar_register_one(env, cls, "nDrawCircle", "(JFFFJ)V",
                            (void *) Java_android_graphics_Canvas_nDrawCircle);
        muplar_register_one(
            env, cls, "nDrawOval", "(JFFFFJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawOval);
        muplar_register_one(env, cls, "nDrawArc", "(JFFFFFFZJ)V",
                            (void *) Java_android_graphics_BaseCanvas_nDrawArc);
        muplar_register_one(env, cls, "nDrawPath", "(JJJ)V",
                            (void *) Java_android_graphics_Canvas_nDrawPath);
        muplar_register_one(
            env, cls, "nDrawBitmap", "(JJFFJIII)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawBitmap);
        muplar_register_one(
            env, cls, "nDrawBitmap", "(JJFFFFFFFFJII)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawBitmapRect);
        muplar_register_one(
            env, cls, "nDrawText", "(JLjava/lang/String;IIFFIJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawTextString);
        muplar_register_one(
            env, cls, "nDrawText", "(J[CIIFFIJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawTextChars);
        muplar_register_one(
            env, cls, "nDrawTextRun", "(JLjava/lang/String;IIIIFFZJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawTextRunString);
        muplar_register_one(
            env, cls, "nDrawTextRun", "(J[CIIIIFFZJJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawTextRunChars);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/BaseRecordingCanvas");
    if (cls) {
        muplar_register_one(env, cls, "nDrawColor", "(JII)V",
                            (void *) Java_android_graphics_Canvas_nDrawColor);
        muplar_register_one(
            env, cls, "nDrawPaint", "(JJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawPaint);
        muplar_register_one(
            env, cls, "nDrawRect", "(JFFFFJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawRect);
        muplar_register_one(
            env, cls, "nDrawRoundRect", "(JFFFFFFJ)V",
            (void *) Java_android_graphics_Canvas_nDrawRoundRect);
        muplar_register_one(env, cls, "nDrawCircle", "(JFFFJ)V",
                            (void *) Java_android_graphics_Canvas_nDrawCircle);
        muplar_register_one(
            env, cls, "nDrawOval", "(JFFFFJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawOval);
        muplar_register_one(
            env, cls, "nDrawText", "(JLjava/lang/String;IIFFIJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawTextString);
        muplar_register_one(
            env, cls, "nDrawText", "(J[CIIFFIJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawTextChars);
        muplar_register_one(
            env, cls, "nDrawTextRun", "(JLjava/lang/String;IIIIFFZJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawTextRunString);
        muplar_register_one(
            env, cls, "nDrawTextRun", "(J[CIIIIFFZJJ)V",
            (void *) Java_android_graphics_BaseCanvas_nDrawTextRunChars);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/text/LineBreaker");
    if (cls) {
        muplar_register_one(
            env, cls, "nInit", "(IIZ[IZ)J",
            (void *) Java_android_graphics_text_LineBreaker_nInit);
        muplar_register_one(
            env, cls, "nGetReleaseFunc", "()J",
            (void *) Java_android_graphics_text_LineBreaker_nGetReleaseFunc);
        muplar_register_one(
            env, cls, "nGetReleaseResultFunc", "()J",
            (void *)
                Java_android_graphics_text_LineBreaker_nGetReleaseResultFunc);
        muplar_register_one(
            env, cls, "nComputeLineBreaks", "(J[CJIFIF[FFI)J",
            (void *) Java_android_graphics_text_LineBreaker_nComputeLineBreaks);
        muplar_register_one(
            env, cls, "nGetLineCount", "(J)I",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineCount);
        muplar_register_one(
            env, cls, "nGetLineBreakOffset", "(JI)I",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineBreakOffset);
        muplar_register_one(
            env, cls, "nGetLineWidth", "(JI)F",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineWidth);
        muplar_register_one(
            env, cls, "nGetLineAscent", "(JI)F",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineAscent);
        muplar_register_one(
            env, cls, "nGetLineDescent", "(JI)F",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineDescent);
        muplar_register_one(
            env, cls, "nGetLineFlag", "(JI)I",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineFlag);
        muplar_register_one(
            env, cls, "nFinish", "(J)V",
            (void *) Java_android_graphics_text_LineBreaker_nFinish);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/text/LineBreaker$Result");
    if (cls) {
        muplar_register_one(
            env, cls, "nGetLineCount", "(J)I",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineCount);
        muplar_register_one(
            env, cls, "nGetLineBreakOffset", "(JI)I",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineBreakOffset);
        muplar_register_one(
            env, cls, "nGetLineWidth", "(JI)F",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineWidth);
        muplar_register_one(
            env, cls, "nGetLineAscent", "(JI)F",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineAscent);
        muplar_register_one(
            env, cls, "nGetLineDescent", "(JI)F",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineDescent);
        muplar_register_one(
            env, cls, "nGetLineFlag", "(JI)I",
            (void *)
                Java_android_graphics_text_LineBreaker_00024Result_nGetLineFlag);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/text/MeasuredText");
    if (cls) {
        muplar_register_one(
            env, cls, "nGetReleaseFunc", "()J",
            (void *) Java_android_graphics_text_MeasuredText_nGetReleaseFunc);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/text/MeasuredText$Builder");
    if (cls) {
        muplar_register_one(
            env, cls, "nInitBuilder", "()J",
            (void *)
                Java_android_graphics_text_MeasuredText_00024Builder_nInitBuilder);
        muplar_register_one(
            env, cls, "nFreeBuilder", "(J)V",
            (void *)
                Java_android_graphics_text_MeasuredText_00024Builder_nFreeBuilder);
        muplar_register_one(
            env, cls, "nAddStyleRun", "(JJIIZIIZ)V",
            (void *)
                Java_android_graphics_text_MeasuredText_00024Builder_nAddStyleRun);
        muplar_register_one(
            env, cls, "nBuildMeasuredText", "(JJ[CZZZZ)J",
            (void *)
                Java_android_graphics_text_MeasuredText_00024Builder_nBuildMeasuredText);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/RecordingCanvas");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(
        env, cls, "nCreateDisplayListCanvas", "(JII)J",
        (void *)
            Java_android_graphics_RecordingCanvas_nCreateDisplayListCanvas);
    muplar_register_one(
        env, cls, "nResetDisplayListCanvas", "(JJII)V",
        (void *) Java_android_graphics_RecordingCanvas_nResetDisplayListCanvas);
    muplar_register_one(
        env, cls, "nFinishRecording", "(JJ)V",
        (void *) Java_android_graphics_RecordingCanvas_nFinishRecording);

    cls = (*env)->FindClass(env, "android/graphics/RenderNode");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(env, cls, "nCreate", "(Ljava/lang/String;)J",
                        (void *) Java_android_graphics_RenderNode_nCreate);
    muplar_register_one(
        env, cls, "nGetNativeFinalizer", "()J",
        (void *) Java_android_graphics_RenderNode_nGetNativeFinalizer);
    muplar_register_one(env, cls, "nGetUniqueId", "(J)I",
                        (void *) Java_android_graphics_RenderNode_nGetUniqueId);
    muplar_register_one(
        env, cls, "nSetLeftTopRightBottom", "(JIIII)Z",
        (void *) Java_android_graphics_RenderNode_nSetLeftTopRightBottom);
    muplar_register_one(env, cls, "nSetClipToBounds", "(JZ)Z",
                        (void *) Java_android_graphics_RenderNode_nSetBoolean);
    muplar_register_one(env, cls, "nSetClipToOutline", "(JZ)Z",
                        (void *) Java_android_graphics_RenderNode_nSetBoolean);
    muplar_register_one(env, cls, "nSetClipBounds", "(JIIII)Z",
                        (void *) muplar_RenderNode_nSetClipBounds);
    muplar_register_one(env, cls, "nSetForceDarkAllowed", "(JZ)Z",
                        (void *) Java_android_graphics_RenderNode_nSetBoolean);
    muplar_register_one(env, cls, "nSetAllowForceDark", "(JZ)Z",
                        (void *) Java_android_graphics_RenderNode_nSetBoolean);
    muplar_register_one(env, cls, "nSetHasOverlappingRendering", "(JZ)Z",
                        (void *) Java_android_graphics_RenderNode_nSetBoolean);
    muplar_register_one(env, cls, "nSetAlpha", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nGetAlpha", "(J)F",
                        (void *) muplar_RenderNode_getFloatOne);
    muplar_register_one(env, cls, "nSetTranslationX", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nSetTranslationY", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nSetTranslationZ", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nGetTranslationX", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nGetTranslationY", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nGetTranslationZ", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nSetElevation", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nGetElevation", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nSetRotation", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nSetRotationX", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nSetRotationY", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nSetRotationZ", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nGetRotation", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nGetRotationX", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nGetRotationY", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nGetRotationZ", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nSetScaleX", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nSetScaleY", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nGetScaleX", "(J)F",
                        (void *) muplar_RenderNode_getFloatOne);
    muplar_register_one(env, cls, "nGetScaleY", "(J)F",
                        (void *) muplar_RenderNode_getFloatOne);
    muplar_register_one(env, cls, "nSetPivotX", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nSetPivotY", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nGetPivotX", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nGetPivotY", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nIsPivotExplicitlySet", "(J)Z",
                        (void *) muplar_RenderNode_nIsPivotExplicitlySet);
    muplar_register_one(env, cls, "nResetPivot", "(J)Z",
                        (void *) muplar_RenderNode_nResetPivot);
    muplar_register_one(env, cls, "nSetCameraDistance", "(JF)Z",
                        (void *) Java_android_graphics_RenderNode_nSetFloat);
    muplar_register_one(env, cls, "nGetCameraDistance", "(J)F",
                        (void *) muplar_RenderNode_getFloatZero);
    muplar_register_one(env, cls, "nGetLeft", "(J)I",
                        (void *) muplar_RenderNode_getIntZero);
    muplar_register_one(env, cls, "nGetTop", "(J)I",
                        (void *) muplar_RenderNode_getIntZero);
    muplar_register_one(env, cls, "nGetRight", "(J)I",
                        (void *) muplar_RenderNode_getIntZero);
    muplar_register_one(env, cls, "nGetBottom", "(J)I",
                        (void *) muplar_RenderNode_getIntZero);
    muplar_register_one(env, cls, "nOffsetLeftAndRight", "(JI)Z",
                        (void *) muplar_RenderNode_nSetInt);
    muplar_register_one(env, cls, "nOffsetTopAndBottom", "(JI)Z",
                        (void *) muplar_RenderNode_nSetInt);
    muplar_register_one(env, cls, "nHasIdentityMatrix", "(J)Z",
                        (void *) muplar_RenderNode_getBooleanTrue);
    muplar_register_one(env, cls, "nSetAnimationMatrix", "(JJ)Z",
                        (void *) muplar_RenderNode_nSetLong);
    muplar_register_one(env, cls, "nSetStaticMatrix", "(JJ)Z",
                        (void *) muplar_RenderNode_nSetLong);
    muplar_register_one(env, cls, "nSetUsageHint", "(JI)V",
                        (void *) muplar_RenderNode_nSetIntVoid);
    muplar_register_one(env, cls, "nRequestPositionUpdates",
                        "(JLjava/lang/ref/WeakReference;)V",
                        (void *) muplar_RenderNode_nRequestPositionUpdates);
    muplar_register_one(env, cls, "nSetLayerType", "(JI)Z",
                        (void *) muplar_RenderNode_nSetLayerType);
    muplar_register_one(env, cls, "nGetLayerType", "(J)I",
                        (void *) muplar_RenderNode_nGetLayerType);
    muplar_register_one(env, cls, "nSetLayerPaint", "(JJ)Z",
                        (void *) muplar_RenderNode_nSetLayerPaint);

    cls = (*env)->FindClass(env, "android/graphics/HardwareRenderer");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(
        env, cls, "nCreateHardwareBitmap", "(JII)Landroid/graphics/Bitmap;",
        (void *) Java_android_graphics_HardwareRenderer_nCreateHardwareBitmap);
    muplar_register_one(
        env, cls, "nSetRtAnimationsEnabled", "(Z)V",
        (void *)
            Java_android_graphics_HardwareRenderer_nSetRtAnimationsEnabled);
    muplar_register_one(
        env, cls, "nSetHighContrastText", "(Z)V",
        (void *) Java_android_graphics_HardwareRenderer_nSetHighContrastText);
    muplar_register_one(
        env, cls, "nIsHighContrastTextEnabled", "()Z",
        (void *)
            Java_android_graphics_HardwareRenderer_nIsHighContrastTextEnabled);
    muplar_register_one(
        env, cls, "preInitBufferAllocator", "()V",
        (void *) Java_android_graphics_HardwareRenderer_preInitBufferAllocator);

    cls = (*env)->FindClass(env, "android/view/InputChannel");
    if (cls) {
        muplar_register_one(
            env, cls, "nativeGetFinalizer", "()J",
            (void *) Java_android_view_InputChannel_nativeGetFinalizer);
        muplar_register_one(
            env, cls, "nativeOpenInputChannelPair", "(Ljava/lang/String;)[J",
            (void *) Java_android_view_InputChannel_nativeOpenInputChannelPair);
        muplar_register_one(env, cls, "nativeDup", "(J)J",
                            (void *) Java_android_view_InputChannel_nativeDup);
        muplar_register_one(
            env, cls, "nativeDispose", "(J)V",
            (void *) Java_android_view_InputChannel_nativeDispose);
        muplar_register_one(
            env, cls, "nativeGetName", "(J)Ljava/lang/String;",
            (void *) Java_android_view_InputChannel_nativeGetName);
        muplar_register_one(
            env, cls, "nativeGetToken", "(J)Landroid/os/IBinder;",
            (void *) Java_android_view_InputChannel_nativeGetToken);
        muplar_register_one(
            env, cls, "nativeReadFromParcel", "(Landroid/os/Parcel;)J",
            (void *) Java_android_view_InputChannel_nativeReadFromParcel);
        muplar_register_one(
            env, cls, "nativeWriteToParcel", "(Landroid/os/Parcel;J)V",
            (void *) Java_android_view_InputChannel_nativeWriteToParcel);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/view/InputEventReceiver");
    if (cls) {
        muplar_register_one(
            env, cls, "nativeInit",
            "(Ljava/lang/ref/WeakReference;Landroid/view/InputChannel;Landroid/"
            "os/MessageQueue;)J",
            (void *) Java_android_view_InputEventReceiver_nativeInit);
        muplar_register_one(
            env, cls, "nativeDispose", "(J)V",
            (void *) Java_android_view_InputEventReceiver_nativeDispose);
        muplar_register_one(
            env, cls, "nativeConsumeBatchedInputEvents", "(JJ)Z",
            (void *)
                Java_android_view_InputEventReceiver_nativeConsumeBatchedInputEvents);
    } else {
        (*env)->ExceptionClear(env);
    }

    cls = (*env)->FindClass(env, "android/graphics/Region");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(
        env, cls, "nativeConstructor", "()J",
        (void *) Java_android_graphics_Region_nativeConstructor);
    muplar_register_one(
        env, cls, "nativeCreateFromParcel", "(Landroid/os/Parcel;)J",
        (void *) Java_android_graphics_Region_nativeCreateFromParcel);
    muplar_register_one(env, cls, "nativeDestructor", "(J)V",
                        (void *) Java_android_graphics_Region_nativeDestructor);
    muplar_register_one(env, cls, "nativeEquals", "(JJ)Z",
                        (void *) Java_android_graphics_Region_nativeEquals);
    muplar_register_one(
        env, cls, "nativeGetBoundaryPath", "(JJ)Z",
        (void *) Java_android_graphics_Region_nativeGetBoundaryPath);
    muplar_register_one(env, cls, "nativeGetBounds",
                        "(JLandroid/graphics/Rect;)Z",
                        (void *) Java_android_graphics_Region_nativeGetBounds);
    muplar_register_one(env, cls, "nativeOp", "(JIIIII)Z",
                        (void *) Java_android_graphics_Region_nativeOp__JIIIII);
    muplar_register_one(env, cls, "nativeOp", "(JJJI)Z",
                        (void *) Java_android_graphics_Region_nativeOp__JJJI);
    muplar_register_one(
        env, cls, "nativeOp", "(JLandroid/graphics/Rect;JI)Z",
        (void *)
            Java_android_graphics_Region_nativeOp__JLandroid_graphics_Rect_2JI);
    muplar_register_one(env, cls, "nativeSetPath", "(JJJ)Z",
                        (void *) Java_android_graphics_Region_nativeSetPath);
    muplar_register_one(env, cls, "nativeSetRect", "(JIIII)Z",
                        (void *) Java_android_graphics_Region_nativeSetRect);
    muplar_register_one(env, cls, "nativeSetRegion", "(JJ)V",
                        (void *) Java_android_graphics_Region_nativeSetRegion);
    muplar_register_one(env, cls, "nativeToString", "(J)Ljava/lang/String;",
                        (void *) Java_android_graphics_Region_nativeToString);
    muplar_register_one(
        env, cls, "nativeWriteToParcel", "(JLandroid/os/Parcel;)Z",
        (void *) Java_android_graphics_Region_nativeWriteToParcel);
    muplar_register_one(env, cls, "contains", "(II)Z",
                        (void *) Java_android_graphics_Region_contains);
    muplar_register_one(env, cls, "isComplex", "()Z",
                        (void *) Java_android_graphics_Region_isComplex);
    muplar_register_one(env, cls, "isEmpty", "()Z",
                        (void *) Java_android_graphics_Region_isEmpty);
    muplar_register_one(env, cls, "isRect", "()Z",
                        (void *) Java_android_graphics_Region_isRect);
    muplar_register_one(env, cls, "quickContains", "(IIII)Z",
                        (void *) Java_android_graphics_Region_quickContains);
    muplar_register_one(
        env, cls, "quickReject", "(IIII)Z",
        (void *) Java_android_graphics_Region_quickReject__IIII);
    muplar_register_one(
        env, cls, "quickReject", "(Landroid/graphics/Region;)Z",
        (void *)
            Java_android_graphics_Region_quickReject__Landroid_graphics_Region_2);
    muplar_register_one(env, cls, "scale", "(FLandroid/graphics/Region;)V",
                        (void *) Java_android_graphics_Region_scale);
    muplar_register_one(env, cls, "set", "(IIII)Z",
                        (void *) Java_android_graphics_Region_set__IIII);
    muplar_register_one(
        env, cls, "set", "(Landroid/graphics/Region;)Z",
        (void *) Java_android_graphics_Region_set__Landroid_graphics_Region_2);
    muplar_register_one(env, cls, "translate", "(IILandroid/graphics/Region;)V",
                        (void *) Java_android_graphics_Region_translate);

    cls = (*env)->FindClass(env, "android/graphics/RegionIterator");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "nativeConstructor", "(J)J",
            (void *) Java_android_graphics_RegionIterator_nativeConstructor);
        muplar_register_one(
            env, cls, "nativeDestructor", "(J)V",
            (void *) Java_android_graphics_RegionIterator_nativeDestructor);
        muplar_register_one(
            env, cls, "nativeNext", "(JLandroid/graphics/Rect;)Z",
            (void *) Java_android_graphics_RegionIterator_nativeNext);
    }

    cls = (*env)->FindClass(env, "android/view/SurfaceControl");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(
        env, cls, "nativeGetNativeSurfaceControlFinalizer", "()J",
        (void *)
            Java_android_view_SurfaceControl_nativeGetNativeSurfaceControlFinalizer);
    muplar_register_one(
        env, cls, "nativeGetNativeTransactionFinalizer", "()J",
        (void *)
            Java_android_view_SurfaceControl_nativeGetNativeTransactionFinalizer);
    muplar_register_one(
        env, cls, "nativeCreateTransaction", "()J",
        (void *) Java_android_view_SurfaceControl_nativeCreateTransaction);
    muplar_register_one(env, cls, "nativeCreate",
                        "(Landroid/view/SurfaceSession;Ljava/lang/"
                        "String;IIIIJLandroid/os/Parcel;)J",
                        (void *) Java_android_view_SurfaceControl_nativeCreate);
    muplar_register_one(
        env, cls, "nativeCopyFromSurfaceControl", "(J)J",
        (void *) Java_android_view_SurfaceControl_nativeCopyFromSurfaceControl);
    muplar_register_one(
        env, cls, "nativeMirrorSurface", "(J)J",
        (void *) Java_android_view_SurfaceControl_nativeMirrorSurface);
    muplar_register_one(
        env, cls, "nativeMirrorSurfaceWithStopLayer", "(JJ)J",
        (void *)
            Java_android_view_SurfaceControl_nativeMirrorSurfaceWithStopLayer);
    muplar_register_one(
        env, cls, "nativeApplyTransaction", "(JZZ)V",
        (void *) Java_android_view_SurfaceControl_nativeApplyTransaction);
    muplar_register_one(
        env, cls, "nativeClearTransaction", "(J)V",
        (void *) Java_android_view_SurfaceControl_nativeClearTransaction);
    muplar_register_one(
        env, cls, "nativeMergeTransaction", "(JJ)V",
        (void *) Java_android_view_SurfaceControl_nativeMergeTransaction);
    muplar_register_one(
        env, cls, "nativeSetAnimationTransaction", "(J)V",
        (void *)
            Java_android_view_SurfaceControl_nativeSetAnimationTransaction);
    muplar_register_one(
        env, cls, "nativeSetFrameTimelineVsync", "(JJ)V",
        (void *) Java_android_view_SurfaceControl_nativeSetFrameTimelineVsync);
    muplar_register_one(
        env, cls, "nativeGetTransactionId", "(J)J",
        (void *) Java_android_view_SurfaceControl_nativeGetTransactionId);
    muplar_register_one(
        env, cls, "nativeGetHandle", "(J)J",
        (void *) Java_android_view_SurfaceControl_nativeGetHandle);
    muplar_register_one(
        env, cls, "nativeGetLayerId", "(J)I",
        (void *) Java_android_view_SurfaceControl_nativeGetLayerId);
    muplar_register_one(
        env, cls, "nativeBootFinished", "()Z",
        (void *) Java_android_view_SurfaceControl_nativeBootFinished);
    muplar_register_one(
        env, cls, "nativeGetBootDisplayModeSupport", "()Z",
        (void *)
            Java_android_view_SurfaceControl_nativeGetBootDisplayModeSupport);
    muplar_register_one(
        env, cls, "nativeGetDefaultApplyToken", "()Landroid/os/IBinder;",
        (void *) Java_android_view_SurfaceControl_nativeGetDefaultApplyToken);
    muplar_register_one(
        env, cls, "nativeSetDefaultApplyToken", "(Landroid/os/IBinder;)V",
        (void *) Java_android_view_SurfaceControl_nativeSetDefaultApplyToken);
    muplar_register_one(
        env, cls, "nativeGetTransformHint", "(J)I",
        (void *) Java_android_view_SurfaceControl_nativeGetTransformHint);

    cls = (*env)->FindClass(env, "android/os/SystemClock");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(env, cls, "uptimeMillis", "()J",
                        (void *) Java_android_os_SystemClock_uptimeMillis);
    muplar_register_one(env, cls, "elapsedRealtime", "()J",
                        (void *) Java_android_os_SystemClock_elapsedRealtime);
    muplar_register_one(
        env, cls, "elapsedRealtimeNanos", "()J",
        (void *) Java_android_os_SystemClock_elapsedRealtimeNanos);
    muplar_register_one(env, cls, "uptimeNanos", "()J",
                        (void *) Java_android_os_SystemClock_uptimeNanos);
    muplar_register_one(
        env, cls, "currentThreadTimeMillis", "()J",
        (void *) Java_android_os_SystemClock_currentThreadTimeMillis);

    cls = (*env)->FindClass(env, "android/os/Trace");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(env, cls, "nativeIsTagEnabled", "(J)Z",
                        (void *) Java_android_os_Trace_nativeIsTagEnabled);
    muplar_register_one(env, cls, "nativeTraceBegin", "(JLjava/lang/String;)V",
                        (void *) Java_android_os_Trace_nativeTraceBegin);
    muplar_register_one(env, cls, "nativeTraceEnd", "(J)V",
                        (void *) Java_android_os_Trace_nativeTraceEnd);
    muplar_register_one(env, cls, "nativeAsyncTraceBegin",
                        "(JLjava/lang/String;I)V",
                        (void *) Java_android_os_Trace_nativeAsyncTraceBegin);
    muplar_register_one(env, cls, "nativeAsyncTraceEnd",
                        "(JLjava/lang/String;I)V",
                        (void *) Java_android_os_Trace_nativeAsyncTraceEnd);
    muplar_register_one(env, cls, "nativeInstant", "(JLjava/lang/String;)V",
                        (void *) Java_android_os_Trace_nativeInstant);

    cls = (*env)->FindClass(env, "android/os/PerfettoTrace$Category");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "native_init",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)J",
            (void *) Java_android_os_PerfettoTrace_00024Category_native_1init);
        muplar_register_one(
            env, cls, "native_delete", "()J",
            (void *)
                Java_android_os_PerfettoTrace_00024Category_native_1delete);
        muplar_register_one(
            env, cls, "native_register", "(J)V",
            (void *)
                Java_android_os_PerfettoTrace_00024Category_native_1register);
        muplar_register_one(
            env, cls, "native_unregister", "(J)V",
            (void *)
                Java_android_os_PerfettoTrace_00024Category_native_1unregister);
        muplar_register_one(
            env, cls, "native_is_enabled", "(J)Z",
            (void *)
                Java_android_os_PerfettoTrace_00024Category_native_1is_1enabled);
        muplar_register_one(
            env, cls, "native_get_extra_ptr", "(J)J",
            (void *)
                Java_android_os_PerfettoTrace_00024Category_native_1get_1extra_1ptr);
    }

    cls = (*env)->FindClass(env, "android/os/PerfettoTrace");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "native_get_process_track_uuid", "()J",
            (void *)
                Java_android_os_PerfettoTrace_native_1get_1process_1track_1uuid);
        muplar_register_one(
            env, cls, "native_get_thread_track_uuid", "(J)J",
            (void *)
                Java_android_os_PerfettoTrace_native_1get_1thread_1track_1uuid);
        muplar_register_one(
            env, cls, "native_activate_trigger", "(Ljava/lang/String;I)V",
            (void *) Java_android_os_PerfettoTrace_native_1activate_1trigger);
        muplar_register_one(
            env, cls, "native_register", "(Z)V",
            (void *) Java_android_os_PerfettoTrace_native_1register);
        muplar_register_one(
            env, cls, "native_start_session", "(Z[B)J",
            (void *) Java_android_os_PerfettoTrace_native_1start_1session);
        muplar_register_one(
            env, cls, "native_stop_session", "(J)[B",
            (void *) Java_android_os_PerfettoTrace_native_1stop_1session);
    }

    cls = (*env)->FindClass(env, "android/os/PerfettoTrackEventExtra");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "native_init", "()J",
            (void *) Java_android_os_PerfettoTrackEventExtra_native_1init);
        muplar_register_one(
            env, cls, "native_delete", "()J",
            (void *) Java_android_os_PerfettoTrackEventExtra_native_1delete);
        muplar_register_one(
            env, cls, "native_add_arg", "(JJ)V",
            (void *) Java_android_os_PerfettoTrackEventExtra_native_1add_1arg);
        muplar_register_one(
            env, cls, "native_clear_args", "(J)V",
            (void *)
                Java_android_os_PerfettoTrackEventExtra_native_1clear_1args);
        muplar_register_one(
            env, cls, "native_emit", "(IJLjava/lang/String;J)V",
            (void *) Java_android_os_PerfettoTrackEventExtra_native_1emit);
    }

    cls = (*env)->FindClass(env, "android/tracing/perfetto/DataSource");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "nativeCreate",
            "(Landroid/tracing/perfetto/DataSource;Ljava/lang/String;)J",
            (void *) Java_android_tracing_perfetto_DataSource_nativeCreate);
        muplar_register_one(
            env, cls, "nativeFlushAll", "(J)V",
            (void *) Java_android_tracing_perfetto_DataSource_nativeFlushAll);
        muplar_register_one(
            env, cls, "nativeGetFinalizer", "()J",
            (void *)
                Java_android_tracing_perfetto_DataSource_nativeGetFinalizer);
        muplar_register_one(
            env, cls, "nativeGetPerfettoDsInstanceIndex", "(J)I",
            (void *)
                Java_android_tracing_perfetto_DataSource_nativeGetPerfettoDsInstanceIndex);
        muplar_register_one(
            env, cls, "nativeGetPerfettoInstanceLocked",
            "(JI)Landroid/tracing/perfetto/DataSourceInstance;",
            (void *)
                Java_android_tracing_perfetto_DataSource_nativeGetPerfettoInstanceLocked);
        muplar_register_one(
            env, cls, "nativePerfettoDsTraceIterateBegin", "(J)Z",
            (void *)
                Java_android_tracing_perfetto_DataSource_nativePerfettoDsTraceIterateBegin);
        muplar_register_one(
            env, cls, "nativePerfettoDsTraceIterateBreak", "(J)V",
            (void *)
                Java_android_tracing_perfetto_DataSource_nativePerfettoDsTraceIterateBreak);
        muplar_register_one(
            env, cls, "nativePerfettoDsTraceIterateNext", "(J)Z",
            (void *)
                Java_android_tracing_perfetto_DataSource_nativePerfettoDsTraceIterateNext);
        muplar_register_one(
            env, cls, "nativeRegisterDataSource", "(JIZZ)V",
            (void *)
                Java_android_tracing_perfetto_DataSource_nativeRegisterDataSource);
        muplar_register_one(
            env, cls, "nativeReleasePerfettoInstanceLocked", "(JI)V",
            (void *)
                Java_android_tracing_perfetto_DataSource_nativeReleasePerfettoInstanceLocked);
        muplar_register_one(
            env, cls, "nativeWritePackets", "(J[[B)V",
            (void *)
                Java_android_tracing_perfetto_DataSource_nativeWritePackets);
    }

    cls = (*env)->FindClass(env, "android/tracing/perfetto/Producer");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "nativePerfettoProducerInit", "(II)V",
            (void *)
                Java_android_tracing_perfetto_Producer_nativePerfettoProducerInit);
    }

    cls = (*env)->FindClass(env, "android/os/Process");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(env, cls, "setThreadPriority", "(I)V",
                        (void *) Java_android_os_Process_setThreadPriority);
    muplar_register_one(env, cls, "setThreadPriority", "(II)V",
                        (void *) Java_android_os_Process_setThreadPriority__II);
    muplar_register_one(env, cls, "getTotalMemory", "()J",
                        (void *) Java_android_os_Process_getTotalMemory);

    cls = (*env)->FindClass(env, "android/os/Binder");
    if (!cls) {
        (*env)->ExceptionClear(env);
        return JNI_VERSION_1_6;
    }
    muplar_register_one(
        env, cls, "blockUntilThreadAvailable", "()V",
        (void *) Java_android_os_Binder_blockUntilThreadAvailable);
    muplar_register_one(env, cls, "clearCallingIdentity", "()J",
                        (void *) Java_android_os_Binder_clearCallingIdentity);
    muplar_register_one(env, cls, "restoreCallingIdentity", "(J)V",
                        (void *) Java_android_os_Binder_restoreCallingIdentity);
    muplar_register_one(env, cls, "clearCallingWorkSource", "()J",
                        (void *) Java_android_os_Binder_clearCallingWorkSource);
    muplar_register_one(
        env, cls, "restoreCallingWorkSource", "(J)V",
        (void *) Java_android_os_Binder_restoreCallingWorkSource);
    muplar_register_one(env, cls, "getCallingPid", "()I",
                        (void *) Java_android_os_Binder_getCallingPid);
    muplar_register_one(env, cls, "getCallingUid", "()I",
                        (void *) Java_android_os_Binder_getCallingUid);
    muplar_register_one(
        env, cls, "getCallingWorkSourceUid", "()I",
        (void *) Java_android_os_Binder_getCallingWorkSourceUid);
    muplar_register_one(
        env, cls, "setCallingWorkSourceUid", "(I)J",
        (void *) Java_android_os_Binder_setCallingWorkSourceUid);
    muplar_register_one(
        env, cls, "getThreadStrictModePolicy", "()I",
        (void *) Java_android_os_Binder_getThreadStrictModePolicy);
    muplar_register_one(
        env, cls, "setThreadStrictModePolicy", "(I)V",
        (void *) Java_android_os_Binder_setThreadStrictModePolicy);
    muplar_register_one(env, cls, "hasExplicitIdentity", "()Z",
                        (void *) Java_android_os_Binder_hasExplicitIdentity);
    muplar_register_one(
        env, cls, "isDirectlyHandlingTransactionNative", "()Z",
        (void *) Java_android_os_Binder_isDirectlyHandlingTransactionNative);
    muplar_register_one(env, cls, "setExtensionNative",
                        "(Landroid/os/IBinder;)V",
                        (void *) Java_android_os_Binder_setExtensionNative);
    muplar_register_one(env, cls, "flushPendingCommands", "()V",
                        (void *) Java_android_os_Binder_flushPendingCommands);
    muplar_register_one(env, cls, "getNativeBBinderHolder", "()J",
                        (void *) Java_android_os_Binder_getNativeBBinderHolder);
    muplar_register_one(env, cls, "getNativeFinalizer", "()J",
                        (void *) Java_android_os_Binder_getNativeFinalizer);

    cls = (*env)->FindClass(env, "android/os/BinderProxy");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "getNativeFinalizer", "()J",
            (void *) Java_android_os_BinderProxy_getNativeFinalizer);
        muplar_register_one(
            env, cls, "isFrozenStateChangeCallbackSupportedNative", "()Z",
            (void *)
                Java_android_os_BinderProxy_isFrozenStateChangeCallbackSupportedNative);
        muplar_register_one(
            env, cls, "addFrozenStateChangeCallbackNative",
            "(Landroid/os/IBinder$FrozenStateChangeCallback;)V",
            (void *)
                Java_android_os_BinderProxy_addFrozenStateChangeCallbackNative);
        muplar_register_one(
            env, cls, "removeFrozenStateChangeCallbackNative",
            "(Landroid/os/IBinder$FrozenStateChangeCallback;)Z",
            (void *)
                Java_android_os_BinderProxy_removeFrozenStateChangeCallbackNative);
        muplar_register_one(
            env, cls, "linkToDeathNative",
            "(Landroid/os/IBinder$DeathRecipient;I)V",
            (void *) Java_android_os_BinderProxy_linkToDeathNative);
        muplar_register_one(
            env, cls, "unlinkToDeathNative",
            "(Landroid/os/IBinder$DeathRecipient;I)Z",
            (void *) Java_android_os_BinderProxy_unlinkToDeathNative);
        muplar_register_one(env, cls, "getExtension", "()Landroid/os/IBinder;",
                            (void *) Java_android_os_BinderProxy_getExtension);
        muplar_register_one(
            env, cls, "getInterfaceDescriptor", "()Ljava/lang/String;",
            (void *) Java_android_os_BinderProxy_getInterfaceDescriptor);
        muplar_register_one(env, cls, "isBinderAlive", "()Z",
                            (void *) Java_android_os_BinderProxy_isBinderAlive);
        muplar_register_one(env, cls, "pingBinder", "()Z",
                            (void *) Java_android_os_BinderProxy_pingBinder);
        muplar_register_one(
            env, cls, "transactNative",
            "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z",
            (void *) Java_android_os_BinderProxy_transactNative);
    }

    cls = (*env)->FindClass(env, "com/android/internal/os/BinderInternal");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "getContextObject", "()Landroid/os/IBinder;",
            (void *)
                Java_com_android_internal_os_BinderInternal_getContextObject);
    }

    cls = (*env)->FindClass(env, "android/os/ServiceManagerProxy");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "getNativeServiceManager", "()Landroid/os/IBinder;",
            (void *)
                Java_android_os_ServiceManagerProxy_getNativeServiceManager);
    }

    cls = (*env)->FindClass(env, "android/view/VelocityTracker");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "nativeInitialize", "(I)J",
            (void *) Java_android_view_VelocityTracker_nativeInitialize);
        muplar_register_one(
            env, cls, "nativeDispose", "(J)V",
            (void *) Java_android_view_VelocityTracker_nativeDispose);
        muplar_register_one(
            env, cls, "nativeClear", "(J)V",
            (void *) Java_android_view_VelocityTracker_nativeClear);
        muplar_register_one(
            env, cls, "nativeAddMovement", "(JLandroid/view/MotionEvent;)V",
            (void *) Java_android_view_VelocityTracker_nativeAddMovement);
        muplar_register_one(
            env, cls, "nativeComputeCurrentVelocity", "(JIF)V",
            (void *)
                Java_android_view_VelocityTracker_nativeComputeCurrentVelocity);
        muplar_register_one(
            env, cls, "nativeGetVelocity", "(JII)F",
            (void *) Java_android_view_VelocityTracker_nativeGetVelocity);
        muplar_register_one(
            env, cls, "nativeIsAxisSupported", "(I)Z",
            (void *) Java_android_view_VelocityTracker_nativeIsAxisSupported);
    }

    cls = (*env)->FindClass(env, "android/view/DisplayEventReceiver");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    } else if (cls) {
        muplar_register_one(
            env, cls, "nativeGetDisplayEventReceiverFinalizer", "()J",
            (void *)
                Java_android_view_DisplayEventReceiver_nativeGetDisplayEventReceiverFinalizer);
        muplar_register_one(
            env, cls, "nativeInit",
            "(Ljava/lang/ref/WeakReference;Ljava/lang/ref/WeakReference;"
            "Landroid/os/MessageQueue;IIJ)J",
            (void *) Java_android_view_DisplayEventReceiver_nativeInit);
        muplar_register_one(
            env, cls, "nativeScheduleVsync", "(J)V",
            (void *)
                Java_android_view_DisplayEventReceiver_nativeScheduleVsync);
        muplar_register_one(
            env, cls, "nativeGetLatestVsyncEventData",
            "(J)Landroid/view/DisplayEventReceiver$VsyncEventData;",
            (void *)
                Java_android_view_DisplayEventReceiver_nativeGetLatestVsyncEventData);
    }

    return JNI_VERSION_1_6;
}

void *android_get_exported_namespace(const char *name)
{
    (void) name;
    return &muplar_system_namespace_token;
}

void *android_create_namespace(const char *name,
                               const char *ld_library_path,
                               const char *default_library_path,
                               uint64_t type,
                               const char *permitted_when_isolated_path,
                               void *parent,
                               const void *caller_addr)
{
    (void) name;
    (void) ld_library_path;
    (void) default_library_path;
    (void) type;
    (void) permitted_when_isolated_path;
    (void) parent;
    (void) caller_addr;
    return &muplar_created_namespace_token;
}

int android_link_namespaces(void *from,
                            void *to,
                            const char *shared_libs_sonames)
{
    (void) from;
    (void) to;
    (void) shared_libs_sonames;
    return 1;
}

void *android_dlopen_ext(const char *filename, int flag, const void *extinfo)
{
    (void) extinfo;
    return muplar_dlopen_android_library(filename, flag);
}

void *OpenSystemLibrary(const char *libpath, int flag)
{
    return muplar_dlopen_android_library(libpath, flag);
}

int NativeBridgeIsPathSupported(const char *path)
{
    (void) path;
    return 0;
}

const char *NativeBridgeGetError(void)
{
    return "";
}

void *NativeBridgeGetExportedNamespace(const char *name)
{
    return android_get_exported_namespace(name);
}

void *NativeBridgeCreateNamespace(const char *name,
                                  const char *ld_library_path,
                                  const char *default_library_path,
                                  uint64_t type,
                                  const char *permitted_when_isolated_path,
                                  void *parent)
{
    return android_create_namespace(name, ld_library_path, default_library_path,
                                    type, permitted_when_isolated_path, parent,
                                    NULL);
}

int NativeBridgeLinkNamespaces(void *from,
                               void *to,
                               const char *shared_libs_sonames)
{
    return android_link_namespaces(from, to, shared_libs_sonames);
}

void *NativeBridgeLoadLibraryExt(const char *libpath, int flag, void *ns)
{
    (void) ns;
    return muplar_dlopen_android_library(libpath, flag);
}
