/*
 * Copyright (c) 2012, 2013 Erik Faye-Lund
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

#include "vdpau_tegra.h"

struct host1x_csc_params csc_rgb_default = {
    .cvr = 0x80, .cub = 0x80, .cyx = 0x80,
};

int host1x_gr2d_clear(struct tegra_stream *stream,
                      struct host1x_pixelbuffer *pixbuf,
                      uint32_t color)
{
    return host1x_gr2d_clear_rect(stream, pixbuf, color, 0, 0,
                                  pixbuf->width, pixbuf->height);
}

int host1x_gr2d_clear_rect(struct tegra_stream *stream,
                           struct host1x_pixelbuffer *pixbuf,
                           uint32_t color,
                           unsigned x, unsigned y,
                           unsigned width, unsigned height)
{
    unsigned tiled = 0;
    int err;

    if (!pixbuf)
        return -EINVAL;

    DebugMsg("pixbuf width %u height %u color 0x%08X x %u y %u width %u height %u\n",
             pixbuf->width, pixbuf->height, color, x, y, width, height);

    if (x + width > pixbuf->width)
        return -EINVAL;

    if (y + height > pixbuf->height)
        return -EINVAL;

    switch (pixbuf->layout) {
    case PIX_BUF_LAYOUT_TILED_16x16:
        tiled = 1;
    case PIX_BUF_LAYOUT_LINEAR:
        break;
    default:
        host1x_error("Invalid layout %u\n", pixbuf->layout);
        return -EINVAL;
    }

    err = tegra_stream_begin(stream);
    if (err < 0)
        return err;

    tegra_stream_push_setclass(stream, HOST1X_CLASS_GR2D);
    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x09, 9));
    tegra_stream_push(stream, 0x0000003a);
    tegra_stream_push(stream, 0x00000000);
    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x1e, 7));
    tegra_stream_push(stream, 0x00000000); /* controlsecond */
    tegra_stream_push(stream, /* controlmain */
                      (PIX_BUF_FORMAT_BYTES(pixbuf->format) >> 1) << 16 |
                      1 << 6 | /* srcsld */
                      1 << 2 /* turbofill */);
    tegra_stream_push(stream, 0x000000cc);
    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x2b, 9));
    tegra_stream_push_reloc(stream, pixbuf->bo, 0);
    tegra_stream_push(stream, pixbuf->pitch);
    tegra_stream_push(stream, HOST1X_OPCODE_NONINCR(0x35, 1));
    tegra_stream_push(stream, color);
    tegra_stream_push(stream, HOST1X_OPCODE_NONINCR(0x46, 1));
    tegra_stream_push(stream, tiled << 20); /* tilemode */
    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x38, 5));
    tegra_stream_push(stream, height << 16 | width);
    tegra_stream_push(stream, y << 16 | x);

    err = tegra_stream_end(stream);
    if (err < 0)
        return err;

    err = tegra_stream_flush(stream);
    if (err < 0)
        return err;

    host1x_pixelbuffer_check_guard(pixbuf);

    return 0;
}

