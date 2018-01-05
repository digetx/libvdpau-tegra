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
        if (__get_surface(id) == NULL) {
            break;
        }
    }

    if (id == MAX_SURFACES_NB) {
        return VDP_INVALID_HANDLE;
    }

    return id;
}

int dynamic_alloc_surface_data(tegra_surface *surf)
{
    int ret = 0;

    pthread_mutex_lock(&surf->lock);
    if (!surf->data_allocated) {
        DebugMsg("surface %u %p\n", surf->surface_id, surf);

        ret = alloc_surface_data(surf);
    } else {
        DebugMsg("surface %u %p.. already allocated\n",
                 surf->surface_id, surf);
    }
    pthread_mutex_unlock(&surf->lock);

    return ret;
}

int dynamic_release_surface_data(tegra_surface *surf)
{
    int ret = 0;

    pthread_mutex_lock(&surf->lock);
    if (surf->data_allocated) {
        DebugMsg("surface %u %p\n", surf->surface_id, surf);

        ret = release_surface_data(surf);
    } else {
        DebugMsg("surface %u %p.. already released\n",
                 surf->surface_id, surf);
    }
    surf->data_dirty = false;
    pthread_mutex_unlock(&surf->lock);

    return ret;
}

int alloc_surface_data(tegra_surface *surf)
{
    tegra_device *dev                   = surf->dev;
    uint32_t width                      = surf->width;
    uint32_t height                     = surf->height;
    VdpRGBAFormat rgba_format           = surf->rgba_format;
    int output                          = surf->flags & SURFACE_OUTPUT;
    int video                           = surf->flags & SURFACE_VIDEO;
    pixman_image_t *pix                 = NULL;
    XvImage *xv_img                     = NULL;
    struct tegra_vde_h264_frame *frame  = NULL;
    struct host1x_pixelbuffer *pixbuf   = NULL;
    uint32_t stride                     = ALIGN(width * 4, 16);
    uint32_t *bo_flinks                 = NULL;
    uint32_t *pitches                   = NULL;
    int ret;

    if (!video) {
        pixman_format_code_t pfmt;
        enum pixel_format pixbuf_fmt;
        void *data;

        switch (rgba_format) {
        case VDP_RGBA_FORMAT_R8G8B8A8:
            pixbuf_fmt = PIX_BUF_FMT_ABGR8888;
            pfmt = PIXMAN_a8b8g8r8;
            break;

        case VDP_RGBA_FORMAT_B8G8R8A8:
            pixbuf_fmt = PIX_BUF_FMT_ARGB8888;
            pfmt = PIXMAN_a8r8g8b8;
            break;

        default:
            ret = -EINVAL;
            goto err_cleanup;
        }

        pixbuf = host1x_pixelbuffer_create(dev->drm, width, height,
                                           stride, 0,
                                           pixbuf_fmt,
                                           PIX_BUF_LAYOUT_LINEAR);
        if (pixbuf == NULL) {
            ret = -ENOMEM;
            goto err_cleanup;
        }

        ret = drm_tegra_bo_map(pixbuf->bo, &data);

        if (ret < 0) {
            goto err_cleanup;
        }

        pix = pixman_image_create_bits_no_clear(pfmt, width, height,
                                                data, stride);

        if (pix == NULL) {
            ret = -ENOMEM;
            goto err_cleanup;
        }
    }

    if (video) {
        frame = surf->frame;

        frame->y_fd = -1;
        frame->cb_fd = -1;
        frame->cr_fd = -1;
        frame->aux_fd = -1;

        pixbuf = host1x_pixelbuffer_create(dev->drm,
                                           width, ALIGN(height, 16),
                                           ALIGN(width, 16),
                                           ALIGN(width / 2, 8),
                                           PIX_BUF_FMT_YV12,
                                           PIX_BUF_LAYOUT_LINEAR);
        if (pixbuf == NULL) {
            ret = -ENOMEM;
            goto err_cleanup;
        }

        surf->y_bo  = pixbuf->bos[0];
        surf->cb_bo = pixbuf->bos[1];
        surf->cr_bo = pixbuf->bos[2];

        /* luma plane */

        ret = drm_tegra_bo_to_dmabuf(surf->y_bo, (uint32_t *) &frame->y_fd);

        if (ret < 0) {
            goto err_cleanup;
        }

        ret = drm_tegra_bo_map(surf->y_bo, &surf->y_data);

        if (ret < 0) {
            goto err_cleanup;
        }

        /* blue plane */

        ret = drm_tegra_bo_to_dmabuf(surf->cb_bo, (uint32_t *) &frame->cb_fd);

        if (ret < 0) {
            goto err_cleanup;
        }

        ret = drm_tegra_bo_map(surf->cb_bo, &surf->cb_data);

        if (ret < 0) {
            goto err_cleanup;
        }

        surf->cb_data += pixbuf->bo_offset[1];
        frame->cb_offset = pixbuf->bo_offset[1];

        /* red plane */

        ret = drm_tegra_bo_to_dmabuf(surf->cr_bo, (uint32_t *) &frame->cr_fd);

        if (ret < 0) {
            goto err_cleanup;
        }

        ret = drm_tegra_bo_map(surf->cr_bo, &surf->cr_data);

        if (ret < 0) {
            goto err_cleanup;
        }

        surf->cr_data += pixbuf->bo_offset[2];
        frame->cr_offset = pixbuf->bo_offset[2];

        /* aux stuff */

        ret = drm_tegra_bo_new(&surf->aux_bo, dev->drm, 0,
                               ALIGN(width, 16) * ALIGN(height, 16) / 4);
        if (ret < 0) {
            goto err_cleanup;
        }

        ret = drm_tegra_bo_to_dmabuf(surf->aux_bo, (uint32_t *) &frame->aux_fd);

        if (ret < 0) {
            goto err_cleanup;
        }
    }

    if (output && !tegra_vdpau_force_dri) {
        int format_id = -1;

        switch (rgba_format) {
        case VDP_RGBA_FORMAT_R8G8B8A8:
            format_id = FOURCC_PASSTHROUGH_XBGR8888;
            break;

        case VDP_RGBA_FORMAT_B8G8R8A8:
            format_id = FOURCC_PASSTHROUGH_XRGB8888;
            break;

        default:
            ret = -EINVAL;
            goto err_cleanup;
        }

        xv_img = XvCreateImage(dev->display, dev->xv_port,
                               format_id, NULL, width, height);
        if (xv_img == NULL) {
            ErrorMsg("XvCreateImage failed\n");
            ret = -ENOMEM;
            goto err_cleanup;
        }

        assert(xv_img->data_size == PASSTHROUGH_DATA_SIZE);

        xv_img->data = calloc(1, xv_img->data_size);

        if (xv_img->data == NULL) {
            ret = -ENOMEM;
            goto err_cleanup;
        }

        bo_flinks = (uint32_t *) xv_img->data;

        ret = drm_tegra_bo_get_name(pixbuf->bo, &bo_flinks[0]);
        if (ret != 0) {
            ErrorMsg("drm_tegra_bo_get_name failed\n");
            goto err_cleanup;
        }

        pitches = (uint32_t *) (xv_img->data + 12);

        pitches[0] = pixbuf->pitch;
    }

    surf->pix = pix;
    surf->xv_img = xv_img;
    surf->pixbuf = pixbuf;
    surf->data_allocated = true;

    return 0;

err_cleanup:
    if (xv_img != NULL) {
        free(xv_img->data);
        XFree(xv_img);
    }

    if (pix != NULL) {
        pixman_image_unref(pix);
    }

    if (pixbuf!= NULL) {
        host1x_pixelbuffer_free(pixbuf);
    }

    if (frame != NULL) {
        drm_tegra_bo_unref(surf->aux_bo);
        close(frame->y_fd);
        close(frame->cb_fd);
        close(frame->cr_fd);
        close(frame->aux_fd);
    }

    return ret;
}

