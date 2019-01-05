/*
 * NVIDIA TEGRA 2 VDPAU backend driver
 *
 * Copyright (c) 2019 Dmitry Osipenko <digetx@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "vdpau_tegra.h"

int rotate_surface_gr2d(tegra_surface *src_surf,
                        tegra_surface *dst_surf,
                        struct host1x_csc_params *csc,
                        enum host1x_2d_rotate rotate,
                        unsigned int sx, unsigned int sy,
                        unsigned int src_width, int src_height,
                        unsigned int dx, unsigned int dy,
                        unsigned int dst_width, int dst_height,
                        bool check_only)
{
    tegra_device *dev;
    struct tegra_stream *stream;
    struct host1x_pixelbuffer *src;
    struct host1x_pixelbuffer *dst;
    struct host1x_pixelbuffer *tmp2 = NULL;
    struct host1x_pixelbuffer *tmp = NULL;
    struct host1x_pixelbuffer *rot = NULL;
    unsigned pre_rot_width, pre_rot_height;
    unsigned rot_width, rot_height;
    unsigned tmp_width, tmp_height;
    bool downscale = true;
    bool twopass = false;
    unsigned x, y;
    int ret = 0;

    if (!src_surf || !dst_surf)
        return -EINVAL;

    pthread_mutex_lock(&src_surf->lock);
    pthread_mutex_lock(&dst_surf->lock);

    if (!(src_surf->flags & SURFACE_VIDEO)) {
        ret = -EINVAL;
        goto out_unlock;
    }

    if (!(dst_surf->flags & SURFACE_OUTPUT)) {
        ret = -EINVAL;
        goto out_unlock;
    }

    if (src_width  != src_surf->width ||
        src_height != src_surf->height) {
        ret = -EINVAL;
        goto out_unlock;
    }

    if (dst_surf->rgba_format != VDP_RGBA_FORMAT_R8G8B8A8 &&
        dst_surf->rgba_format != VDP_RGBA_FORMAT_B8G8R8A8) {
        ret = -EINVAL;
        goto out_unlock;
    }

    switch (rotate) {
    case ROT_270:
        pre_rot_width = dst_height;
        pre_rot_height = dst_width;
        break;

    case ROT_180:
        pre_rot_width = dst_width;
        pre_rot_height = dst_height;
        break;

    case ROT_90:
        pre_rot_width = dst_height;
        pre_rot_height = dst_width;
        break;

    default:
        ret = -EINVAL;
        goto out_unlock;
    }

    if (check_only)
        goto out_unlock;

    src    = src_surf->pixbuf;
    dst    = dst_surf->pixbuf;
    dev    = dst_surf->dev;
    stream = &dst_surf->stream_2d;

    if (pre_rot_width * pre_rot_height > src_width * src_height)
        downscale = false;

    if (downscale) {
        tmp_width = ALIGN(pre_rot_width, 4);
        tmp_height = ALIGN(pre_rot_height, 4);

        rot_width = ALIGN(dst_width, 4);
        rot_height = ALIGN(dst_height, 4);
    } else {
        tmp_width = ALIGN(src_width, 4);
        tmp_height = ALIGN(src_height, 4);

        if (rotate == ROT_180) {
            rot_width = tmp_width;
            rot_height = tmp_height;
        } else {
            rot_width = tmp_height;
            rot_height = tmp_width;
        }
    }

    tmp = host1x_pixelbuffer_create(dev->drm,
                                    tmp_width, tmp_height,
                                    tmp_width * 4, 0,
                                    dst->format,
                                    PIX_BUF_LAYOUT_LINEAR);
    if (!tmp)
        return -EINVAL;

    ret = host1x_gr2d_surface_blit(stream,
                                   src, tmp,
                                   csc, sx, sy,
                                   src_width,
                                   src_height,
                                   0, 0,
                                   tmp_width,
                                   tmp_height);
    if (ret)
        goto out_unref;

    if (!ALIGNED(dx, 4) ||
        !ALIGNED(dy, 4) ||
        tmp_width != pre_rot_width ||
        tmp_height != pre_rot_height)
    {
        tmp2 = host1x_pixelbuffer_create(dev->drm,
                                         rot_width, rot_height,
                                         rot_width * 4, 0,
                                         dst->format,
                                         PIX_BUF_LAYOUT_LINEAR);
        if (!tmp2) {
            ret = -ENOMEM;
            goto out_unref;
        }

        DebugMsg("two-pass rotation, downscale %u, %u:%u %u:%u %u:%u\n",
                 downscale, dx, dy, tmp_width, tmp_height,
                 pre_rot_width, pre_rot_height);

        twopass = true;

        rot = tmp2;
    } else {
        DebugMsg("direct rotation\n");

        rot = dst;
    }

    if (twopass) {
        x = 0;
        y = 0;
    } else {
        x = dx;
        y = dy;
    }

    ret = host1x_gr2d_blit(stream,
                           tmp, rot,
                           rotate,
                           0, 0,
                           x, y,
                           tmp_width, tmp_height);
    if (ret)
        goto out_unref;

    if (twopass)
        ret = host1x_gr2d_surface_blit(stream,
                                       rot, dst,
                                       &csc_rgb_default,
                                       0, 0,
                                       rot_width, rot_height,
                                       dx, dy, dst_width, dst_height);

out_unref:
    if (tmp)
        host1x_pixelbuffer_free(tmp);

    if (tmp2)
        host1x_pixelbuffer_free(tmp2);

out_unlock:
    pthread_mutex_unlock(&dst_surf->lock);
    pthread_mutex_unlock(&src_surf->lock);

    return ret;
}
