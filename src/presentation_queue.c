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

static VdpTime get_time(void)
{
    struct timespec tp;

    if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
        abort();

    return (VdpTime)tp.tv_sec * 1000000000ULL + (VdpTime)tp.tv_nsec;
}

static void pqt_display_surface_to_idle_state(tegra_pqt *pqt)
{
    tegra_surface *surf = pqt->disp_surf;

    if (!surf) {
        return;
    }

    pthread_mutex_lock(&surf->lock);
    if (surf->status == VDP_PRESENTATION_QUEUE_STATUS_VISIBLE) {
        surf->status = VDP_PRESENTATION_QUEUE_STATUS_IDLE;
        pthread_cond_signal(&surf->idle_cond);
        pqt->disp_surf = NULL;
    }
    pthread_mutex_unlock(&surf->lock);

    unref_surface(surf);
}

static void pqt_display_surface(tegra_pqt *pqt, tegra_surface *surf)
{
    tegra_device *dev = pqt->dev;
    CARD64 count;

    if (!DRI_OUTPUT) {
        if (surf->set_bg && surf->bg_color != pqt->bg_color) {
            XSetWindowBackground(dev->display, pqt->drawable, surf->bg_color);
            XClearWindow(dev->display, pqt->drawable);

            pqt->bg_color = surf->bg_color;
        }

        if (surf->shared) {
            XvPutImage(dev->display, dev->xv_port,
                       pqt->drawable, pqt->gc,
                       surf->shared->xv_img,
                       surf->shared->src_x0,
                       surf->shared->src_y0,
                       surf->shared->src_width,
                       surf->shared->src_height,
                       surf->shared->dst_x0,
                       surf->shared->dst_y0,
                       surf->shared->dst_width,
                       surf->shared->dst_height);
        } else if (surf->xv_img) {
            XvPutImage(dev->display, dev->xv_port,
                       pqt->drawable, pqt->gc,
                       surf->xv_img,
                       0,
                       0,
                       surf->disp_width,
                       surf->disp_height,
                       0,
                       0,
                       surf->disp_width,
                       surf->disp_height);
        }

        XSync(dev->display, 0);
    } else if (pqt->dri_pixbuf) {
        DRI2SwapBuffers(dev->display, pqt->drawable, 0, 0, 0, &count);
    }

    pthread_mutex_lock(&surf->lock);
    surf->first_presentation_time = get_time();
    surf->status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;

    if (pqt->disp_surf != surf) {
        pqt_display_surface_to_idle_state(pqt);
        pqt->disp_surf = surf;
    }
    pthread_mutex_unlock(&surf->lock);
}

static void pqt_update_dri_buffer(tegra_pqt *pqt, tegra_surface *surf)
{
    if (!DRI_OUTPUT) {
        return;
    }

    if (surf->disp_width != pqt->dri_pixbuf->width ||
            surf->disp_height != pqt->dri_pixbuf->height) {
        pqt_update_dri_pixbuf(pqt);
    }

    if (!pqt->dri_pixbuf) {
        return;
    }

    pthread_mutex_lock(&surf->lock);
    pthread_mutex_lock(&pqt->dev->lock);

    if (surf->shared) {
        if (surf->set_bg) {
            host1x_gr2d_clear_rect_clipped(surf->dev->stream,
                                           pqt->dri_pixbuf,
                                           surf->bg_color,
                                           0,
                                           0,
                                           pqt->dri_pixbuf->width,
                                           pqt->dri_pixbuf->height,
                                           surf->shared->dst_x0,
                                           surf->shared->dst_y0,
                                           surf->shared->dst_x0 + surf->shared->dst_width,
                                           surf->shared->dst_y0 + surf->shared->dst_height,
                                           true);
        }

        host1x_gr2d_surface_blit(pqt->dev->stream,
                                 surf->shared->video->pixbuf,
                                 pqt->dri_pixbuf,
                                 &surf->shared->csc,
                                 surf->shared->src_x0,
                                 surf->shared->src_y0,
                                 surf->shared->src_width,
                                 surf->shared->src_height,
                                 surf->shared->dst_x0,
                                 surf->shared->dst_y0,
                                 surf->shared->dst_width,
                                 surf->shared->dst_height);
    } else if (surf->pixbuf) {
        host1x_gr2d_surface_blit(pqt->dev->stream,
                                 surf->pixbuf,
                                 pqt->dri_pixbuf,
                                 &csc_rgb_default,
                                 0,
                                 0,
                                 surf->disp_width,
                                 surf->disp_height,
                                 0,
                                 0,
                                 pqt->dri_pixbuf->width,
                                 pqt->dri_pixbuf->height);
    }

    pthread_mutex_unlock(&pqt->dev->lock);
    pthread_mutex_unlock(&surf->lock);
}