int release_surface_data(tegra_surface *surf)
{
    assert(surf->data_allocated);

    if (surf->pixbuf != NULL) {
        host1x_pixelbuffer_free(surf->pixbuf);
        surf->pixbuf = NULL;
    }

    if (surf->pix != NULL) {
        pixman_image_unref(surf->pix);
        surf->pix = NULL;
    }

    if (surf->xv_img != NULL) {
        free(surf->xv_img->data);
        XFree(surf->xv_img);
        surf->xv_img = NULL;
    }

    if (surf->frame != NULL) {
        drm_tegra_bo_unref(surf->aux_bo);
        close(surf->frame->y_fd);
        close(surf->frame->cb_fd);
        close(surf->frame->cr_fd);
        close(surf->frame->aux_fd);

        surf->frame->y_fd = -1;
        surf->frame->cb_fd = -1;
        surf->frame->cr_fd = -1;
        surf->frame->aux_fd = -1;

        surf->y_data = NULL;
        surf->cb_data = NULL;
        surf->cr_data = NULL;
    }

    surf->data_allocated = false;

    return 0;
}

tegra_surface *alloc_surface(tegra_device *dev,
                             uint32_t width, uint32_t height,
                             VdpRGBAFormat rgba_format,
                             int output, int video)
{
    tegra_surface *surf                 = calloc(1, sizeof(tegra_surface));
    struct tegra_vde_h264_frame *frame  = NULL;
    pthread_mutexattr_t mutex_attrs;
    int ret;

    if (!surf) {
        return NULL;
    }

    if (video) {
        frame = calloc(1, sizeof(struct tegra_vde_h264_frame));

        if (frame == NULL) {
            ret = -ENOMEM;
            goto err_cleanup;
        }
    }

    pthread_mutexattr_init(&mutex_attrs);
    pthread_mutexattr_settype(&mutex_attrs, PTHREAD_MUTEX_RECURSIVE);

    ret = pthread_mutex_init(&surf->lock, &mutex_attrs);
    if (ret != 0) {
        ErrorMsg("pthread_mutex_init failed\n");
        goto err_cleanup;
    }

    ret = pthread_cond_init(&surf->idle_cond, NULL);
    if (ret != 0) {
        ErrorMsg("pthread_cond_init failed\n");
        goto err_cleanup;
    }

    atomic_set(&surf->refcnt, 1);
    surf->status = VDP_PRESENTATION_QUEUE_STATUS_IDLE;
    surf->frame = frame;
    surf->flags = video ? SURFACE_VIDEO : 0;
    surf->flags |= output ? SURFACE_OUTPUT : 0;
    surf->dev = dev;
    surf->width = width;
    surf->height = height;
    surf->rgba_format = rgba_format;

    if (!output) {
        ret = alloc_surface_data(surf);
        if (ret != 0) {
            goto err_cleanup;
        }
    }

    ref_device(dev);

    return surf;

err_cleanup:
    free(frame);
    free(surf);

    ErrorMsg("failed to allocate surface %d %s\n", ret, strerror(ret));

    return NULL;
}

