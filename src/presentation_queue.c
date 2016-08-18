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

static uint64_t get_time(void)
{
    struct timespec tp;

    if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
        return 0;

    return (uint64_t)tp.tv_sec * 1000000000ULL + (uint64_t)tp.tv_nsec;
}

VdpStatus vdp_presentation_queue_target_destroy(
                        VdpPresentationQueueTarget presentation_queue_target)
{
    tegra_pqt *pqt = get_presentation_queue_target(presentation_queue_target);

    if (pqt == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    set_presentation_queue_target(presentation_queue_target, NULL);

    free(pqt);

    return VDP_STATUS_OK;
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

    pq->presentation_queue_target = presentation_queue_target;

    *presentation_queue = i;

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

    if (surf == NULL || pq == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    pqt = get_presentation_queue_target(pq->presentation_queue_target);

    if (pqt == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    dev = get_device(pqt->device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    assert(surf->img != NULL);

    if (surf->pix_disp != NULL) {
        /* Display uses other pixel format, conversion is required.  */
        pixman_image_composite(PIXMAN_OP_SRC,
                               surf->pix,
                               NULL,
                               surf->pix_disp,
                               0, 0,
                               0, 0,
                               0, 0,
                               clip_width, clip_height);
    }

    XPutImage(dev->display, pqt->drawable, pqt->gc,
              surf->img,
              0, 0,
              0, 0,
              clip_width, clip_height);

    XSync(dev->display, 0);

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
        return VDP_STATUS_INVALID_HANDLE;
    }

    *first_presentation_time = get_time();

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
        return VDP_STATUS_INVALID_HANDLE;
    }

    *status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;
    *first_presentation_time = get_time();

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

    XSetWindowBackground(dev->display, drawable,
                         BlackPixel(dev->display, dev->screen));
    XClearWindow(dev->display, drawable);
    XSync(dev->display, 0);

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

    pqt->device = device;
    pqt->drawable = drawable;
    pqt->gc = XCreateGC(dev->display, drawable, 0, &values);

    *target = i;

    return VDP_STATUS_OK;
}