int host1x_gr2d_clear_rect_clipped(struct tegra_stream *stream,
                                   struct host1x_pixelbuffer *pixbuf,
                                   uint32_t color,
                                   unsigned x, unsigned y,
                                   unsigned width, unsigned height,
                                   unsigned clip_x0, unsigned clip_y0,
                                   unsigned clip_x1, unsigned clip_y1,
                                   bool draw_outside)
{
    unsigned tiled = 0;
    int err;

    if (!pixbuf)
        return -EINVAL;

    DebugMsg("pixbuf width %u height %u color 0x%08X x %u y %u "
             "width %u height %u clip_x0 %u, clip_y0 %u clip_x1 %u clip_y1 %u"
             " draw_outside %d\n",
             pixbuf->width, pixbuf->height, color, x, y,
             width, height, clip_x0, clip_y0, clip_x1, clip_y1, draw_outside);

    if (x + width > pixbuf->width)
        return -EINVAL;

    if (y + height > pixbuf->height)
        return -EINVAL;

    if (clip_x0 > pixbuf->width)
        return -EINVAL;

    if (clip_y0 > pixbuf->height)
        return -EINVAL;

    if (clip_x1 > pixbuf->width)
        return -EINVAL;

    if (clip_y1 > pixbuf->height)
        return -EINVAL;

    if (draw_outside &&
            x == clip_x0 &&
                y == clip_y0 &&
                    x + width == clip_x1 &&
                        y + height == clip_y1)
        return 0;

    switch (pixbuf->layout) {
    case PIX_BUF_LAYOUT_TILED_16x16:
        tiled = 1;
    case PIX_BUF_LAYOUT_LINEAR:
        break;
    default:
        host1x_error("Invalid layout %u\n", pixbuf->layout);
        return -EINVAL;
    }

    err = tegra_stream_begin(stream);
    if (err < 0)
        return err;

    tegra_stream_push_setclass(stream, HOST1X_CLASS_GR2D);
    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x09, 9));
    tegra_stream_push(stream, 0x0000003a);
    tegra_stream_push(stream, 0x00000000);
    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x1e, 7));
    tegra_stream_push(stream, /* controlsecond */
                      (draw_outside ? 3 : 2) << 21 /* clip inside/outside */);
    tegra_stream_push(stream, /* controlmain */
                      (PIX_BUF_FORMAT_BYTES(pixbuf->format) >> 1) << 16 |
                      1 << 6 /* srcsld */);
    tegra_stream_push(stream, 0x000000cc);
    tegra_stream_push(stream, HOST1X_OPCODE_INCR(0x22, 2));
    tegra_stream_push(stream, clip_y0 << 16 | clip_x0);
    tegra_stream_push(stream, clip_y1 << 16 | clip_x1);
    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x2b, 9));
    tegra_stream_push_reloc(stream, pixbuf->bo, 0);
    tegra_stream_push(stream, pixbuf->pitch);
    tegra_stream_push(stream, HOST1X_OPCODE_NONINCR(0x35, 1));
    tegra_stream_push(stream, color);
    tegra_stream_push(stream, HOST1X_OPCODE_NONINCR(0x46, 1));
    tegra_stream_push(stream, tiled << 20); /* tilemode */
    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x38, 5));
    tegra_stream_push(stream, height << 16 | width);
    tegra_stream_push(stream, y << 16 | x);

    err = tegra_stream_end(stream);
    if (err < 0)
        return err;

    err = tegra_stream_flush(stream);
    if (err < 0)
        return err;

    host1x_pixelbuffer_check_guard(pixbuf);

    return 0;
}

