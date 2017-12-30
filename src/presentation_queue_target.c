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

void pqt_update_dri_pixbuf(tegra_pqt *pqt)
{
    tegra_device *dev = pqt->dev;
    struct drm_tegra_bo *bo;
    DRI2Buffer *buf;
    unsigned int attachment = DRI2BufferBackLeft;
    int width, height;
    int outCount;
    int err;

    if (pqt->dri_pixbuf) {
        host1x_pixelbuffer_free(pqt->dri_pixbuf);
        pqt->dri_pixbuf = NULL;
    }

    buf = DRI2GetBuffers(dev->display, pqt->drawable, &width, &height,
                         &attachment, 1, &outCount);
    if (!buf || outCount != 1) {
        ErrorMsg("Failed to get DRI2 buffer\n");
        return;
    }

    DebugMsg("width %d height %d\n", width, height);

    err = drm_tegra_bo_from_name(&bo, dev->drm, buf[0].names[0], 0);
    if (err) {
        return;
    }

    err = drm_tegra_bo_forbid_caching(bo);
    if (err) {
        drm_tegra_bo_unref(bo);
        return;
    }

    pqt->dri_pixbuf = host1x_pixelbuffer_wrap(&bo, width, height,
                                              buf[0].pitch[0], 0,
                                              PIX_BUF_FMT_ARGB8888,
                                              PIX_BUF_LAYOUT_LINEAR);
    if (!pqt->dri_pixbuf) {
        drm_tegra_bo_unref(bo);
        return;
    }
}

static bool initialize_dri2(tegra_pqt *pqt)
{
    tegra_device *dev = pqt->dev;
    char *driverName, *deviceName;
    Bool ret;

    ret = DRI2Connect(dev->display, pqt->drawable, DRI2DriverVDPAU,
                      &driverName, &deviceName);
    if (!ret) {
        ErrorMsg("DRI2 connect failed\n");
        return false;
    }

    DRI2CreateDrawable(dev->display, pqt->drawable);

    pqt_update_dri_pixbuf(pqt);

    return true;
}

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

    if (!DRI_OUTPUT) {
        XSetWindowBackground(dev->display, drawable, pqt->bg_color);
        XClearWindow(dev->display, drawable);
    } else {
        if (!initialize_dri2(pqt)) {
            put_queue_target(pqt);
            put_device(dev);
            return VDP_STATUS_RESOURCES;
        }
    }

    *target = i;

    put_device(dev);

    return VDP_STATUS_OK;
}