static void * presentation_queue_thr(void *opaque)
{
    tegra_pq *pq = opaque;
    tegra_pqt *pqt = pq->pqt;
    tegra_surface *surf, *tmp;
    VdpTime earliest_time = UINT64_MAX;
    VdpTime time = UINT64_MAX;
    struct timespec tp;
    int ret;

    pthread_mutex_lock(&pq->lock);

    while (true) {
        if (time == UINT64_MAX) {
            clock_gettime(CLOCK_MONOTONIC, &tp);
            tp.tv_sec += 9999999;
        } else {
            memset(&tp, 0, sizeof(tp));
            tp.tv_sec = time / 1000000000ULL;
            tp.tv_nsec = time - tp.tv_sec * 1000000000ULL;
        }

        ret = pthread_cond_timedwait(&pq->cond, &pq->lock, &tp);

        if (pq->exit) {
            LIST_FOR_EACH_ENTRY_SAFE(surf, tmp, &pq->surf_list, list_item) {
                pthread_mutex_lock(&surf->lock);

                surf->status = VDP_PRESENTATION_QUEUE_STATUS_IDLE;
                surf->first_presentation_time = 0;
                pthread_cond_signal(&surf->idle_cond);

                LIST_DEL(&surf->list_item);
                pthread_mutex_unlock(&surf->lock);

                unref_surface(surf);
            }

            pqt_display_surface_to_idle_state(pqt);

            pthread_mutex_unlock(&pq->lock);

            return NULL;
        }

        if (ret != ETIMEDOUT) {
            time = get_time();
        }

        earliest_time = UINT64_MAX;

        LIST_FOR_EACH_ENTRY_SAFE(surf, tmp, &pq->surf_list, list_item) {
            pthread_mutex_lock(&surf->lock);

            if (surf->status != VDP_PRESENTATION_QUEUE_STATUS_QUEUED) {
                pthread_mutex_unlock(&surf->lock);
                continue;
            }

            if (surf->earliest_presentation_time < earliest_time) {
                earliest_time = surf->earliest_presentation_time;
            }

            if (surf->earliest_presentation_time > time) {
                pthread_mutex_unlock(&surf->lock);
                continue;
            }

            LIST_DEL(&surf->list_item);
            pthread_mutex_unlock(&surf->lock);

            pqt_update_dri_buffer(pqt, surf);
            pqt_display_surface(pqt, surf);
        }

        time = earliest_time;
    }

    pthread_mutex_unlock(&pq->lock);

    return NULL;
}

