/*
 * Copyright (c) 2012, 2013 Erik Faye-Lund
 * Copyright (c) 2013 Avionic Design GmbH
 * Copyright (c) 2013 Thierry Reding
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef HOST1X_H
#define HOST1X_H

#define FLOAT_TO_FIXED_6_12(fp) \
    (((int32_t) (fp * 4096.0f + 0.5f)) & ((1 << 18) - 1))

#define FLOAT_TO_FIXED_s2_7(fp) \
    (((fp < 0.0f) << 9) | (((int32_t) (fabs(fp) * 128.0f)) & ((1 << 9) - 1)))

#define FLOAT_TO_FIXED_s1_7(fp) \
    (((fp < 0.0f) << 8) | (((int32_t) (fabs(fp) * 128.0f)) & ((1 << 8) - 1)))

#define FLOAT_TO_FIXED_0_8(fp) \
    (((int32_t) (fp * 256.0f + 0.5f)) & ((1 << 8) - 1))

#define HOST1X_OPCODE_SETCL(offset, classid, mask) \
    ((0x0 << 28) | (((offset) & 0xfff) << 16) | (((classid) & 0x3ff) << 6) | ((mask) & 0x3f))
#define HOST1X_OPCODE_INCR(offset, count) \
    ((0x1 << 28) | (((offset) & 0xfff) << 16) | ((count) & 0xffff))
#define HOST1X_OPCODE_NONINCR(offset, count) \
    ((0x2 << 28) | (((offset) & 0xfff) << 16) | ((count) & 0xffff))
#define HOST1X_OPCODE_MASK(offset, mask) \
    ((0x3 << 28) | (((offset) & 0xfff) << 16) | ((mask) & 0xffff))
#define HOST1X_OPCODE_IMM(offset, data) \
    ((0x4 << 28) | (((offset) & 0xfff) << 16) | ((data) & 0xffff))
#define HOST1X_OPCODE_EXTEND(subop, value) \
    ((0xe << 28) | (((subop) & 0xf) << 24) | ((value) & 0xffffff))

#define HOST1X_CLASS_GR2D       0x51
#define HOST1X_CLASS_GR2D_SB    0x52

#define host1x_error(fmt, args...) \
    fprintf(stderr, "ERROR: %s: %d: " fmt, __func__, __LINE__, ##args)

#define PIX_BUF_FMT(id, bpp, planes_nb) \
    ((planes_nb) << 16 | (id) << 8 | (bpp))

#define PIX_BUF_FORMAT_BITS(f) \
    ((f) & 0xff)

#define PIX_BUF_FORMAT_BYTES(f) \
    (PIX_BUF_FORMAT_BITS(f) >> 3)

#define PIX_BUF_FORMAT_PLANES_NB(f) \
    (((f) >> 16) & 3)

enum pixel_format {
    PIX_BUF_FMT_A8            = PIX_BUF_FMT(0, 8, 1),
    PIX_BUF_FMT_L8            = PIX_BUF_FMT(1, 8, 1),
    PIX_BUF_FMT_S8            = PIX_BUF_FMT(2, 8, 1),
    PIX_BUF_FMT_LA88          = PIX_BUF_FMT(3, 16, 1),
    PIX_BUF_FMT_RGB565        = PIX_BUF_FMT(4, 16, 1),
    PIX_BUF_FMT_RGBA5551      = PIX_BUF_FMT(5, 16, 1),
    PIX_BUF_FMT_RGBA4444      = PIX_BUF_FMT(6, 16, 1),
    PIX_BUF_FMT_D16_LINEAR    = PIX_BUF_FMT(7, 16, 1),
    PIX_BUF_FMT_D16_NONLINEAR = PIX_BUF_FMT(8, 16, 1),
    PIX_BUF_FMT_RGBA8888      = PIX_BUF_FMT(9, 32, 1),
    PIX_BUF_FMT_RGBA_FP32     = PIX_BUF_FMT(10, 32, 1),
    PIX_BUF_FMT_ARGB8888      = PIX_BUF_FMT(11, 32, 1),
    PIX_BUF_FMT_ABGR8888      = PIX_BUF_FMT(12, 32, 1),
    PIX_BUF_FMT_YV12          = PIX_BUF_FMT(13, 8, 3),
};

enum layout_format {
    PIX_BUF_LAYOUT_LINEAR,
    PIX_BUF_LAYOUT_TILED_16x16,
};

struct host1x_csc_params {
    uint32_t yos, cvr, cub;
    uint32_t cyx, cur, cug;
    uint32_t cvb, cvg;
};

extern struct host1x_csc_params csc_rgb_default;

struct host1x_pixelbuffer {
    union {
        struct drm_tegra_bo *bo;
        struct drm_tegra_bo *bos[3];
    };
    enum pixel_format format;
    enum layout_format layout;
    unsigned width;
    unsigned height;
    unsigned pitch;
    unsigned pitch_uv;
    uint32_t guard_offset[3];
    uint32_t bo_offset[3];
    bool guard_enabled;
};

#define PIXBUF_GUARD_AREA_SIZE    0x4000

struct host1x_pixelbuffer *host1x_pixelbuffer_create(struct drm_tegra *drm,
                                                     unsigned width,
                                                     unsigned height,
                                                     unsigned pitch,
                                                     unsigned pitch_uv,
                                                     enum pixel_format format,
                                                     enum layout_format layout);

struct host1x_pixelbuffer *host1x_pixelbuffer_wrap(struct drm_tegra_bo **bos,
                                                   unsigned width,
                                                   unsigned height,
                                                   unsigned pitch,
                                                   unsigned pitch_uv,
                                                   enum pixel_format format,
                                                   enum layout_format layout);

void host1x_pixelbuffer_free(struct host1x_pixelbuffer *pixbuf);

int host1x_pixelbuffer_load_data(struct drm_tegra *drm,
                                 struct tegra_stream *stream,
                                 struct host1x_pixelbuffer *pixbuf,
                                 void *data,
                                 unsigned data_pitch,
                                 unsigned long data_size,
                                 enum pixel_format data_format,
                                 enum layout_format data_layout);

int host1x_pixelbuffer_setup_guard(struct host1x_pixelbuffer *pixbuf);

int host1x_pixelbuffer_check_guard(struct host1x_pixelbuffer *pixbuf);

void host1x_pixelbuffer_disable_bo_guard(void);

int host1x_gr2d_clear(struct tegra_stream *stream,
                      struct host1x_pixelbuffer *pixbuf,
                      uint32_t color);

int host1x_gr2d_clear_rect(struct tegra_stream *stream,
                           struct host1x_pixelbuffer *pixbuf,
                           uint32_t color,
                           unsigned x, unsigned y,
                           unsigned width, unsigned height);

int host1x_gr2d_clear_rect_clipped(struct tegra_stream *stream,
                                   struct host1x_pixelbuffer *pixbuf,
                                   uint32_t color,
                                   unsigned x, unsigned y,
                                   unsigned width, unsigned height,
                                   unsigned clip_x0, unsigned clip_y0,
                                   unsigned clip_x1, unsigned clip_y1,
                                   bool draw_outside);

enum host1x_2d_rotate {
    FLIP_X,
    FLIP_Y,
    TRANS_LR,
    TRANS_RL,
    ROT_90,
    ROT_180,
    ROT_270,
    IDENTITY,
};

int host1x_gr2d_blit(struct tegra_stream *stream,
                     struct host1x_pixelbuffer *src,
                     struct host1x_pixelbuffer *dst,
                     enum host1x_2d_rotate rotate,
                     unsigned int sx, unsigned int sy,
                     unsigned int dx, unsigned int dy,
                     unsigned int width, int height);

int host1x_gr2d_surface_blit(struct tegra_stream *stream,
                             struct host1x_pixelbuffer *src,
                             struct host1x_pixelbuffer *dst,
                             struct host1x_csc_params *csc,
                             unsigned int sx, unsigned int sy,
                             unsigned int src_width, unsigned int src_height,
                             unsigned int dx, unsigned int dy,
                             unsigned int dst_width, int dst_height);
#endif