int host1x_gr2d_blit(struct tegra_stream *stream,
                     struct host1x_pixelbuffer *src,
                     struct host1x_pixelbuffer *dst,
                     unsigned int sx, unsigned int sy,
                     unsigned int dx, unsigned int dy,
                     unsigned int width, int height)
{
    unsigned src_tiled = 0;
    unsigned dst_tiled = 0;
    unsigned yflip = 0;
    unsigned xdir = 0;
    unsigned ydir = 0;
    int err;

    if (!src)
        return -EINVAL;

    if (!dst)
        return -EINVAL;

    DebugMsg("pixbuf src width %u height %u format %u "
             "dst width %u height %u format %u "
             "sx %u sy %u dx %u dy %u width %u height %u\n",
             src->width, src->height, src->format,
             dst->width, dst->height, dst->format,
             sx, sy, dx, dy, width, height);


    if (PIX_BUF_FORMAT_BYTES(src->format) !=
        PIX_BUF_FORMAT_BYTES(dst->format))
    {
        host1x_error("Unequal bytes size\n");
        return -EINVAL;
    }

    switch (src->layout) {
    case PIX_BUF_LAYOUT_TILED_16x16:
        src_tiled = 1;
    case PIX_BUF_LAYOUT_LINEAR:
        break;
    default:
        host1x_error("Invalid src layout %u\n", src->layout);
        return -EINVAL;
    }

    switch (dst->layout) {
    case PIX_BUF_LAYOUT_TILED_16x16:
        dst_tiled = 1;
    case PIX_BUF_LAYOUT_LINEAR:
        break;
    default:
        host1x_error("Invalid dst layout %u\n", dst->layout);
        return -EINVAL;
    }

    if (height < 0) {
        yflip = 1;
        height = -height;
    }

    if (sx + width > src->width ||
        dx + width > dst->width ||
        sy + height > src->height ||
        dy + height > dst->height) {
        host1x_error("Coords out of range\n");
        return -EINVAL;
    }

    if (src != dst)
        goto yflip_setup;

    if (sx >= dx + width || sx + width <= dx)
        goto yflip_setup;

    if (sy >= dy + height || sy + height <= dy)
        goto yflip_setup;

    if (dx > sx) {
        xdir = 1;
        sx += width - 1;
        dx += width - 1;
    }

    if (dy > sy) {
        ydir = 1;
        sy += height - 1;
        dy += height - 1;
    }

yflip_setup:
    if (yflip && !ydir)
        dy += height - 1;

    err = tegra_stream_begin(stream);
    if (err < 0)
        return err;

    tegra_stream_push_setclass(stream, HOST1X_CLASS_GR2D);

    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x009, 9));
    tegra_stream_push(stream, 0x0000003a); /* trigger */
    tegra_stream_push(stream, 0x00000000); /* cmdsel */

    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x01e, 0x7));
    tegra_stream_push(stream, 0x00000000); /* controlsecond */
    /*
     * [20:20] source color depth (0: mono, 1: same)
     * [17:16] destination color depth (0: 8 bpp, 1: 16 bpp, 2: 32 bpp)
     */
    tegra_stream_push(stream, /* controlmain */
                      1 << 20 |
                      (PIX_BUF_FORMAT_BYTES(dst->format) >> 1) << 16 |
                      yflip << 14 | ydir << 10 | xdir << 9);
    tegra_stream_push(stream, 0x000000cc); /* ropfade */

    tegra_stream_push(stream, HOST1X_OPCODE_NONINCR(0x046, 1));
    /*
     * [20:20] destination write tile mode (0: linear, 1: tiled)
     * [ 0: 0] tile mode Y/RGB (0: linear, 1: tiled)
     */
    tegra_stream_push(stream, dst_tiled << 20 | src_tiled); /* tilemode */

    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x02b, 0xe149));
    tegra_stream_push_reloc(stream, dst->bo, 0); /* dstba */
    tegra_stream_push(stream, dst->pitch); /* dstst */
    tegra_stream_push_reloc(stream, src->bo, 0); /* srcba */
    tegra_stream_push(stream, src->pitch); /* srcst */
    tegra_stream_push(stream, height << 16 | width); /* dstsize */
    tegra_stream_push(stream, sy << 16 | sx); /* srcps */
    tegra_stream_push(stream, dy << 16 | dx); /* dstps */

    err = tegra_stream_end(stream);
    if (err < 0)
        return err;

    err = tegra_stream_flush(stream);
    if (err < 0)
        return err;

    host1x_pixelbuffer_check_guard(dst);

    return 0;
}

static uint32_t sb_offset(struct host1x_pixelbuffer *pixbuf,
                          uint32_t xpos, uint32_t ypos)
{
    uint32_t offset;
    uint32_t bytes_per_pixel = PIX_BUF_FORMAT_BYTES(pixbuf->format);
    uint32_t pixels_per_line = pixbuf->pitch / bytes_per_pixel;
    uint32_t xb;

    if (pixbuf->layout == PIX_BUF_LAYOUT_LINEAR) {
        offset = ypos * pixbuf->pitch;
        offset += xpos * bytes_per_pixel;
    } else {
        xb = xpos * bytes_per_pixel;
        offset = 16 * pixels_per_line * (ypos / 16);
        offset += 256 * (xb / 16);
        offset += 16 * (ypos % 16);
        offset += xb % 16;
    }

    return offset;
}