VdpStatus vdp_presentation_queue_create(
                        VdpDevice device,
                        VdpPresentationQueueTarget presentation_queue_target,
                        VdpPresentationQueue *presentation_queue)
{
    tegra_device *dev = get_device(device);
    tegra_pqt *pqt = get_presentation_queue_target(presentation_queue_target);
    tegra_pq *pq;
    VdpPresentationQueue i;
    pthread_mutexattr_t mutex_attrs;
    pthread_condattr_t cond_attrs;
    pthread_attr_t thread_attrs;
    int ret;

    if (dev == NULL || pqt == NULL) {
        put_device(dev);
        put_queue_target(pqt);
        return VDP_STATUS_INVALID_HANDLE;
    }

    pthread_mutex_lock(&global_lock);

    for (i = 0; i < MAX_PRESENTATION_QUEUES_NB; i++) {
        pq = __get_presentation_queue(i);

        if (pq == NULL) {
            pq = calloc(1, sizeof(tegra_pq));
            set_presentation_queue(i, pq);
            break;
        }
    }

    pthread_mutex_unlock(&global_lock);

    if (i == MAX_PRESENTATION_QUEUES_NB || pq == NULL) {
        put_device(dev);
        put_queue_target(pqt);
        return VDP_STATUS_RESOURCES;
    }

    pthread_mutexattr_init(&mutex_attrs);
    pthread_mutexattr_settype(&mutex_attrs, PTHREAD_MUTEX_RECURSIVE);

    ret = pthread_mutex_init(&pq->lock, &mutex_attrs);
    if (ret != 0) {
        ErrorMsg("pthread_mutex_init failed\n");
        put_device(dev);
        put_queue_target(pqt);
        return VDP_STATUS_RESOURCES;
    }

    pthread_condattr_init(&cond_attrs);
    pthread_condattr_setclock(&cond_attrs, CLOCK_MONOTONIC);

    ret = pthread_cond_init(&pq->cond, &cond_attrs);
    if (ret != 0) {
        ErrorMsg("pthread_cond_init failed\n");
        put_device(dev);
        put_queue_target(pqt);
        return VDP_STATUS_RESOURCES;
    }

    LIST_INITHEAD(&pq->surf_list);
    atomic_set(&pq->refcnt, 1);
    pq->pqt = pqt;

    pthread_attr_init(&thread_attrs);
    pthread_attr_setdetachstate(&thread_attrs, PTHREAD_CREATE_JOINABLE);

    ret = pthread_create(&pq->disp_thread, &thread_attrs,
                         presentation_queue_thr, pq);
    if (ret != 0) {
        ErrorMsg("pthread_create failed\n");
        put_device(dev);
        put_queue_target(pqt);
        return VDP_STATUS_RESOURCES;
    }

    ref_queue_target(pqt);
    ref_device(dev);

    *presentation_queue = i;

    put_device(dev);
    put_queue_target(pqt);

    return VDP_STATUS_OK;
}

void ref_presentation_queue(tegra_pq *pq)
{
    atomic_inc(&pq->refcnt);
}

