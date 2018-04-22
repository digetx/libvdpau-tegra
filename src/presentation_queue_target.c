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

void pqt_display_surface_to_idle_state(tegra_pqt *pqt)
{
    tegra_surface *surf;

    pthread_mutex_lock(&pqt->lock);

    surf = pqt->disp_surf;
    if (!surf) {
        pthread_mutex_unlock(&pqt->lock);
        return;
    }

    pthread_mutex_lock(&surf->lock);
    if (surf->status == VDP_PRESENTATION_QUEUE_STATUS_VISIBLE) {
        surf->status = VDP_PRESENTATION_QUEUE_STATUS_IDLE;
        pthread_cond_signal(&surf->idle_cond);
        pqt->disp_surf = NULL;

        DebugMsg("visible surface %u became idle\n",
                 surf->surface_id);
    } else {
        DebugMsg("trying to set invisible surface %u to idle\n",
                 surf->surface_id);
    }
    pthread_mutex_unlock(&surf->lock);

    pthread_mutex_unlock(&pqt->lock);

    unref_surface(surf);
}

static void pqt_create_dri2_drawable(tegra_pqt *pqt)
{
    tegra_device *dev = pqt->dev;

    if (!pqt->dri2_drawable_created) {
        DRI2CreateDrawable(dev->display, pqt->drawable);
        DRI2SwapInterval(dev->display, pqt->drawable, 1);
        pqt->dri2_drawable_created = true;
    }
}

static void pqt_destroy_dri2_drawable(tegra_pqt *pqt)
{
    tegra_device *dev = pqt->dev;

    if (pqt->dri2_drawable_created) {
        DRI2DestroyDrawable(dev->display, pqt->drawable);
        pqt->dri2_drawable_created = false;
    }
}

static int pqt_update_dri_pixbuf(tegra_pqt *pqt)
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

    pqt_create_dri2_drawable(pqt);

    buf = DRI2GetBuffers(dev->display, pqt->drawable, &width, &height,
                         &attachment, 1, &outCount);
    if (!buf || outCount != 1) {
        ErrorMsg("Failed to get DRI2 buffer\n");
        return -1;
    }

    DebugMsg("width %d height %d\n", width, height);

    err = drm_tegra_bo_from_name(&bo, dev->drm, buf[0].names[0], 0);
    if (err) {
        return err;
    }

    err = drm_tegra_bo_forbid_caching(bo);
    if (err) {
        drm_tegra_bo_unref(bo);
        return err;
    }

    pqt->dri_pixbuf = host1x_pixelbuffer_wrap(&bo, width, height,
                                              buf[0].pitch[0], 0,
                                              PIX_BUF_FMT_ARGB8888,
                                              PIX_BUF_LAYOUT_LINEAR);
    if (!pqt->dri_pixbuf) {
        drm_tegra_bo_unref(bo);
        return -2;
    }

    return 0;
}

static bool initialize_dri2(tegra_pqt *pqt)
{
    tegra_device *dev = pqt->dev;
    char *driverName, *deviceName;
    int ret;

    pthread_mutex_lock(&global_lock);

    if (!dev->dri2_inited) {
        dev->dri2_inited = true;

        ret = DRI2Connect(dev->display, pqt->drawable, DRI2DriverVDPAU,
                          &driverName, &deviceName);
        if (!ret) {
            ErrorMsg("DRI2 connect failed\n");

            if (!tegra_vdpau_force_xv) {
                ErrorMsg("forcing Xv output\n");
                tegra_vdpau_force_xv = true;
            }

            tegra_vdpau_force_dri = false;

            goto unlock;
        }

        ret = pqt_update_dri_pixbuf(pqt);
        if (ret) {
            ErrorMsg("DRI2 buffer preparation failed %d\n", ret);

            pqt_destroy_dri2_drawable(pqt);

            if (!tegra_vdpau_force_xv) {
                ErrorMsg("forcing Xv output\n");
                tegra_vdpau_force_xv = true;
            }

            tegra_vdpau_force_dri = false;

            goto unlock;
        }

        DebugMsg("Success\n");

        dev->dri2_ready = true;
    }

unlock:
    pthread_mutex_unlock(&global_lock);

    return dev->dri2_ready;
}