uint32_t create_surface(tegra_device *dev,
                        uint32_t width,
                        uint32_t height,
                        VdpRGBAFormat rgba_format,
                        int output,
                        int video)
{
    tegra_surface *surf;
    uint32_t surface_id;

    surf = alloc_surface(dev, width, height, rgba_format, output, video);

    if (surf == NULL) {
        return VDP_STATUS_RESOURCES;
    }

    pthread_mutex_lock(&global_lock);

    surface_id = get_unused_surface_id();

    if (surface_id != VDP_INVALID_HANDLE) {
        set_surface(surface_id, surf);
    }

    pthread_mutex_unlock(&global_lock);

    if (surface_id != VDP_INVALID_HANDLE) {
        surf->surface_id = surface_id;
        DebugMsg("surface %u %p\n", surface_id, surf);
    } else {
        destroy_surface(surf);
    }

    return surface_id;
}

void ref_surface(tegra_surface *surf)
{
    atomic_inc(&surf->refcnt);
}

VdpStatus unref_surface(tegra_surface *surf)
{
    if (!atomic_dec_and_test(&surf->refcnt)) {
        return VDP_STATUS_OK;
    }

    DebugMsg("surface %u %p\n", surf->surface_id, surf);

    dynamic_release_surface_data(surf);
    unref_device(surf->dev);

    set_surface(surf->surface_id, NULL);
    free(surf->frame);
    free(surf);

    return VDP_STATUS_OK;
}

VdpStatus destroy_surface(tegra_surface *surf)
{
    DebugMsg("surface %u %p\n", surf->surface_id, surf);

    pthread_mutex_lock(&surf->lock);
    if (surf->flags & SURFACE_OUTPUT) {
        shared_surface_kill_disp(surf);
    }
    surf->earliest_presentation_time = 0;
    pthread_mutex_unlock(&surf->lock);

    unref_surface(surf);

    return VDP_STATUS_OK;
}

int sync_video_frame_dmabufs(tegra_surface *surf, enum frame_sync type)
{
    int ret = 0;

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

    if (ret) {
        ErrorMsg("dmabuf sync %u failed %d\n", type, ret);
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

    if (ret) {
        ErrorMsg("dmabuf sync %u failed %d\n", type, ret);
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

    if (ret) {
        ErrorMsg("dmabuf sync %u failed %d\n", type, ret);
        return ret;
    }

    return 0;
}
