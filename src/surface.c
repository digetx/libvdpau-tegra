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

static uint32_t get_unused_surface_id(tegra_device *dev)
{
    uint32_t ret_id, id;

    for (id = 0; id < MAX_SURFACES_NB; id++) {
        ret_id = dev->surf_id_itr++ % MAX_SURFACES_NB;

        if (__get_surface(ret_id) == NULL) {
            break;
        }
    }

    if (id == MAX_SURFACES_NB) {
        return VDP_INVALID_HANDLE;
    }

    return ret_id;
}

int dynamic_alloc_surface_data(tegra_surface *surf)
{
    int ret = 0;

    pthread_mutex_lock(&surf->lock);
    if (!surf->data_allocated) {
        DebugMsg("surface %u %p\n", surf->surface_id, surf);

        ret = alloc_surface_data(surf);
        if (ret)
            ErrorMsg("surface %u %p failed width %u height %u %d (%s)\n",
                     surf->surface_id, surf, surf->width, surf->height,
                     ret, strerror(-ret));
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

int map_surface_data(tegra_surface *surf)
{
    void *data = NULL;
    int err;

    pthread_mutex_lock(&surf->lock);

    if (surf->map_cnt++) {
        goto out_unlock;
    }

    if (surf->flags & SURFACE_VIDEO) {
        if (!surf->pixbuf) {
            err = -EINVAL;
            goto err_cleanup;
        }

        if (!surf->y_data) {
            err = drm_tegra_bo_map(surf->y_bo, &surf->y_data);

            if (err < 0) {
                goto err_cleanup;
            }

            surf->y_data += surf->pixbuf->bos_offset[0];
        }

        if (!surf->cb_data) {
            err = drm_tegra_bo_map(surf->cb_bo, &surf->cb_data);

            if (err < 0) {
                goto err_cleanup;
            }

            surf->cb_data += surf->pixbuf->bos_offset[1];
        }

        if (!surf->cr_data) {
            err = drm_tegra_bo_map(surf->cr_bo, &surf->cr_data);

            if (err < 0) {
                goto err_cleanup;
            }

            surf->cr_data += surf->pixbuf->bos_offset[2];
        }
    } else {
        if (!surf->pix) {
            err = drm_tegra_bo_map(surf->bo, &data);

            if (err < 0) {
                goto err_cleanup;
            }

            surf->pix = pixman_image_create_bits_no_clear(surf->pfmt,
                                                          surf->pixbuf->width,
                                                          surf->pixbuf->height,
                                                          data,
                                                          surf->pixbuf->pitch);

            if (surf->pix == NULL) {
                err = -ENOMEM;
                goto err_cleanup;
            }
        }
    }

out_unlock:
    pthread_mutex_unlock(&surf->lock);

    return 0;

err_cleanup:
    if (surf->flags & SURFACE_VIDEO) {
        if (surf->y_data) {
            drm_tegra_bo_unmap(surf->y_bo);
        }

        if (surf->cb_data) {
            drm_tegra_bo_unmap(surf->cb_bo);
        }

        if (surf->cr_data) {
            drm_tegra_bo_unmap(surf->cr_bo);
        }

        surf->y_data = NULL;
        surf->cb_data = NULL;
        surf->cr_data = NULL;
    } else {
        if (data) {
            drm_tegra_bo_unmap(surf->bo);
        }

        if (surf->pix) {
            pixman_image_unref(surf->pix);
            surf->pix = NULL;
        }
    }

    surf->map_cnt = 0;

    pthread_mutex_unlock(&surf->lock);

    ErrorMsg("surface %u mapping failed %d (%s)\n",
             surf->surface_id, err, strerror(-err));

    return err;
}

void unmap_surface_data(tegra_surface *surf)
{
    pthread_mutex_lock(&surf->lock);

    if (--surf->map_cnt == 0) {
        if (surf->flags & SURFACE_VIDEO) {
            if (surf->y_data) {
                drm_tegra_bo_unmap(surf->y_bo);
            }

            if (surf->cb_data) {
                drm_tegra_bo_unmap(surf->cb_bo);
            }

            if (surf->cr_data) {
                drm_tegra_bo_unmap(surf->cr_bo);
            }

            surf->y_data = NULL;
            surf->cb_data = NULL;
            surf->cr_data = NULL;
        } else {
            if (surf->pix) {
                drm_tegra_bo_unmap(surf->bo);
                pixman_image_unref(surf->pix);

                surf->pix = NULL;
            }
        }
    }

    pthread_mutex_unlock(&surf->lock);
}

static int __alloc_surface_data(tegra_surface *surf)
{
    tegra_device *dev                   = surf->dev;
    uint32_t width                      = surf->width;
    uint32_t height                     = surf->height;
    VdpRGBAFormat rgba_format           = surf->rgba_format;
    int output                          = surf->flags & SURFACE_OUTPUT;
    int video                           = surf->flags & SURFACE_VIDEO;
    XvImage *xv_img                     = NULL;
    struct tegra_vde_h264_frame *frame  = NULL;
    struct host1x_pixelbuffer *pixbuf   = NULL;
    uint32_t *bo_flinks                 = NULL;
    uint32_t *pitches                   = NULL;
    uint32_t bo_flags                   = 0;
    uint32_t size;
    int drm_ver;
    int ret;

    if (!video) {
        enum pixel_format pixbuf_fmt;
        unsigned int stride = width * 4;
        unsigned int alignment = 64;

        switch (rgba_format) {
        case VDP_RGBA_FORMAT_R8G8B8A8:
            pixbuf_fmt = PIX_BUF_FMT_ABGR8888;
            surf->pfmt = PIXMAN_a8b8g8r8;
            break;

        case VDP_RGBA_FORMAT_B8G8R8A8:
            pixbuf_fmt = PIX_BUF_FMT_ARGB8888;
            surf->pfmt = PIXMAN_a8r8g8b8;
            break;

        default:
            ret = -EINVAL;
            goto err_cleanup;
        }

        /* GR3D texture sampler has specific alignment restrictions. */
        if (IS_POW2(width) && IS_POW2(height)) {
            alignment = 16;
        }

        pixbuf = host1x_pixelbuffer_create(dev->drm, width, height,
                                           ALIGN(stride, alignment), 0,
                                           pixbuf_fmt,
                                           PIX_BUF_LAYOUT_LINEAR);
        if (pixbuf == NULL) {
            ret = -ENOMEM;
            goto err_cleanup;
        }

        surf->bo = pixbuf->bo;
    }

    if (video) {
        unsigned int luma_stride, chroma_stride;

        frame = surf->frame;

        frame->y_fd = -1;
        frame->cb_fd = -1;
        frame->cr_fd = -1;
        frame->aux_fd = -1;

        if (dev->v4l2.presents) {
            struct v4l2_format format;

            ret = v4l2_try_format(dev->v4l2.video_fd,
                                  V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                                  width, height, V4L2_PIX_FMT_YUV420M,
                                  &format);
            if (ret)
                goto err_cleanup;

            if (format.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_YUV420M) {
                ErrorMsg("unexpected pixelformat %u\n",
                         format.fmt.pix_mp.pixelformat);
                ret = -EINVAL;
                goto err_cleanup;
            }

            luma_stride   = format.fmt.pix_mp.plane_fmt[0].bytesperline;
            chroma_stride = format.fmt.pix_mp.plane_fmt[1].bytesperline;
        } else {
            luma_stride   = ALIGN(width, 16);
            chroma_stride = ALIGN(width, 32) / 2;
        }

        DebugMsg("luma_stride %u chroma_stride %u\n",
                 luma_stride, chroma_stride);

        pixbuf = host1x_pixelbuffer_create(dev->drm, width, height,
                                           luma_stride,
                                           chroma_stride,
                                           PIX_BUF_FMT_YV12,
                                           PIX_BUF_LAYOUT_LINEAR);
        if (pixbuf == NULL) {
            ret = -ENOMEM;
            goto err_cleanup;
        }

        drm_ver = drm_tegra_version(dev->drm);

        if (drm_ver >= GRATE_KERNEL_DRM_VERSION)
            bo_flags |= DRM_TEGRA_GEM_CREATE_DONT_KMAP;

        surf->y_bo  = pixbuf->bos[0];
        surf->cb_bo = pixbuf->bos[1];
        surf->cr_bo = pixbuf->bos[2];

        /* luma plane */

        ret = drm_tegra_bo_to_dmabuf(surf->y_bo, (uint32_t *) &frame->y_fd);

        if (ret) {
            ErrorMsg("drm_tegra_bo_to_dmabuf failed %d (%s)\n",
                     ret, strerror(-ret));
            goto err_cleanup;
        }

        frame->y_offset = pixbuf->bos_offset[0];

        /* blue plane */

        ret = drm_tegra_bo_to_dmabuf(surf->cb_bo, (uint32_t *) &frame->cb_fd);

        if (ret) {
            ErrorMsg("drm_tegra_bo_to_dmabuf failed %d (%s)\n",
                     ret, strerror(-ret));
            goto err_cleanup;
        }

        frame->cb_offset = pixbuf->bos_offset[1];

        /* red plane */

        ret = drm_tegra_bo_to_dmabuf(surf->cr_bo, (uint32_t *) &frame->cr_fd);

        if (ret) {
            ErrorMsg("drm_tegra_bo_to_dmabuf failed %d (%s)\n",
                     ret, strerror(-ret));
            goto err_cleanup;
        }

        frame->cr_offset = pixbuf->bos_offset[2];

        /* aux stuff */

        /* kernel V4L driver allocates aux buffer internally */
        if (dev->v4l2.presents)
            goto done_aux;

        size = ALIGN(width, 32) * ALIGN(height, 16) / 4;

        ret = drm_tegra_bo_new(&surf->aux_bo, dev->drm, bo_flags, ALIGN(size, 256));
        if (ret) {
            ErrorMsg("drm_tegra_bo_new failed %d (%s)\n",
                     ret, strerror(-ret));
            goto err_cleanup;
        }

        ret = drm_tegra_bo_to_dmabuf(surf->aux_bo, (uint32_t *) &frame->aux_fd);

        if (ret) {
            ErrorMsg("drm_tegra_bo_to_dmabuf failed %d (%s)\n",
                     ret, strerror(-ret));
            goto err_cleanup;
        }

done_aux:
    }

    if (output && !tegra_vdpau_force_dri) {
        int format_id = -1;

        switch (rgba_format) {
        case VDP_RGBA_FORMAT_R8G8B8A8:
            format_id = dev->xv_v2 ? FOURCC_PASSTHROUGH_XBGR8888_V2 :
                                     FOURCC_PASSTHROUGH_XBGR8888;
            break;

        case VDP_RGBA_FORMAT_B8G8R8A8:
            format_id = dev->xv_v2 ? FOURCC_PASSTHROUGH_XRGB8888_V2 :
                                     FOURCC_PASSTHROUGH_XRGB8888;
            break;

        default:
            ret = -EINVAL;
            goto err_cleanup;
        }

        if (!dev->xv_ready) {
            ret = -ENOMEM;
            goto err_cleanup;
        }

        xv_img = XvCreateImage(dev->display, dev->xv_port,
                               format_id, NULL, width, height);
        if (xv_img == NULL) {
            ErrorMsg("XvCreateImage failed\n");
            ret = -ENOMEM;
            goto err_cleanup;
        }

        if (dev->xv_v2) {
            assert(xv_img->data_size == PASSTHROUGH_DATA_SIZE_V2);
        } else {
            assert(xv_img->data_size == PASSTHROUGH_DATA_SIZE);
        }

        xv_img->data = calloc(1, xv_img->data_size);

        if (xv_img->data == NULL) {
            ret = -ENOMEM;
            goto err_cleanup;
        }

        bo_flinks = (uint32_t *) xv_img->data;

        ret = drm_tegra_bo_get_name(pixbuf->bo, &bo_flinks[0]);
        if (ret) {
            ErrorMsg("drm_tegra_bo_get_name failed %d (%s)\n",
                     ret, strerror(-ret));
            goto err_cleanup;
        }

        if (dev->xv_v2) {
            pitches = (uint32_t *) (xv_img->data + 16);
        } else {
            pitches = (uint32_t *) (xv_img->data + 12);
        }

        pitches[0] = pixbuf->pitch;
    }

    surf->xv_img = xv_img;
    surf->pixbuf = pixbuf;
    surf->v4l2.buf_idx = -1;
    surf->data_allocated = true;

    return 0;

err_cleanup:
    if (xv_img != NULL) {
        free(xv_img->data);
        XFree(xv_img);
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

int alloc_surface_data(tegra_surface *surf)
{
    if (__alloc_surface_data(surf)) {
        tegra_surface_drop_caches();
        return __alloc_surface_data(surf);
    }

    return 0;
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
        surf->aux_bo = NULL;

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

        surf->frame = NULL;
    }

    surf->data_allocated = false;

    return 0;
}

tegra_surface *alloc_surface(tegra_device *dev,
                             uint32_t width, uint32_t height,
                             VdpRGBAFormat rgba_format,
                             int output, int video)
{
    struct tegra_vde_h264_frame *frame = NULL;
    pthread_mutexattr_t mutex_attrs;
    tegra_surface *surf;
    int ret;

    surf = tegra_surface_cache_take_surface(dev, width, height,
                                            rgba_format, output, video);
    if (surf) {
        surf->destroyed = false;
        return surf;
    }

    surf = calloc(1, sizeof(tegra_surface));
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
    surf->surface_id = MAX_SURFACES_NB;

    if (!output) {
        ret = alloc_surface_data(surf);
        if (ret != 0) {
            goto err_cleanup;
        }
    }

    tegra_stream_create(&surf->stream_3d, dev, dev->gr3d);
    tegra_stream_create(&surf->stream_2d, dev, dev->gr2d);
    ref_device(dev);

    DebugMsg("surface %p output %d video %d\n", surf, output, video);

    return surf;

err_cleanup:
    free(frame);
    free(surf);

    ErrorMsg("failed to allocate surface %d %s\n", ret, strerror(-ret));

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

    surface_id = get_unused_surface_id(dev);

    if (surface_id != VDP_INVALID_HANDLE) {
        set_surface(surface_id, surf);
    }

    pthread_mutex_unlock(&global_lock);

    if (surface_id != VDP_INVALID_HANDLE) {
        surf->surface_id = surface_id;
        DebugMsg("surface %u %p output %d video %d\n",
                 surface_id, surf, output, video);
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
    tegra_stream_destroy(surf->stream_3d);
    tegra_stream_destroy(surf->stream_2d);
    unref_device(surf->dev);

    set_surface(surf->surface_id, NULL);
    free(surf->frame);
    free(surf);

    return VDP_STATUS_OK;
}

VdpStatus destroy_surface(tegra_surface *surf)
{
    DebugMsg("surface %u %p\n", surf->surface_id, surf);

    tegra_surface_cache_surface_update_last_use(surf);

    pthread_mutex_lock(&surf->lock);
    if (surf->flags & SURFACE_OUTPUT) {
        shared_surface_kill_disp(surf);
    }
    surf->earliest_presentation_time = 0;
    surf->destroyed = true;
    pthread_mutex_unlock(&surf->lock);

    unref_surface(surf);

    return VDP_STATUS_OK;
}
