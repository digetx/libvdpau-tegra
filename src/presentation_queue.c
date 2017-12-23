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

static void * x11_thr(void *opaque)
{
    tegra_pq *pq = opaque;
    tegra_pqt *pqt = pq->pqt;
    tegra_device *dev = pqt->dev;
    XEvent event;
    int x = 0, y = 0, width = 0, height = 0;

    while (true) {
        if (!pq->exit &&
            !XCheckWindowEvent(dev->display, pqt->drawable,
                               StructureNotifyMask, &event)) {
            usleep(100000);
            continue;
        }

        if (pq->exit)
            break;

        switch (event.type) {
        case ConfigureNotify:
            if (x != event.xconfigure.x)
                break;

            if (y != event.xconfigure.y)
                break;

            if (width != event.xconfigure.width)
                break;

            if (height != event.xconfigure.height)
                break;

        default:
            continue;
        }

        x = event.xconfigure.x;
        y = event.xconfigure.y;
        width = event.xconfigure.width;
        height = event.xconfigure.height;

        pthread_mutex_lock(&pq->lock);

        if (pqt->disp_surf) {
                XvPutImage(dev->display, dev->xv_port,
                           pqt->drawable, pqt->gc,
                           pqt->disp_surf->xv_img,
                           0, 0,
                           pqt->disp_surf->disp_width,
                           pqt->disp_surf->disp_height,
                           0, 0,
                           pqt->disp_surf->disp_width,
                           pqt->disp_surf->disp_height);

                XSync(dev->display, 0);
        }

        pthread_mutex_unlock(&pq->lock);
    }

    return NULL;
}

static void pqt_display_surface_to_idle_state(tegra_pqt *pqt)
{
    if (!pqt->disp_surf) {
        return;
    }

    pthread_mutex_lock(&pqt->disp_surf->lock);

    pqt->disp_surf->status = VDP_PRESENTATION_QUEUE_STATUS_IDLE;
    pthread_cond_signal(&pqt->disp_surf->idle_cond);

    pthread_mutex_unlock(&pqt->disp_surf->lock);

    unref_surface(pqt->disp_surf);

    pqt->disp_surf = NULL;
}

static void * presentation_queue_thr(void *opaque)
{
    tegra_pq *pq = opaque;
    tegra_pqt *pqt = pq->pqt;
    tegra_device *dev = pqt->dev;
    tegra_surface *surf, *tmp;
    struct timespec tp;
    VdpTime time = UINT64_MAX;
    int ret;

    while (true) {
        pthread_mutex_lock(&pq->lock);

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

        if (ret == ETIMEDOUT) {
            time = (VdpTime)tp.tv_sec * 1000000000ULL + (VdpTime)tp.tv_nsec;

            LIST_FOR_EACH_ENTRY_SAFE(surf, tmp, &pq->surf_list, list_item) {
                pthread_mutex_lock(&surf->lock);

                if (surf->earliest_presentation_time > time) {
                    pthread_mutex_unlock(&surf->lock);
                    continue;
                }

                if (surf->earliest_presentation_time < time) {
                    surf->status = VDP_PRESENTATION_QUEUE_STATUS_IDLE;
                    surf->first_presentation_time = 0;
                    pthread_cond_signal(&surf->idle_cond);

                    goto del_surface;
                }

                XvPutImage(dev->display, dev->xv_port,
                           pqt->drawable, pqt->gc,
                           surf->xv_img,
                           0, 0,
                           surf->disp_width,
                           surf->disp_height,
                           0, 0,
                           surf->disp_width,
                           surf->disp_height);

                XSync(dev->display, 0);

                surf->first_presentation_time = get_time();
                surf->status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;

                if (pqt->disp_surf != surf) {
                    pqt_display_surface_to_idle_state(pqt);
                    pqt->disp_surf = surf;
                }

                ref_surface(surf);
del_surface:
                LIST_DEL(&surf->list_item);
                pthread_mutex_unlock(&surf->lock);
                unref_surface(surf);
            }
        }

        time = UINT64_MAX;

        LIST_FOR_EACH_ENTRY(surf, &pq->surf_list, list_item) {
            if (surf->earliest_presentation_time < time)
                time = surf->earliest_presentation_time;
        }

        pthread_mutex_unlock(&pq->lock);
    }

    return NULL;
}

