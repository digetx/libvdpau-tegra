 /*
 * NVIDIA TEGRA 2 VDPAU backend
 *
 * Copyright (c) 2017 Dmitry Osipenko <digetx@gmail.com>
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

static pthread_mutex_t shared_lock = PTHREAD_MUTEX_INITIALIZER;

static XvImage * create_video_xv(tegra_surface *video)
{
    XvImage *xv_img;
    uint32_t *bo_flinks;
    uint32_t *pitches;
    uint32_t *offsets;

    if (tegra_vdpau_force_dri) {
        return NULL;
    }

    if (!video->dev->xv_ready) {
        return NULL;
    }

    xv_img = XvCreateImage(video->dev->display, video->dev->xv_port,
                           FOURCC_PASSTHROUGH_YV12, NULL,
                           video->width, video->height);
    if (!xv_img) {
        return NULL;
    }

    xv_img->data = calloc(1, xv_img->data_size);

    if (!xv_img->data) {
        XFree(xv_img);
        return NULL;
    }

    bo_flinks = (uint32_t*) (xv_img->data + 0);
    pitches = (uint32_t*) (xv_img->data + 12);
    offsets = (uint32_t*) (xv_img->data + 24);

    drm_tegra_bo_get_name(video->y_bo, &bo_flinks[0]);
    drm_tegra_bo_get_name(video->cb_bo, &bo_flinks[1]);
    drm_tegra_bo_get_name(video->cr_bo, &bo_flinks[2]);

    pitches[0] = video->pixbuf->pitch;
    pitches[1] = video->pixbuf->pitch_uv;
    pitches[2] = video->pixbuf->pitch_uv;

    offsets[0] = video->pixbuf->bo_offset[0];
    offsets[1] = video->pixbuf->bo_offset[1];
    offsets[2] = video->pixbuf->bo_offset[2];

    return xv_img;
}

tegra_shared_surface *create_shared_surface(tegra_surface *disp,
                                            tegra_surface *video,
                                            struct host1x_csc_params *csc,
                                            uint32_t src_x0,
                                            uint32_t src_y0,
                                            uint32_t src_width,
                                            uint32_t src_height,
                                            uint32_t dst_x0,
                                            uint32_t dst_y0,
                                            uint32_t dst_width,
                                            uint32_t dst_height)
{
    tegra_shared_surface *shared;
    int ret;

    pthread_mutex_lock(&shared_lock);
    pthread_mutex_lock(&video->lock);
    pthread_mutex_lock(&disp->lock);

    if (disp->data_dirty || disp->shared || video->shared) {
        pthread_mutex_unlock(&disp->lock);
        pthread_mutex_unlock(&video->lock);
        pthread_mutex_unlock(&shared_lock);
        return NULL;
    }

    shared = calloc(1, sizeof(tegra_shared_surface));
    if (!shared) {
        pthread_mutex_unlock(&disp->lock);
        pthread_mutex_unlock(&video->lock);
        pthread_mutex_unlock(&shared_lock);
        return NULL;
    }

    atomic_set(&shared->refcnt, 1);
    memcpy(&shared->csc, csc, sizeof(*csc));

    shared->xv_img = create_video_xv(video);
    shared->video = video;
    shared->disp = disp;
    shared->src_x0 = src_x0;
    shared->src_y0 = src_y0;
    shared->src_width = src_width;
    shared->src_height = src_height;
    shared->dst_x0 = dst_x0;
    shared->dst_y0 = dst_y0;
    shared->dst_width = dst_width;
    shared->dst_height = dst_height;

    if (!shared->xv_img && !tegra_vdpau_force_dri) {
        free(shared);

        pthread_mutex_unlock(&disp->lock);
        pthread_mutex_unlock(&video->lock);
        pthread_mutex_unlock(&shared_lock);
        return NULL;
    }

    ret = dynamic_release_surface_data(disp);
    if (ret) {
        if (shared->xv_img) {
            free(shared->xv_img->data);
            XFree(shared->xv_img);
        }

        free(shared);

        pthread_mutex_unlock(&disp->lock);
        pthread_mutex_unlock(&video->lock);
        pthread_mutex_unlock(&shared_lock);
        return NULL;
    }

    ref_surface(disp);
    ref_surface(video);

    video->shared = shared;
    disp->shared = shared;

    pthread_mutex_unlock(&disp->lock);
    pthread_mutex_unlock(&video->lock);
    pthread_mutex_unlock(&shared_lock);

    DebugMsg("%p disp %u video %u\n",
             shared, disp->surface_id, video->surface_id);

    return shared;
}

void ref_shared_surface(tegra_shared_surface *shared)
{
    atomic_inc(&shared->refcnt);
}

void unref_shared_surface(tegra_shared_surface *shared)
{
    if (!atomic_dec_and_test(&shared->refcnt)) {
        return;
    }

    DebugMsg("%p disp %u video %u\n",
             shared,
             shared->disp->surface_id,
             shared->video->surface_id);

    unref_surface(shared->video);
    unref_surface(shared->disp);

    if (shared->xv_img) {
        free(shared->xv_img->data);
        XFree(shared->xv_img);
    }

    free(shared);
}

static void shared_surface_break_link_locked(tegra_shared_surface *shared)
{
    DebugMsg("%p disp %u video %u\n",
             shared,
             shared->disp->surface_id,
             shared->video->surface_id);

    shared->disp->shared = NULL;
    shared->video->shared = NULL;
}

tegra_surface * shared_surface_swap_video(tegra_surface *old)
{
    tegra_shared_surface *shared;
    tegra_surface *new;

    DebugMsg("surface %u\n", old->surface_id);

    assert(old->flags & SURFACE_VIDEO);

    pthread_mutex_lock(&shared_lock);
    shared = old->shared;
    if (shared) {
        ref_surface(old);

        DebugMsg("%p disp %u video %u\n",
                 shared,
                 shared->disp->surface_id,
                 shared->video->surface_id);
    }
    pthread_mutex_unlock(&shared_lock);

    if (!shared) {
        return old;
    }

    new = alloc_surface(old->dev, old->width, old->height, ~0, 0, 1);
    if (new) {
        pthread_mutex_lock(&global_lock);
        replace_surface(old, new);
        pthread_mutex_unlock(&global_lock);

        unref_surface(old);
    } else {
        new = old;
    }

    unref_surface(old);

    return new;
}

int shared_surface_transfer_video(tegra_surface *disp)
{
    tegra_shared_surface *shared;
    tegra_surface *video;
    int ret;

    DebugMsg("surface %u\n", disp->surface_id);

    pthread_mutex_lock(&disp->lock);

    pthread_mutex_lock(&shared_lock);
    shared = disp->shared;
    if (shared) {
        ref_shared_surface(shared);
        video = shared->video;
    }
    pthread_mutex_unlock(&shared_lock);

    ret = dynamic_alloc_surface_data(disp);
    if (ret) {
        if (shared) {
            goto unshare;
        }
        pthread_mutex_unlock(&disp->lock);
        return ret;
    }

    if (!shared) {
        pthread_mutex_unlock(&disp->lock);
        return 0;
    }

    assert(disp->flags & SURFACE_OUTPUT);

    DebugMsg("%p disp %u video %u\n",
             shared,
             shared->disp->surface_id,
             shared->video->surface_id);

    if (disp->set_bg) {
        ret = host1x_gr2d_clear_rect_clipped(&disp->stream_2d,
                                             disp->pixbuf,
                                             disp->bg_color,
                                             0, 0,
                                             disp->width,
                                             disp->height,
                                             shared->dst_x0,
                                             shared->dst_y0,
                                             shared->dst_x0 + shared->dst_width,
                                             shared->dst_y0 + shared->dst_height,
                                             true);
        if (ret) {
            ErrorMsg("setting BG failed %d\n", ret);
        }

        disp->set_bg = false;
    }

    ret = host1x_gr2d_surface_blit(&disp->stream_2d,
                                   video->pixbuf,
                                   disp->pixbuf,
                                   &shared->csc,
                                   shared->src_x0,
                                   shared->src_y0,
                                   shared->src_width,
                                   shared->src_height,
                                   shared->dst_x0,
                                   shared->dst_y0,
                                   shared->dst_width,
                                   shared->dst_height);
    if (ret) {
        ErrorMsg("video transfer failed %d\n", ret);
    }

unshare:
    pthread_mutex_lock(&shared_lock);
    shared_surface_break_link_locked(shared);
    pthread_mutex_unlock(&shared_lock);

    unref_shared_surface(shared);
    pthread_mutex_unlock(&disp->lock);

    unref_shared_surface(shared);

    return 0;
}

void shared_surface_kill_disp(tegra_surface *disp)
{
    tegra_shared_surface *shared = NULL;

    DebugMsg("surface %u\n", disp->surface_id);

    assert(disp->flags & SURFACE_OUTPUT);
    disp->data_dirty = false;

    pthread_mutex_lock(&disp->lock);
    pthread_mutex_lock(&shared_lock);

    shared = disp->shared;
    if (shared) {
        DebugMsg("%p disp %u video %u\n",
                shared,
                shared->disp->surface_id,
                shared->video->surface_id);

        shared_surface_break_link_locked(shared);
    }

    pthread_mutex_unlock(&shared_lock);
    pthread_mutex_unlock(&disp->lock);

    if (shared) {
        unref_shared_surface(shared);
    }
}

tegra_shared_surface * shared_surface_get(tegra_surface *disp)
{
    tegra_shared_surface *shared = NULL;

    DebugMsg("surface %u\n", disp->surface_id);

    pthread_mutex_lock(&disp->lock);
    pthread_mutex_lock(&shared_lock);

    shared = disp->shared;
    if (shared) {
        ref_shared_surface(shared);
    }

    pthread_mutex_unlock(&shared_lock);
    pthread_mutex_unlock(&disp->lock);

    return shared;
}
