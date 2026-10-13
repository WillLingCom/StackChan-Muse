/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 the StackChan K151 Muse port authors.
 * The capture sequence follows M5Stack's StackChan firmware
 * (main/hal/board/stackchan_camera.cc @1b57655, MIT) and esp_video's V4L2 API.
 */

#include "stackchan_camera.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#include "bsp/esp-bsp.h"
#include "camera.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_enc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_ioctl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "sdkconfig.h"

#define CAM_W           320
#define CAM_H           240
#define CAM_BUFS        2
#define WARMUP_FRAMES   8       /* ~0.4 s at 20 fps: exposure settles, and the photo is new */
#define DQBUF_MS        500
#define JPEG_QUALITY    70
/* camera.capture results over 16 KB came back to Muse as "a string of data"
 * (17.4 KB failed; 12-15 KB worked): keep the JPEG under JPEG_MAX so the
 * message (4*ceil(n/3) base64 + 186 B measured overhead) stays near 15.5 KB,
 * stepping the quality down. */
#define JPEG_MAX        11500
#define JPEG_CAP        (64 * 1024)

static const char *TAG = "camera";
static int s_fd = -1;
#if CONFIG_MUSE_STACKCHAN_CAMERA_HFLIP
#define HFLIP_DEFAULT 1
#else
#define HFLIP_DEFAULT 0
#endif
static int s_hflip = HFLIP_DEFAULT, s_hflip_set = -1;
static uint32_t s_fourcc;
static jpeg_pixel_format_t s_src;
static struct {
    void *start;
    size_t len;
} s_buf[CAM_BUFS];

typedef struct {
    int64_t captured_us;
} frame_priv_t;

static const char *fourcc(uint32_t f, char out[5])
{
    for (int i = 0; i < 4; i++) {
        out[i] = (char)(f >> (8 * i));
    }
    out[4] = 0;
    return out;
}

/* Prefer a format the software encoder takes as is. */
static bool pick_format(void)
{
    static const struct {
        uint32_t v4l2;
        jpeg_pixel_format_t jpeg;
    } PREFS[] = {
        { V4L2_PIX_FMT_YUYV, JPEG_PIXEL_FORMAT_YCbYCr },
        { V4L2_PIX_FMT_UYVY, JPEG_PIXEL_FORMAT_CbYCrY },
        { V4L2_PIX_FMT_RGB565, JPEG_PIXEL_FORMAT_RGB565_LE },
        { V4L2_PIX_FMT_GREY, JPEG_PIXEL_FORMAT_GRAY },
    };
    uint32_t have[16];
    int n = 0;
    struct v4l2_fmtdesc d = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    char cc[5];
    while (n < 16 && ioctl(s_fd, VIDIOC_ENUM_FMT, &d) == 0) {
        ESP_LOGI(TAG, "sensor format %s (%s)", fourcc(d.pixelformat, cc), (const char *)d.description);
        have[n++] = d.pixelformat;
        d.index++;
    }
    for (size_t p = 0; p < sizeof(PREFS) / sizeof(PREFS[0]); p++) {
        for (int i = 0; i < n; i++) {
            if (have[i] == PREFS[p].v4l2) {
                s_fourcc = PREFS[p].v4l2;
                s_src = PREFS[p].jpeg;
                return true;
            }
        }
    }
    return false;
}

