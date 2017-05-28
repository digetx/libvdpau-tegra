/*
 * NVIDIA TEGRA 2 VDPAU backend driver
 *
 * Copyright (c) 2016 Dmitry Osipenko <digetx@gmail.com>
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

static uint32_t get_unused_surface_id(void)
{
    uint32_t id;

    for (id = 0; id < MAX_SURFACES_NB; id++) {
        if (get_surface(id) == NULL) {
            break;
        }
    }

    if (id == MAX_SURFACES_NB) {
        return VDP_INVALID_HANDLE;
    }

    return id;
}

static struct drm_tegra_bo *alloc_plane(struct drm_tegra *drm, void **map,
                                        int *dmabuf_fd, int size)
{
    struct drm_tegra_bo *bo;
    uint32_t fd;
    int ret;

    ret = drm_tegra_bo_new(&bo, drm, 0, size);

    if (ret < 0) {
        return NULL;
    }

    if (map) {
        ret = drm_tegra_bo_map(bo, map);

        if (ret < 0) {
            drm_tegra_bo_unref(bo);
            return NULL;
        }
    }

    ret = drm_tegra_bo_to_dmabuf(bo, &fd);

    if (ret < 0) {
        drm_tegra_bo_unref(bo);
        return NULL;
    }

    *dmabuf_fd = fd;

    return bo;
}

uint32_t create_surface(tegra_device *dev,
                        uint32_t width,
                        uint32_t height,
                        pixman_format_code_t pfmt,
                        int output,
                        int video)
{
    tegra_surface *surf                 = calloc(1, sizeof(tegra_surface));
    pixman_image_t *pix                 = NULL;
    pixman_image_t *pix_disp            = NULL;
    XImage *img                         = NULL;
    struct tegra_vde_h264_frame *frame  = NULL;
    void *data                          = NULL;
    void *xrgb_data                     = NULL;
    uint32_t surface_id                 = VDP_INVALID_HANDLE;
    uint32_t stride                     = width * 4;

    if (surf == NULL) {
        return VDP_INVALID_HANDLE;
    }

    pthread_mutex_lock(&global_lock);

    surface_id = get_unused_surface_id();

    if (surface_id != VDP_INVALID_HANDLE) {
        set_surface(surface_id, surf);
    }

    pthread_mutex_unlock(&global_lock);

    if (surface_id == VDP_INVALID_HANDLE) {
        goto err_cleanup;
    }

    xrgb_data = data = malloc(stride * height);

    if (data == NULL) {
        goto err_cleanup;
    }

    pix = pixman_image_create_bits_no_clear(pfmt, width, height, data, stride);

    if (pix == NULL) {
        goto err_cleanup;
    }

    if (video) {
        frame = calloc(1, sizeof(struct tegra_vde_h264_frame));

        if (frame == NULL) {
            goto err_cleanup;
        }

        assert(dev != NULL);

        surf->y_bo = alloc_plane(dev->drm, &surf->y_data, &frame->y_fd,
                                 width * height);

        if (surf->y_bo == NULL) {
            goto err_cleanup;
        }

        surf->cb_bo = alloc_plane(dev->drm, &surf->cb_data, &frame->cb_fd,
                                  width * height / 4);

        if (surf->cb_bo == NULL) {
            goto err_cleanup;
        }

        surf->cr_bo = alloc_plane(dev->drm, &surf->cr_data, &frame->cr_fd,
                                  width * height / 4);

        if (surf->cr_bo == NULL) {
            goto err_cleanup;
        }

        surf->aux_bo = alloc_plane(dev->drm, NULL, &frame->aux_fd,
                                   width * height / 4);

        if (surf->aux_bo == NULL) {
            goto err_cleanup;
        }

        frame->flags = FLAG_IS_VALID;
    }

    if (output) {
        if (pfmt != PIXMAN_x8r8g8b8 && pfmt != PIXMAN_a8r8g8b8) {
            xrgb_data = malloc(stride * height);

            pix_disp = pixman_image_create_bits_no_clear(
                            PIXMAN_x8r8g8b8, width, height, xrgb_data, stride);

            assert(pix_disp != NULL);

            if (pix_disp == NULL) {
                goto err_cleanup;
            }
        }

        img = XCreateImage(dev->display, NULL, 24, ZPixmap, 0,
                           xrgb_data,
                           width, height,
                           8,
                           stride);

        assert(img != NULL);

        if (img == NULL) {
            goto err_cleanup;
        }
    }

    surf->rawdata = data;
    surf->pix = pix;
    surf->pix_disp = pix_disp;
    surf->img = img;
    surf->frame = frame;
    surf->flags = video ? SURFACE_VIDEO : 0;

    return surface_id;

err_cleanup:
    set_surface(surface_id, NULL);

    if (pix_disp != NULL && pix_disp != pix) {
        pixman_image_unref(pix_disp);
    }

    if (pix != NULL) {
        pixman_image_unref(pix);
    }

    if (frame != NULL) {
        drm_tegra_bo_unref(surf->y_bo);
        drm_tegra_bo_unref(surf->cb_bo);
        drm_tegra_bo_unref(surf->cr_bo);
        drm_tegra_bo_unref(surf->aux_bo);
    }

    if (xrgb_data != data) {
        free(xrgb_data);
    }

    free(frame);
    free(data);
    free(surf);

    return VDP_INVALID_HANDLE;
}

VdpStatus destroy_surface(tegra_surface *surf)
{
    pixman_bool_t ret;

    if (surf->frame != NULL) {
        drm_tegra_bo_unref(surf->y_bo);
        drm_tegra_bo_unref(surf->cb_bo);
        drm_tegra_bo_unref(surf->cr_bo);
        drm_tegra_bo_unref(surf->aux_bo);
    }

    ret = pixman_image_unref(surf->pix);

    assert(ret != 0);

    if (surf->img != NULL) {
        XDestroyImage(surf->img);
    }

    if (surf->pix_disp != NULL) {
        ret = pixman_image_unref(surf->pix_disp);
        assert(ret != 0);
    }

    if (surf->pix_disp != NULL) {
        free(surf->rawdata);
    }

    free(surf->frame);
    free(surf);

    return VDP_STATUS_OK;
}

static void convert_yv12_to_xrgb(uint8_t *restrict src_y_,
                                 uint8_t *restrict src_cb,
                                 uint8_t *restrict src_cr,
                                 uint8_t *restrict dest_xrgb,
                                 int width, int height,
                                 VdpCSCMatrix cscmat)
{
    float red, green, blue;
    int y_, cb, cr;
    int p_y, p_c;
    int cx, cy;
    int x, y;

#define _X  3
#define _R  2
#define _G  1
#define _B  0

#define _MAX(x, y) (((x) > (y)) ? (x) : (y))
#define _MIN(x, y) (((x) < (y)) ? (x) : (y))
#define _CLAMP(c) (_MIN(255, _MAX(c, 0)))

    for (cy = 0, y = 0; y < height; y++) {
        cy = y / 2;

        for (x = 0; x < width; x++) {
            cx = x / 2;

            p_y = x + width * y;
            p_c = cx + width / 2 * cy;

            y_ = src_y_[p_y];
            cb = src_cb[p_c] - 128;
            cr = src_cr[p_c] - 128;

            red   = y_ + cscmat[0][1] * cb + cscmat[0][2] * cr;
            green = y_ + cscmat[1][1] * cb + cscmat[1][2] * cr;
            blue  = y_ + cscmat[2][1] * cb + cscmat[2][2] * cr;

            dest_xrgb[_R] = _CLAMP(red);
            dest_xrgb[_G] = _CLAMP(green);
            dest_xrgb[_B] = _CLAMP(blue);
            dest_xrgb[_X] = 255;

            dest_xrgb += 4;
        }
    }
}

int sync_video_frame_dmabufs(tegra_surface *surf, enum frame_sync type)
{
    int ret;

    switch (type) {
    case READ_START:
    case READ_END:
        if (!(surf->flags & SURFACE_DATA_NEEDS_SYNC)) {
            return 0;
        }
        break;
    case WRITE_START:
    case WRITE_END:
        break;
    }

    assert(surf->flags & SURFACE_VIDEO);

    switch (type) {
    case READ_START:
        ret = sync_dmabuf_read_start(surf->frame->y_fd);
        break;
    case READ_END:
        ret = sync_dmabuf_read_end(surf->frame->y_fd);
        break;
    case WRITE_START:
        ret = sync_dmabuf_write_start(surf->frame->y_fd);
        break;
    case WRITE_END:
        ret = sync_dmabuf_write_end(surf->frame->y_fd);
        break;
    }

    assert(ret == 0);

    if (ret) {
        return ret;
    }

    switch (type) {
    case READ_START:
        ret = sync_dmabuf_read_start(surf->frame->cb_fd);
        break;
    case READ_END:
        ret = sync_dmabuf_read_end(surf->frame->cb_fd);
        break;
    case WRITE_START:
        ret = sync_dmabuf_write_start(surf->frame->cb_fd);
        break;
    case WRITE_END:
        ret = sync_dmabuf_write_end(surf->frame->cb_fd);
        break;
    }

    assert(ret == 0);

    if (ret) {
        return ret;
    }

    switch (type) {
    case READ_START:
        ret = sync_dmabuf_read_start(surf->frame->cr_fd);
        break;
    case READ_END:
        ret = sync_dmabuf_read_end(surf->frame->cr_fd);
        break;
    case WRITE_START:
        ret = sync_dmabuf_write_start(surf->frame->cr_fd);
        break;
    case WRITE_END:
        ret = sync_dmabuf_write_end(surf->frame->cr_fd);
        break;
    }

    assert(ret == 0);

    if (ret) {
        return ret;
    }

    return 0;
}

int convert_video_surf(tegra_surface *surf, VdpCSCMatrix cscmat)
{
    pixman_image_t *pix = surf->pix;
    int ret;

    if (!(surf->flags & SURFACE_VIDEO)) {
        return 0;
    }

    if (!(surf->flags & SURFACE_YUV_UNCONVERTED)) {
        return 0;
    }

    ret = sync_video_frame_dmabufs(surf, READ_START);

    if (ret) {
        return ret;
    }

    convert_yv12_to_xrgb(surf->y_data, surf->cb_data, surf->cr_data,
                         (void *) pixman_image_get_data(pix),
                         pixman_image_get_width(pix),
                         pixman_image_get_height(pix),
                         cscmat);

    surf->flags &= ~SURFACE_YUV_UNCONVERTED;

    ret = sync_video_frame_dmabufs(surf, READ_END);

    if (ret) {
        return ret;
    }

    surf->flags &= ~SURFACE_DATA_NEEDS_SYNC;

    return 0;
}