static void pqt_display_dri(tegra_pqt *pqt, tegra_surface *surf)
{
    tegra_device *dev = pqt->dev;
    CARD64 ust, msc, sbc;
    CARD64 count;

    DebugMsg("surface %u DRI\n", surf->surface_id);

    pthread_mutex_lock(&dev->lock);
    DRI2GetMSC(dev->display, pqt->drawable, &ust, &msc, &sbc);
    DRI2SwapBuffers(dev->display, pqt->drawable, 0, 0, 0, &count);
    DRI2WaitMSC(dev->display, pqt->drawable, msc + 1, 1, 1, &ust, &msc, &sbc);
    pthread_mutex_unlock(&dev->lock);

    if (pqt->dri_prep_surf == surf) {
        pqt->dri_prep_surf = NULL;
    }

    if (surf->set_bg) {
        pqt->bg_color = surf->bg_color;
    }
}

static void pqt_display_xv(tegra_pqt *pqt, tegra_surface *surf)
{
    tegra_device *dev = pqt->dev;

    if (surf->set_bg && surf->bg_color != pqt->bg_color) {
        XSetWindowBackground(dev->display, pqt->drawable, surf->bg_color);
        XClearWindow(dev->display, pqt->drawable);

        pqt->bg_color = surf->bg_color;
    }

    if (surf->shared && surf->shared->xv_img) {
        DebugMsg("surface %u YUV overlay\n", surf->surface_id);

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
        DebugMsg("surface %u RGB overlay\n", surf->surface_id);

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
    } else {
        DebugMsg("surface %u is absent\n", surf->surface_id);
    }

    XSync(dev->display, 0);
}

static void transit_display_to_xv(tegra_pqt *pqt)
{
    tegra_surface *surf = pqt->disp_surf;
    tegra_device *dev = pqt->dev;

    DebugMsg("surface %u\n", surf->surface_id);

    pqt_destroy_dri2_drawable(pqt);

    XClearWindow(dev->display, pqt->drawable);
    XSync(dev->display, 0);

    pqt->disp_state = TEGRA_PQT_XV;
}

static void transit_display_to_dri(tegra_pqt *pqt)
{
    tegra_surface *surf = pqt->disp_surf;
    tegra_device *dev = pqt->dev;

    DebugMsg("surface %u\n", surf->surface_id);

    XvStopVideo(dev->display, dev->xv_port, pqt->drawable);

    pqt->disp_state = TEGRA_PQT_DRI;
}