static esp_err_t cam_init(void)
{
    esp_err_t err = bsp_camera_start(NULL);   /* powers the camera, starts esp_video on the shared I2C bus */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera start: %s", esp_err_to_name(err));
        return err;
    }
    s_fd = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDWR);
    if (s_fd < 0) {
        ESP_LOGE(TAG, "open %s: errno %d", ESP_VIDEO_DVP_DEVICE_NAME, errno);
        return ESP_FAIL;
    }
    struct v4l2_capability cap = { 0 };
    if (ioctl(s_fd, VIDIOC_QUERYCAP, &cap) == 0) {
        ESP_LOGI(TAG, "%s: %s", (const char *)cap.driver, (const char *)cap.card);
    }
    if (!pick_format()) {
        ESP_LOGE(TAG, "no format the JPEG encoder takes");
        return ESP_ERR_NOT_SUPPORTED;
    }
    struct v4l2_format f = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    f.fmt.pix.width = CAM_W;
    f.fmt.pix.height = CAM_H;
    f.fmt.pix.pixelformat = s_fourcc;
    if (ioctl(s_fd, VIDIOC_S_FMT, &f) != 0) {
        ESP_LOGE(TAG, "VIDIOC_S_FMT: errno %d", errno);
        return ESP_FAIL;
    }
    char cc[5];
    ESP_LOGI(TAG, "capturing %s %lux%lu", fourcc(f.fmt.pix.pixelformat, cc), (unsigned long)f.fmt.pix.width,
             (unsigned long)f.fmt.pix.height);
    if (f.fmt.pix.width != CAM_W || f.fmt.pix.height != CAM_H) {
        ESP_LOGE(TAG, "the sensor gave another size");
        return ESP_ERR_NOT_SUPPORTED;
    }
    struct v4l2_requestbuffers req = { .count = CAM_BUFS, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
    if (ioctl(s_fd, VIDIOC_REQBUFS, &req) != 0 || req.count < 1) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS: errno %d", errno);
        return ESP_FAIL;
    }
    for (unsigned i = 0; i < req.count && i < CAM_BUFS; i++) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i };
        if (ioctl(s_fd, VIDIOC_QUERYBUF, &b) != 0) {
            return ESP_FAIL;
        }
        s_buf[i].start = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, b.m.offset);
        s_buf[i].len = b.length;
        if (!s_buf[i].start) {
            return ESP_ERR_NO_MEM;
        }
    }
    struct timeval tv = { .tv_sec = 0, .tv_usec = DQBUF_MS * 1000 };
    ioctl(s_fd, VIDIOC_S_DQBUF_TIMEOUT, &tv);   /* never block forever on a stalled sensor */
    return ESP_OK;
}

static esp_err_t encode_q(const uint8_t *src, size_t len, int quality, camera_frame_t *out)
{
    jpeg_enc_config_t cfg = DEFAULT_JPEG_ENC_CONFIG();
    cfg.width = CAM_W;
    cfg.height = CAM_H;
    cfg.src_type = s_src;
    cfg.subsampling = s_src == JPEG_PIXEL_FORMAT_GRAY ? JPEG_SUBSAMPLE_GRAY : JPEG_SUBSAMPLE_420;
    cfg.quality = quality;
    jpeg_enc_handle_t enc = NULL;
    if (jpeg_enc_open(&cfg, &enc) != JPEG_ERR_OK) {
        return ESP_FAIL;
    }
    uint8_t *jpg = heap_caps_malloc(JPEG_CAP, MALLOC_CAP_SPIRAM);
    int n = 0;
    jpeg_error_t je = jpg ? jpeg_enc_process(enc, src, (int)len, jpg, JPEG_CAP, &n) : JPEG_ERR_NO_MEM;
    jpeg_enc_close(enc);
    if (je != JPEG_ERR_OK || n <= 0) {
        heap_caps_free(jpg);
        ESP_LOGE(TAG, "JPEG encode failed (%d)", je);
        return ESP_FAIL;
    }
    frame_priv_t *priv = heap_caps_malloc(sizeof(*priv), MALLOC_CAP_SPIRAM);
    if (!priv) {
        heap_caps_free(jpg);
        return ESP_ERR_NO_MEM;
    }
    priv->captured_us = esp_timer_get_time();
    *out = (camera_frame_t){ .jpeg = jpg, .len = (size_t)n, .width = CAM_W, .height = CAM_H, .priv = priv };
    return ESP_OK;
}

