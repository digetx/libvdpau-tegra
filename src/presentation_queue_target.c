/*
 * NVIDIA TEGRA 2 VDPAU backend driver
 *
 * Copyright (c) 2016-2017 Dmitry Osipenko <digetx@gmail.com>
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

void ref_queue_target(tegra_pqt *pqt)
{
    atomic_inc(&pqt->refcnt);
}

VdpStatus unref_queue_target(tegra_pqt *pqt)
{
    tegra_device *dev = pqt->dev;

    if (!atomic_dec_and_test(&pqt->refcnt))
        return VDP_STATUS_OK;

    if (pqt->gc != None)
        XFreeGC(dev->display, pqt->gc);

    unref_device(dev);
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
    put_queue_target(pqt);

    unref_queue_target(pqt);

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
        pqt = __get_presentation_queue_target(i);

        if (pqt == NULL) {
            pqt = calloc(1, sizeof(tegra_pqt));
            set_presentation_queue_target(i, pqt);
            break;
        }
    }

    pthread_mutex_unlock(&global_lock);

    if (i == MAX_PRESENTATION_QUEUE_TARGETS_NB || pqt == NULL) {
        put_device(dev);
        return VDP_STATUS_RESOURCES;
    }

    atomic_set(&pqt->refcnt, 1);
    ref_device(dev);
    pqt->dev = dev;
    pqt->drawable = drawable;
    pqt->gc = XCreateGC(dev->display, drawable, 0, &values);

    XSetWindowBackground(dev->display, drawable, pqt->bg_color);
    XClearWindow(dev->display, drawable);

    *target = i;

    put_device(dev);

    return VDP_STATUS_OK;
}
