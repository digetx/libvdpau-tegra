/*
 * Copyright (c) GRATE-DRIVER project
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

#ifndef HOST1X_API_H
#define HOST1X_API_H

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
    union {
        uint32_t bo_offset;
        uint32_t bos_offset[3];
    };
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
                             unsigned int src_width, int src_height,
                             unsigned int dx, unsigned int dy,
                             unsigned int dst_width, int dst_height);

#define FX10(f)	            (((int32_t)((f) * 256.0f + 0.5f)) & 0x3ff)
#define FX10_L(f)           FX10(f)
#define FX10_H(f)           (FX10(f) << 10)
#define FX10x2(low, high)   (FX10_H(high) | FX10_L(low))

#define TGR3D_VAL(reg_name, field_name, value)                              \
    (((value) << TGR3D_ ## reg_name ## _ ## field_name ## __SHIFT) &        \
                 TGR3D_ ## reg_name ## _ ## field_name ## __MASK)

#define TGR3D_BOOL(reg_name, field_name, boolean)                           \
    ((boolean) ? TGR3D_ ## reg_name ## _ ## field_name : 0)

#define LOG2_SIZE(v)    (31 - __builtin_clz(v))
#define IS_POW2(v)      (((v) & ((v) - 1)) == 0)

void host1x_gr3d_upload_const_vp(struct tegra_stream *cmds, unsigned index,
                                 float x, float y, float z, float w);

void host1x_gr3d_upload_const_fp(struct tegra_stream *cmds, unsigned index,
                                 uint32_t constant);

void host1x_gr3d_setup_scissor(struct tegra_stream *cmds,
                               unsigned scissor_x,
                               unsigned scissor_y,
                               unsigned scissor_width,
                               unsigned scissor_heigth);

void host1x_gr3d_setup_viewport_bias_scale(struct tegra_stream *cmds,
                                           float viewport_x_bias,
                                           float viewport_y_bias,
                                           float viewport_z_bias,
                                           float viewport_x_scale,
                                           float viewport_y_scale,
                                           float viewport_z_scale);

void host1x_gr3d_setup_attribute(struct tegra_stream *cmds,
                                 unsigned index,
                                 struct drm_tegra_bo *bo,
                                 unsigned offset, unsigned type,
                                 unsigned size, unsigned stride);

void host1x_gr3d_setup_render_target(struct tegra_stream *cmds,
                                     unsigned index,
                                     struct drm_tegra_bo *bo,
                                     unsigned offset,
                                     unsigned pixel_format,
                                     unsigned pitch);

void host1x_gr3d_enable_render_targets(struct tegra_stream *cmds, uint32_t mask);

void host1x_gr3d_setup_texture_desc(struct tegra_stream *cmds,
                                    unsigned index,
                                    struct drm_tegra_bo *bo,
                                    unsigned offset,
                                    unsigned width,
                                    unsigned height,
                                    unsigned pixel_format,
                                    bool min_filter_linear,
                                    bool mip_filter_linear,
                                    bool mag_filter_linear,
                                    bool clamp_to_edge,
                                    bool mirrored_repeat);

void host1x_gr3d_setup_draw_params(struct tegra_stream *cmds,
                                   unsigned primitive_type,
                                   unsigned index_mode,
                                   unsigned first_vtx);

void host1x_gr3d_draw_primitives(struct tegra_stream *cmds,
                                 unsigned first_index, unsigned count);

void host1x_gr3d_initialize(struct tegra_stream *cmds,
                            const struct shader_program *prog);

#endif