/* esp_video takes controls only as VIDIOC_S_EXT_CTRLS (VIDIOC_S_CTRL is EINVAL). */
static void apply_hflip(void)
{
    if (s_hflip == s_hflip_set) {
        return;
    }
    struct v4l2_ext_control c = { .id = V4L2_CID_HFLIP, .value = s_hflip };
    struct v4l2_ext_controls cs = { .ctrl_class = V4L2_CTRL_CLASS_USER, .count = 1, .controls = &c };
    if (ioctl(s_fd, VIDIOC_S_EXT_CTRLS, &cs) == 0) {
        s_hflip_set = s_hflip;
        ESP_LOGI(TAG, "horizontal flip %s", s_hflip ? "on" : "off");
    } else {
        ESP_LOGW(TAG, "set horizontal flip: errno %d", errno);
    }
}

void stackchan_camera_set_hflip(bool on)
{
    s_hflip = on;
}

/* Internal RAM at a step, and its lowest point since the previous step (S7 memory audit). */
static void mem_step(const char *step)
{
    /* While local monitoring runs, the minimum is the lowest since it started. */
    size_t low = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    heap_caps_monitor_local_minimum_free_size_stop();
    ESP_LOGD(TAG, "mem %-9s internal free %uK, lowest %uK, largest block %uK", step,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(low / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    heap_caps_monitor_local_minimum_free_size_start();
}

static void cam_release(camera_frame_t *frame);

static esp_err_t encode(const uint8_t *src, size_t len, camera_frame_t *out)
{
    static const int QUALITY[] = { JPEG_QUALITY, 60, 50, 40, 30 };
    esp_err_t err = ESP_FAIL;
    for (size_t i = 0; i < sizeof(QUALITY) / sizeof(QUALITY[0]); i++) {
        err = encode_q(src, len, QUALITY[i], out);
        if (err != ESP_OK || out->len <= JPEG_MAX || i + 1 == sizeof(QUALITY) / sizeof(QUALITY[0])) {
            if (err == ESP_OK && i) {
                ESP_LOGI(TAG, "JPEG at quality %d to stay under %d bytes", QUALITY[i], JPEG_MAX);
            }
            return err;
        }
        cam_release(out);   /* too big for one Muse tool result: again, smaller */
    }
    return err;
}

static esp_err_t cam_capture(camera_frame_t *out)
{
    heap_caps_monitor_local_minimum_free_size_start();
    mem_step("start");
    int64_t t0 = esp_timer_get_time();
    apply_hflip();
    for (unsigned i = 0; i < CAM_BUFS; i++) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i };
        ioctl(s_fd, VIDIOC_QBUF, &b);
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON: errno %d", errno);
        heap_caps_monitor_local_minimum_free_size_stop();
        return ESP_FAIL;
    }
    mem_step("streamon");
    esp_err_t err = ESP_ERR_TIMEOUT;
    for (int i = 0; i <= WARMUP_FRAMES; i++) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
        if (ioctl(s_fd, VIDIOC_DQBUF, &b) != 0) {
            ESP_LOGE(TAG, "no frame within %d ms (errno %d)", DQBUF_MS, errno);
            err = ESP_ERR_TIMEOUT;
            break;
        }
        if (i == WARMUP_FRAMES) {
            mem_step("frames");
            err = encode(s_buf[b.index].start, b.bytesused ? b.bytesused : s_buf[b.index].len, out);
            mem_step("encode");
        }
        ioctl(s_fd, VIDIOC_QBUF, &b);
    }
    ioctl(s_fd, VIDIOC_STREAMOFF, &type);   /* stills only: no preview runs between captures */
    mem_step("streamoff");
    heap_caps_monitor_local_minimum_free_size_stop();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "photo: %u bytes JPEG in %d ms", (unsigned)out->len, (int)((esp_timer_get_time() - t0) / 1000));
    }
    return err;
}

static void cam_release(camera_frame_t *frame)
{
    heap_caps_free((void *)frame->jpeg);
    heap_caps_free(frame->priv);
    memset(frame, 0, sizeof(*frame));
}

static const camera_driver_t s_driver = {
    .name = "GC0308 (StackChan K151)",
    .max_width = CAM_W,
    .max_height = CAM_H,
    .init = cam_init,
    .capture = cam_capture,
    .release = cam_release,
};

void stackchan_camera_register(void)
{
    camera_register(&s_driver);
}