VdpStatus unref_presentation_queue(tegra_pq *pq)
{
    tegra_device *dev;
    tegra_pqt *pqt;

    if (!atomic_dec_and_test(&pq->refcnt))
        return VDP_STATUS_OK;

    pqt = pq->pqt;
    dev = pqt->dev;

    pthread_mutex_lock(&pq->lock);
    pq->exit = true;
    pthread_cond_signal(&pq->cond);
    pthread_mutex_unlock(&pq->lock);

    pthread_join(pq->disp_thread, NULL);

    unref_queue_target(pqt);
    unref_device(dev);
    free(pq);

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_destroy(
                                        VdpPresentationQueue presentation_queue)
{
    tegra_pq *pq = get_presentation_queue(presentation_queue);

    if (pq == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    set_presentation_queue(presentation_queue, NULL);
    put_presentation_queue(pq);

    unref_presentation_queue(pq);

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_set_background_color(
                                        VdpPresentationQueue presentation_queue,
                                        VdpColor *const background_color)
{
    tegra_pq *pq = get_presentation_queue(presentation_queue);

    if (pq == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_presentation_queue(pq);

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_get_background_color(
                                        VdpPresentationQueue presentation_queue,
                                        VdpColor *const background_color)
{
    tegra_pq *pq = get_presentation_queue(presentation_queue);

    if (pq == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_presentation_queue(pq);

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_get_time(
                                        VdpPresentationQueue presentation_queue,
                                        VdpTime *current_time)
{
    tegra_pq *pq = get_presentation_queue(presentation_queue);

    if (pq == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    *current_time = get_time();

    put_presentation_queue(pq);

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_display(
                                        VdpPresentationQueue presentation_queue,
                                        VdpOutputSurface surface,
                                        uint32_t clip_width,
                                        uint32_t clip_height,
                                        VdpTime earliest_presentation_time)
{
    tegra_pq *pq = get_presentation_queue(presentation_queue);
    tegra_surface *surf;
    VdpStatus ret = VDP_STATUS_OK;

    if (pq == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    surf = get_surface(surface);
    if (surf == NULL) {
        /* This will happen on surface allocation failure. */
        return VDP_STATUS_RESOURCES;
    }

    pthread_mutex_lock(&surf->lock);

    if (surf->status == VDP_PRESENTATION_QUEUE_STATUS_QUEUED) {
        ret = VDP_STATUS_ERROR;
        goto unlock_surf;
    }

    ref_surface(surf);
    surf->disp_width  = clip_width  ?: surf->width;
    surf->disp_height = clip_height ?: surf->height;

    if (earliest_presentation_time == 0) {
        pthread_mutex_lock(&pq->lock);

        pqt_update_dri_buffer(pq->pqt, surf);
        pqt_display_surface(pq->pqt, surf);

        pthread_mutex_unlock(&pq->lock);

        goto unlock_surf;
    }

    surf->status = VDP_PRESENTATION_QUEUE_STATUS_QUEUED;
    surf->earliest_presentation_time = earliest_presentation_time;

    pthread_mutex_lock(&pq->lock);

    LIST_ADDTAIL(&surf->list_item, &pq->surf_list);
    pthread_cond_signal(&pq->cond);

    pthread_mutex_unlock(&pq->lock);

unlock_surf:
    pthread_mutex_unlock(&surf->lock);

    put_surface(surf);
    put_presentation_queue(pq);

    return ret;
}

VdpStatus vdp_presentation_queue_block_until_surface_idle(
                                        VdpPresentationQueue presentation_queue,
                                        VdpOutputSurface surface,
                                        VdpTime *first_presentation_time)
{
    tegra_surface *itr, *surf = get_surface(surface);
    tegra_pq *pq = get_presentation_queue(presentation_queue);
    VdpStatus ret = VDP_STATUS_ERROR;
    int err;

    if (surf == NULL || pq == NULL) {
        put_surface(surf);
        put_presentation_queue(pq);
        *first_presentation_time = get_time();
        return VDP_STATUS_INVALID_HANDLE;
    }

retry:
    pthread_mutex_lock(&surf->lock);

    if (surf->status == VDP_PRESENTATION_QUEUE_STATUS_IDLE) {
        *first_presentation_time = surf->first_presentation_time;
        ret = VDP_STATUS_OK;
        goto unlock_surf;
    }

    err = pthread_mutex_trylock(&pq->lock);
    if (err) {
        pthread_mutex_unlock(&surf->lock);
        goto retry;
    }

    LIST_FOR_EACH_ENTRY(itr, &pq->surf_list, list_item) {
        if (itr->earliest_presentation_time > surf->earliest_presentation_time) {
            ret = VDP_STATUS_OK;
            break;
        }
    }
    pthread_mutex_unlock(&pq->lock);

    if (ret == VDP_STATUS_OK) {
        pthread_cond_wait(&surf->idle_cond, &surf->lock);
        *first_presentation_time = surf->first_presentation_time;
    } else {
        *first_presentation_time = 0;
    }

unlock_surf:
    pthread_mutex_unlock(&surf->lock);

    put_surface(surf);
    put_presentation_queue(pq);

    return ret;
}

VdpStatus vdp_presentation_queue_query_surface_status(
                                        VdpPresentationQueue presentation_queue,
                                        VdpOutputSurface surface,
                                        VdpPresentationQueueStatus *status,
                                        VdpTime *first_presentation_time)
{
    tegra_surface *surf = get_surface(surface);
    tegra_pq *pq = get_presentation_queue(presentation_queue);

    if (surf == NULL || pq == NULL) {
        put_surface(surf);
        put_presentation_queue(pq);
        *first_presentation_time = get_time();
        return VDP_STATUS_INVALID_HANDLE;
    }

    *status = surf->status;
    *first_presentation_time = surf->first_presentation_time;

    put_surface(surf);
    put_presentation_queue(pq);

    return VDP_STATUS_OK;
}