VdpStatus unref_queue_target(tegra_pqt *pqt)
{
    tegra_device *dev = pqt->dev;

    if (!atomic_dec_and_test(&pqt->refcnt))
        return VDP_STATUS_OK;

    if (pqt->gc != None)
        XFreeGC(dev->display, pqt->gc);

    free(pqt);

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_target_destroy(
                        VdpPresentationQueueTarget presentation_queue_target)
{
    tegra_pqt *pqt = get_presentation_queue_target(presentation_queue_target);

    if (pqt == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    set_presentation_queue_target(presentation_queue_target, NULL);

    return unref_queue_target(pqt);
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
    pthread_condattr_t cond_attrs;
    pthread_attr_t thread_attrs;
    int ret;

    if (dev == NULL || pqt == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    pthread_mutex_lock(&global_lock);

    for (i = 0; i < MAX_PRESENTATION_QUEUES_NB; i++) {
        pq = get_presentation_queue(i);

        if (pq == NULL) {
            pq = calloc(1, sizeof(tegra_pq));
            set_presentation_queue(i, pq);
            break;
        }
    }

    pthread_mutex_unlock(&global_lock);

    if (i == MAX_PRESENTATION_QUEUES_NB || pq == NULL) {
        return VDP_STATUS_RESOURCES;
    }

    ret = pthread_mutex_init(&pq->lock, NULL);
    if (ret != 0) {
        ErrorMsg("pthread_mutex_init failed\n");
        return VDP_STATUS_RESOURCES;
    }

    pthread_condattr_init(&cond_attrs);
    pthread_condattr_setclock(&cond_attrs, CLOCK_MONOTONIC);

    ret = pthread_cond_init(&pq->cond, &cond_attrs);
    if (ret != 0) {
        ErrorMsg("pthread_cond_init failed\n");
        return VDP_STATUS_RESOURCES;
    }

    pthread_condattr_destroy(&cond_attrs);

    LIST_INITHEAD(&pq->surf_list);
    pq->pqt = pqt;

    pthread_attr_init(&thread_attrs);
    pthread_attr_setdetachstate(&thread_attrs, PTHREAD_CREATE_JOINABLE);

    ret = pthread_create(&pq->disp_thread, &thread_attrs,
                         presentation_queue_thr, pq);
    if (ret != 0) {
        ErrorMsg("pthread_create failed\n");
        return VDP_STATUS_RESOURCES;
    }

    pthread_attr_destroy(&thread_attrs);

    if (_Xglobal_lock) {
        pthread_attr_init(&thread_attrs);
        pthread_attr_setdetachstate(&thread_attrs, PTHREAD_CREATE_JOINABLE);

        ret = pthread_create(&pq->x11_thread, &thread_attrs, x11_thr, pq);
        if (ret != 0) {
            ErrorMsg("pthread_create failed\n");
            return VDP_STATUS_RESOURCES;
        }

        pthread_attr_destroy(&thread_attrs);
    }

    atomic_inc(&pqt->refcnt);
    ref_device(dev);

    *presentation_queue = i;

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_destroy(
                                        VdpPresentationQueue presentation_queue)
{
    tegra_pq *pq = get_presentation_queue(presentation_queue);
    tegra_pqt *pqt;
    tegra_device *dev;

    if (pq == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    pqt = pq->pqt;
    dev = pqt->dev;

    set_presentation_queue(presentation_queue, NULL);

    pthread_mutex_lock(&pq->lock);

    pq->exit = true;
    pthread_cond_signal(&pq->cond);

    pthread_mutex_unlock(&pq->lock);

    if (_Xglobal_lock) {
        pthread_join(pq->x11_thread, NULL);
    }
    pthread_join(pq->disp_thread, NULL);
    unref_queue_target(pqt);
    unref_device(dev);
    free(pq);

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

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_display(
                                        VdpPresentationQueue presentation_queue,
                                        VdpOutputSurface surface,
                                        uint32_t clip_width,
                                        uint32_t clip_height,
                                        VdpTime earliest_presentation_time)
{
    tegra_surface *surf = get_surface(surface);
    tegra_pq *pq = get_presentation_queue(presentation_queue);
    tegra_pqt *pqt;
    tegra_device *dev;
    VdpTime time;

    if (pq == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    pqt = pq->pqt;
    dev = pqt->dev;

    /* This will happen on surface allocation failure. */
    if (surf == NULL) {
        time = get_time();

        if (earliest_presentation_time > time) {
            usleep((earliest_presentation_time - time) / 1000);
        }

        pthread_mutex_lock(&pq->lock);

        pqt_display_surface_to_idle_state(pqt);

        pthread_cond_signal(&pq->cond);
        pthread_mutex_unlock(&pq->lock);

        return VDP_STATUS_INVALID_HANDLE;
    }

    pthread_mutex_lock(&pq->lock);
    pthread_mutex_lock(&surf->lock);

    assert(surf->idle_hack ||
           surf->status == VDP_PRESENTATION_QUEUE_STATUS_IDLE);

    surf->disp_width  = clip_width  ?: surf->xv_img->width;
    surf->disp_height = clip_height ?: surf->xv_img->height;
    surf->idle_hack = false;

    /* XXX: X11 app won't survive threading without XInitThreads() */
    if (earliest_presentation_time == 0 || !_Xglobal_lock) {
        XvPutImage(dev->display, dev->xv_port,
                   pqt->drawable, pqt->gc,
                   surf->xv_img,
                   0, 0,
                   surf->disp_width,
                   surf->disp_height,
                   0, 0,
                   surf->disp_width,
                   surf->disp_height);

        XSync(dev->display, 0);

        surf->status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;
        surf->first_presentation_time = get_time();
        surf->idle_hack = true;

        pthread_mutex_unlock(&surf->lock);
        pthread_mutex_unlock(&pq->lock);

        return VDP_STATUS_OK;
    }

    ref_surface(surf);
    LIST_ADDTAIL(&surf->list_item, &pq->surf_list);

    surf->status = VDP_PRESENTATION_QUEUE_STATUS_QUEUED;
    surf->earliest_presentation_time = earliest_presentation_time;

    pthread_mutex_unlock(&surf->lock);

    pthread_cond_signal(&pq->cond);
    pthread_mutex_unlock(&pq->lock);

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_block_until_surface_idle(
                                        VdpPresentationQueue presentation_queue,
                                        VdpOutputSurface surface,
                                        VdpTime *first_presentation_time)
{
    tegra_surface *surf = get_surface(surface);
    tegra_pq *pq = get_presentation_queue(presentation_queue);

    if (surf == NULL || pq == NULL) {
        *first_presentation_time = get_time();
        return VDP_STATUS_INVALID_HANDLE;
    }

    ref_surface(surf);

    pthread_mutex_lock(&surf->lock);

    if (!surf->idle_hack && surf->status != VDP_PRESENTATION_QUEUE_STATUS_IDLE)
        pthread_cond_wait(&surf->idle_cond, &surf->lock);

    *first_presentation_time = surf->first_presentation_time;

    pthread_mutex_unlock(&surf->lock);

    unref_surface(surf);

    return VDP_STATUS_OK;
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
        *first_presentation_time = get_time();
        return VDP_STATUS_INVALID_HANDLE;
    }

    *status = surf->status;
    *first_presentation_time = surf->first_presentation_time;

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_target_create_x11(
                                        VdpDevice device,
                                        Drawable drawable,
                                        VdpPresentationQueueTarget *target)
{
    tegra_device *dev = get_device(device);
    VdpPresentationQueueTarget i;
    XGCValues values;
    tegra_pqt *pqt;

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    pthread_mutex_lock(&global_lock);

    for (i = 0; i < MAX_PRESENTATION_QUEUE_TARGETS_NB; i++) {
        pqt = get_presentation_queue_target(i);

        if (pqt == NULL) {
            pqt = calloc(1, sizeof(tegra_pqt));
            set_presentation_queue_target(i, pqt);
            break;
        }
    }

    pthread_mutex_unlock(&global_lock);

    if (i == MAX_PRESENTATION_QUEUE_TARGETS_NB || pqt == NULL) {
        return VDP_STATUS_RESOURCES;
    }

    atomic_set(&pqt->refcnt, 1);
    pqt->dev = dev;
    pqt->drawable = drawable;
    pqt->gc = XCreateGC(dev->display, drawable, 0, &values);

    *target = i;

    return VDP_STATUS_OK;
}