static void pqt_update_dri_buffer(tegra_pqt *pqt, tegra_surface *surf)
{
    bool new_buffer;
    int ret;

    if (!pqt->dri_pixbuf ||
        surf->disp_width != pqt->dri_pixbuf->width ||
        surf->disp_height != pqt->dri_pixbuf->height)
    {
        pqt_update_dri_pixbuf(pqt);
        new_buffer = true;
    } else {
        new_buffer = false;
    }

    if (!pqt->dri_pixbuf) {
        return;
    }

    if (!new_buffer && pqt->dri_prep_surf == surf) {
        DebugMsg("using prepared surface %u\n", surf->surface_id);
        pqt->dri_prep_surf = NULL;
        return;
    }

    DebugMsg("surface %u+\n", surf->surface_id);

    pthread_mutex_lock(&surf->lock);
    pthread_mutex_lock(&pqt->dev->lock);

    if (surf->shared) {
        DebugMsg("surface %u transfer YUV\n", surf->surface_id);

        if (surf->set_bg) {
            ret = host1x_gr2d_clear_rect_clipped(surf->dev->stream,
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
            if (ret) {
                ErrorMsg("setting BG failed %d\n", ret);
            }
        }

        ret = host1x_gr2d_surface_blit(pqt->dev->stream,
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
        if (ret) {
            ErrorMsg("video transfer failed %d\n", ret);
        }
    } else if (surf->pixbuf) {
        DebugMsg("surface %u transfer RGB\n", surf->surface_id);

        ret = host1x_gr2d_blit(pqt->dev->stream,
                               surf->pixbuf,
                               pqt->dri_pixbuf,
                               0,
                               0,
                               0,
                               0,
                               surf->disp_width,
                               surf->disp_height);
        if (ret) {
            ErrorMsg("video transfer failed %d\n", ret);
        }
    } else {
        DebugMsg("surface %u is absent\n", surf->surface_id);
    }

    pthread_mutex_unlock(&pqt->dev->lock);
    pthread_mutex_unlock(&surf->lock);

    DebugMsg("surface %u-\n", surf->surface_id);
}

void pqt_prepare_dri_surface(tegra_pqt *pqt, tegra_surface *surf)
{
    pthread_mutex_lock(&pqt->lock);

    if ((tegra_vdpau_force_dri || pqt->overlapped_current) &&
        !initialize_dri2(pqt))
    {
        pthread_mutex_unlock(&pqt->lock);
        return;
    }

    pthread_mutex_lock(&surf->lock);

    if (tegra_vdpau_force_dri || pqt->overlapped_current) {
        pqt_update_dri_buffer(pqt, surf);
        pqt->dri_prep_surf = surf;

        DebugMsg("surface %u\n", surf->surface_id);
    }

    pthread_mutex_unlock(&surf->lock);
    pthread_mutex_unlock(&pqt->lock);
}

void pqt_display_surface(tegra_pqt *pqt, tegra_surface *surf,
                         bool update_status, bool transit)
{
    tegra_device *dev = pqt->dev;

    DebugMsg("surface %u earliest_presentation_time %llu+\n",
             surf->surface_id, surf->earliest_presentation_time);

    pthread_mutex_lock(&pqt->lock);

    if (tegra_vdpau_force_dri || pqt->overlapped_current) {
        initialize_dri2(pqt);
    }

    pthread_mutex_lock(&surf->lock);

    if ((tegra_vdpau_force_dri ||
            (pqt->overlapped_current && !tegra_vdpau_force_xv)) &&
        dev->dri2_ready)
    {
        pqt_update_dri_buffer(pqt, surf);
        pqt_display_dri(pqt, surf);

        if (transit || pqt->disp_state != TEGRA_PQT_DRI) {
            transit_display_to_dri(pqt);
        }
    } else {
        pqt_display_xv(pqt, surf);

        if (transit || pqt->disp_state != TEGRA_PQT_XV) {
            transit_display_to_xv(pqt);
        }
    }

    if (pqt->disp_surf != surf) {
        pqt_display_surface_to_idle_state(pqt);
        pqt->disp_surf = surf;
    }

    if (update_status) {
        surf->first_presentation_time = get_time();
        surf->status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;
    }

    pthread_mutex_unlock(&surf->lock);
    pthread_mutex_unlock(&pqt->lock);

    DebugMsg("surface %u-\n", surf->surface_id);
}

static void * pqt_display_thr(void *opaque)
{
    tegra_pqt *pqt = opaque;
    bool overlapped;

    while (!pqt->exit) {
        pthread_mutex_lock(&pqt->disp_lock);
        if (pqt->overlapped_current == pqt->overlapped_new && !pqt->win_move) {
            pthread_cond_wait(&pqt->disp_cond, &pqt->disp_lock);

            if (pqt->exit)
                break;
        }
        overlapped = pqt->overlapped_new;
        pthread_mutex_unlock(&pqt->disp_lock);

        pthread_mutex_lock(&pqt->lock);
        if (pqt->overlapped_current != overlapped) {
            DebugMsg("updating overlap state\n");

            pqt->overlapped_current = overlapped;

            if (pqt->disp_surf) {
                pqt_display_surface(pqt, pqt->disp_surf, false, true);
            }
        }
        if (pqt->win_move) {
            pqt->win_move = false;

            if (pqt->disp_surf) {
                pqt_display_surface(pqt, pqt->disp_surf, false, false);
            }
        }
        pthread_mutex_unlock(&pqt->lock);
    }

    return NULL;
}

static void * pqt_x11_event_thr(void *opaque)
{
    tegra_pqt *pqt = opaque;
    tegra_device *dev = pqt->dev;
    bool overlapped;
    bool win_move;
    int x = 0, y = 0;
    XEvent event;
    struct timeval tv;
    fd_set in_fds;
    int fd;

    fd = ConnectionNumber(dev->display);

    while (!pqt->exit) {
        memset(&tv, 0, sizeof(tv));
        tv.tv_usec = 300000;

        FD_ZERO(&in_fds);
        FD_SET(fd, &in_fds);

        if (select(fd + 1, &in_fds, NULL, NULL, &tv) <= 0) {
            continue;
        }

        if (tegra_vdpau_force_xv || tegra_vdpau_force_dri)
            break;

        if (XCheckWindowEvent(dev->display, pqt->drawable,
                              VisibilityChangeMask, &event)) {

            overlapped = (event.xvisibility.state == VisibilityPartiallyObscured ||
                          event.xvisibility.state == VisibilityFullyObscured);

            if (pqt->overlapped_new != overlapped) {
                pthread_mutex_lock(&pqt->disp_lock);

                DebugMsg("window overlapped %d\n", overlapped);

                pqt->overlapped_new = overlapped;
                pthread_cond_signal(&pqt->disp_cond);

                pthread_mutex_unlock(&pqt->disp_lock);
            }
        } else if (XCheckWindowEvent(dev->display, pqt->drawable,
                                     StructureNotifyMask, &event)) {

            if (event.type == ConfigureNotify &&
                (x != event.xconfigure.x ||
                 y != event.xconfigure.y))
            {
                x = event.xconfigure.x;
                y = event.xconfigure.y;
                win_move = true;
            } else {
                win_move = false;
            }

            XPutBackEvent(dev->display, &event);

            if (win_move) {
                pthread_mutex_lock(&pqt->disp_lock);

                DebugMsg("window move (%d, %d)\n", x, y);

                pqt->win_move = true;
                pthread_cond_signal(&pqt->disp_cond);

                pthread_mutex_unlock(&pqt->disp_lock);
            }
        }
    }

    return NULL;
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

    if (_Xglobal_lock && !(tegra_vdpau_force_xv || tegra_vdpau_force_dri)) {
        pthread_join(pqt->x11_thread, NULL);

        pthread_mutex_lock(&pqt->disp_lock);
        pthread_cond_signal(&pqt->disp_cond);
        pthread_mutex_unlock(&pqt->disp_lock);

        pthread_join(pqt->disp_thread, NULL);
    }

    if (pqt->gc != None) {
        XFreeGC(dev->display, pqt->gc);
    }

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

    pqt->exit = true;

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
    pthread_mutexattr_t mutex_attrs;
    pthread_attr_t thread_attrs;
    XSetWindowAttributes set;
    XWindowAttributes get;

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

    pthread_mutexattr_init(&mutex_attrs);
    pthread_mutexattr_settype(&mutex_attrs, PTHREAD_MUTEX_RECURSIVE);

    pthread_mutex_init(&pqt->lock, &mutex_attrs);
    atomic_set(&pqt->refcnt, 1);
    ref_device(dev);
    pqt->dev = dev;
    pqt->drawable = drawable;
    pqt->gc = XCreateGC(dev->display, drawable, 0, &values);

    XGetWindowAttributes(dev->display, drawable, &get);
    set.event_mask  = get.all_event_masks;
    set.event_mask |= VisibilityChangeMask;
    set.event_mask |= StructureNotifyMask;
    XChangeWindowAttributes(dev->display, drawable, CWEventMask, &set);

    XSetWindowBackground(dev->display, drawable, pqt->bg_color);
    XClearWindow(dev->display, drawable);

    if (_Xglobal_lock && !(tegra_vdpau_force_xv || tegra_vdpau_force_dri)) {
        pthread_attr_init(&thread_attrs);
        pthread_attr_setdetachstate(&thread_attrs, PTHREAD_CREATE_JOINABLE);
        pthread_create(&pqt->x11_thread, &thread_attrs,
                       pqt_x11_event_thr, pqt);

        pthread_mutex_init(&pqt->disp_lock, NULL);
        pthread_cond_init(&pqt->disp_cond, NULL);

        pthread_attr_init(&thread_attrs);
        pthread_attr_setdetachstate(&thread_attrs, PTHREAD_CREATE_JOINABLE);
        pthread_create(&pqt->disp_thread, &thread_attrs,
                       pqt_display_thr, pqt);
    }

    *target = i;

    put_device(dev);

    return VDP_STATUS_OK;
}