int host1x_gr2d_surface_blit(struct tegra_stream *stream,
                             struct host1x_pixelbuffer *src,
                             struct host1x_pixelbuffer *dst,
                             struct host1x_csc_params *csc,
                             unsigned int sx, unsigned int sy,
                             unsigned int src_width, unsigned int src_height,
                             unsigned int dx, unsigned int dy,
                             unsigned int dst_width, int dst_height)
{
    float inv_scale_x;
    float inv_scale_y;
    unsigned src_tiled = 0;
    unsigned dst_tiled = 0;
    unsigned yflip = 0;
    unsigned src_fmt;
    unsigned dst_fmt;
    unsigned hftype;
    unsigned vftype;
    unsigned vfen;
    int err;

    if (!src)
        return -EINVAL;

    if (!dst)
        return -EINVAL;

    DebugMsg("pixbuf src width %u height %u format %u "
             "dst width %u height %u format %u "
             "sx %u sy %u src_width %u src_height %u "
             "dx %u dy %u dst_width %u dst_height %u\n",
             src->width, src->height, src->format,
             dst->width, dst->height, dst->format,
             sx, sy, src_width, src_height,
             dx, dy, dst_width, dst_height);

    switch (src->layout) {
    case PIX_BUF_LAYOUT_TILED_16x16:
        src_tiled = 1;
    case PIX_BUF_LAYOUT_LINEAR:
        break;
    default:
        host1x_error("Invalid src layout %u\n", src->layout);
        return -EINVAL;
    }

    switch (dst->layout) {
    case PIX_BUF_LAYOUT_TILED_16x16:
        dst_tiled = 1;
    case PIX_BUF_LAYOUT_LINEAR:
        break;
    default:
        host1x_error("Invalid dst layout %u\n", dst->layout);
        return -EINVAL;
    }

    /*
     * GR2DSB doesn't support this format. Not sure that this is fine
     * to do, but scaled result looks correct.
     */
    if (src->format == dst->format &&
        src->format == PIX_BUF_FMT_RGBA8888) {
        src_fmt = 14;
        dst_fmt = 14;
        goto coords_check;
    }

    switch (src->format) {
    case PIX_BUF_FMT_ABGR8888:
        src_fmt = 14;
        break;
    case PIX_BUF_FMT_ARGB8888:
        src_fmt = 15;
        break;
    case PIX_BUF_FMT_YV12:
        src_fmt = 0;
        break;
    default:
        host1x_error("Invalid src format %u\n", src->format);
        return -EINVAL;
    }

    switch (dst->format) {
    case PIX_BUF_FMT_ABGR8888:
        dst_fmt = 14;
        break;
    case PIX_BUF_FMT_ARGB8888:
        dst_fmt = 15;
        break;
    default:
        host1x_error("Invalid dst format %u\n", dst->format);
        return -EINVAL;
    }

coords_check:
    if (dst_height < 0) {
        yflip = 1;
        dst_height = -dst_height;
    }

    if (sx + src_width > src->width ||
        dx + dst_width > dst->width ||
        sy + src_height > src->height ||
        dy + dst_height > dst->height) {
        host1x_error("Coords out of range\n");
        return -EINVAL;
    }

    inv_scale_x = (src_width) / (float)(dst_width);
    inv_scale_y = (src_height) / (float)(dst_height);

    if (inv_scale_y > 64.0f || inv_scale_y < 1.0f / 4096.0f) {
        host1x_error("Unsupported Y scale\n");
        return -EINVAL;
    }

    if (inv_scale_x > 64.0f || inv_scale_x < 1.0f / 4096.0f) {
        host1x_error("Unsupported X scale\n");
        return -EINVAL;
    }

    if (inv_scale_x == 1.0f)
        hftype = 7;
    else
        hftype = 0;

    if (inv_scale_y == 1.0f) {
        vftype = 0;
        vfen = 0;
    } else {
        vftype = 0;
        vfen = 1;
    }

    err = tegra_stream_begin(stream);
    if (err < 0)
        return err;

    tegra_stream_push_setclass(stream, HOST1X_CLASS_GR2D_SB);

    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x009, 0xF09));
    tegra_stream_push(stream, 0x00000038); /* trigger */
    tegra_stream_push(stream, 0x00000001); /* cmdsel */
    tegra_stream_push(stream, FLOAT_TO_FIXED_6_12(inv_scale_y)); /* vdda */
    tegra_stream_push(stream, FLOAT_TO_FIXED_0_8(sy)); /* vddaini */
    tegra_stream_push(stream, FLOAT_TO_FIXED_6_12(inv_scale_x)); /* hdda */
    tegra_stream_push(stream, FLOAT_TO_FIXED_0_8(sx)); /* hddainils */

    /* CSC RGB -> RGB coefficients */
    if (src->format != PIX_BUF_FMT_YV12) {
        tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x15, 0x787));

        tegra_stream_push(stream, csc->yos << 24 | csc->cvr << 12 | csc->cub); /* cscfirst */
        tegra_stream_push(stream, csc->cyx << 24 | csc->cur << 12 | csc->cug); /* cscsecond */
        tegra_stream_push(stream, csc->cvb << 16 | csc->cvg); /* cscthird */
    } else {
        tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x15, 0x7E7));

        tegra_stream_push(stream, csc->yos << 24 | csc->cvr << 12 | csc->cub); /* cscfirst */
        tegra_stream_push(stream, csc->cyx << 24 | csc->cur << 12 | csc->cug); /* cscsecond */
        tegra_stream_push(stream, csc->cvb << 16 | csc->cvg); /* cscthird */

        tegra_stream_push_reloc(stream, src->bos[1], src->bo_offset[1]); /* uba */
        tegra_stream_push_reloc(stream, src->bos[2], src->bo_offset[2]); /* vba */
    }

    tegra_stream_push(stream, dst_fmt << 8 | src_fmt); /* sbformat */
    tegra_stream_push(stream, /* controlsb */
                      hftype << 20 | vfen << 18 | vftype << 16 |
                      (3 << 8) /* uvst */ |
                      ((src->format == PIX_BUF_FMT_YV12) << 5) /* imode */);
    tegra_stream_push(stream, 0x00000000); /* controlsecond */
    /*
     * [20:20] source color depth (0: mono, 1: same)
     * [17:16] destination color depth (0: 8 bpp, 1: 16 bpp, 2: 32 bpp)
     */
    tegra_stream_push(stream, /* controlmain */
                      1 << 28 | 1 << 27 |
                      (PIX_BUF_FORMAT_BYTES(dst->format) >> 1) << 16 |
                      yflip << 14);

    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x044, 0x35));
    tegra_stream_push(stream, src->pitch_uv); /* uvstride */
    /*
     * [20:20] destination write tile mode (0: linear, 1: tiled)
     * [ 0: 0] tile mode Y/RGB (0: linear, 1: tiled)
     */
    tegra_stream_push(stream, dst_tiled << 20 | src_tiled); /* tilemode */
    tegra_stream_push_reloc(stream, src->bo, /* srcba_sb_surfbase */
                            sb_offset(src, sx, sy));
    tegra_stream_push_reloc(stream, dst->bo, /* dstba_sb_surfbase */
                            sb_offset(dst, dx, dy) +
                                yflip * dst->pitch * (dst_height - 1));

    tegra_stream_push(stream, HOST1X_OPCODE_MASK(0x02b, 0x3149));
    tegra_stream_push_reloc(stream, dst->bo, /* dstba */
                            sb_offset(dst, dx, dy) +
                                yflip * dst->pitch * (dst_height - 1));
    tegra_stream_push(stream, dst->pitch); /* dstst */
    tegra_stream_push_reloc(stream, src->bo, /* srcba */
                            sb_offset(src, sx, sy));
    tegra_stream_push(stream, src->pitch); /* srcst */
    tegra_stream_push(stream, (src_height - 1) << 16 | src_width); /* srcsize */
    tegra_stream_push(stream, (dst_height - 1) << 16 | dst_width); /* dstsize */

    err = tegra_stream_end(stream);
    if (err < 0)
        return err;

    err = tegra_stream_flush(stream);
    if (err < 0)
        return err;

    host1x_pixelbuffer_check_guard(dst);

    return 0;
}
