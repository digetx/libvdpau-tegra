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
    unsigned int format;
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

    switch (buf[0].cpp) {
    case 4:
        format = PIX_BUF_FMT_ARGB8888;
        break;

    case 2:
        format = PIX_BUF_FMT_RGB565;
        break;

    default:
        ErrorMsg("Unsupported CPP %u\n", buf[0].cpp);
        pqt_destroy_dri2_drawable(pqt);

        if (!tegra_vdpau_force_xv) {
            DebugMsg("forcing Xv output\n");
            tegra_vdpau_force_xv = true;
            tegra_vdpau_force_dri = false;
            tegra_vdpau_dri_xv_autoswitch = false;
        }

        return -1;
    }

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
                                              buf[0].pitch[0], 0, format,
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

    DRI2GetMSC(dev->display, pqt->drawable, &ust, &msc, &sbc);
    DRI2SwapBuffers(dev->display, pqt->drawable, msc + 1, 0, 0, &count);
    DRI2WaitMSC(dev->display, pqt->drawable, msc + 1, 0, 0, &ust, &msc, &sbc);

    if (pqt->dri_prep_surf == surf) {
        pqt->dri_prep_surf = NULL;
    }

    if (surf->set_bg) {
        pqt->bg_new_state.bg_color = surf->bg_color;
    }
}

static bool pqt_update_background_state(tegra_pqt *pqt, tegra_surface *surf)
{
    if (surf->shared) {
        pqt->bg_new_state.surf_x = surf->shared->dst_x0;
        pqt->bg_new_state.surf_y = surf->shared->dst_y0;
        pqt->bg_new_state.surf_w = surf->shared->dst_width;
        pqt->bg_new_state.surf_h = surf->shared->dst_height;
        pqt->bg_new_state.shared = true;
    } else {
        pqt->bg_new_state.shared = false;
    }

    if (surf->set_bg) {
        pqt->bg_new_state.bg_color = surf->bg_color;
    }

    pqt->bg_new_state.disp_w = surf->disp_width;
    pqt->bg_new_state.disp_h = surf->disp_height;

    if (memcmp(&pqt->bg_new_state, &pqt->bg_old_state,
               sizeof(pqt->bg_new_state)) != 0) {

        return true;
    }

    return false;
}

static void pqt_draw_borders(tegra_pqt *pqt, tegra_surface *surf)
{
    tegra_device *dev = pqt->dev;
    struct tegra_pqt_bg_state *bgs;
    int32_t w_right;
    int32_t w_left;
    int32_t h_top;
    int32_t h_bottom;

    bgs = &pqt->bg_new_state;

    if (!bgs->shared) {
        return;
    }

    w_left   = bgs->surf_x;
    w_right  = bgs->disp_w - bgs->surf_x - bgs->surf_w;
    h_top    = bgs->surf_y;
    h_bottom = bgs->disp_h - bgs->surf_y - bgs->surf_h;

    if (w_right < 0 || h_bottom < 0) {
        return;
    }

    if (w_left || w_right || h_top || h_bottom) {
        XSetForeground(dev->display, pqt->gc, bgs->bg_color);
    }

    if (w_left) {
        XFillRectangle(dev->display, pqt->drawable, pqt->gc,
                       0, 0, w_left, bgs->disp_h);
    }

    if (w_right) {
        XFillRectangle(dev->display, pqt->drawable, pqt->gc,
                       bgs->surf_x + bgs->surf_w,
                       0, w_right, bgs->disp_h);
    }

    if (h_top) {
        XFillRectangle(dev->display, pqt->drawable, pqt->gc,
                       0, 0, bgs->disp_w , h_top);
    }

    if (h_bottom) {
        XFillRectangle(dev->display, pqt->drawable, pqt->gc,
                       0, bgs->surf_y + bgs->surf_h,
                       bgs->disp_w , h_bottom);
    }
}

static void pqt_draw_background(tegra_pqt *pqt, tegra_surface *surf)
{
    tegra_device *dev = pqt->dev;
    uint32_t colorkey = 0;
    int val, ret;

    if (surf->rgba_format == VDP_RGBA_FORMAT_R8G8B8A8) {
        colorkey |= (pqt->bg_new_state.colorkey & 0xff00ff00);
        colorkey |= (pqt->bg_new_state.colorkey & 0xff0000) >> 16;
        colorkey |= (pqt->bg_new_state.colorkey & 0x0000ff) << 16;
    } else {
        colorkey = pqt->bg_new_state.colorkey;
    }

    XSetWindowBackground(dev->display, pqt->drawable, colorkey);
    XClearWindow(dev->display, pqt->drawable);

    if (pqt->xv_ckey_atom != None &&
        pqt->bg_new_state.colorkey != pqt->bg_old_state.colorkey)
    {
        ret = XvSetPortAttribute(dev->display, dev->xv_port,
                                 pqt->xv_ckey_atom,
                                 pqt->bg_new_state.colorkey);
        if (ret != Success) {
            ErrorMsg("failed to set Xv colorkey %d\n", ret);

            tegra_vdpau_force_xv = false;

            if (!tegra_vdpau_force_dri) {
                DebugMsg("Forcing DRI output\n");
                tegra_vdpau_force_dri = true;
            }
        } else {
            ret = XvGetPortAttribute(dev->display, dev->xv_port,
                                     pqt->xv_ckey_atom, &val);
            if (ret != Success) {
                ErrorMsg("failed to get Xv colorkey %d\n", ret);
            } else if (val != pqt->bg_new_state.colorkey) {
                ErrorMsg("failed to set Xv colorkey, not changed\n");
            } else {
                DebugMsg("Xv colorkey changed to %08X\n", val);
            }
        }
    }
}

static void wait_for_vblank(tegra_device *dev, int secondary)
{
    drmVBlank vbl;
    int err;

    memset(&vbl, 0, sizeof(vbl));
    vbl.request.type = DRM_VBLANK_RELATIVE;
    vbl.request.sequence = 1;
    vbl.request.signal = 0;

    if (secondary)
        vbl.request.type |= DRM_VBLANK_SECONDARY;

    err = drmWaitVBlank(dev->drm_fd, &vbl);
    if (err) {
        DebugMsg("drmWaitVBlank() failed: %d\n", err);
    }
}

static void pqt_display_xv(tegra_pqt *pqt, tegra_surface *surf, bool block)
{
    tegra_device *dev = pqt->dev;
    bool no_surf = false;
    bool upd_bg;

    upd_bg = pqt_update_background_state(pqt, surf);

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

        tegra_xv_apply_csc(dev, &surf->shared->csc);
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
        no_surf = true;
    }

    if (upd_bg) {
        pqt_draw_background(pqt, surf);
        pqt_draw_borders(pqt, surf);
        pqt->bg_old_state = pqt->bg_new_state;
    }

    if (no_surf) {
        return;
    }

    XSync(dev->display, 0);

    if (dev->xv_v2 && block) {
        TegraXvVdpauInfo vdpau_info = { 0 };
        VdpTime time = 0;
        int32_t val;
        int ret;

        ret = XvGetPortAttribute(dev->display, dev->xv_port, dev->xvVdpauInfo,
                                 &val);
        if (ret != Success || !val) {
            DebugMsg("failed to get XV_TEGRA_VDPAU_INFO %d val %d\n", ret, val);
        } else {
            vdpau_info.data = val;
        }

        DebugMsg("vdpau_info.visible %u vdpau_info.crtc_pipe %u\n",
                 vdpau_info.visible, vdpau_info.crtc_pipe);

        if (tegra_vdpau_debug)
            time = get_time();

        wait_for_vblank(dev, vdpau_info.crtc_pipe);

        DebugMsg("waited for VBLANK %llu usec\n", (get_time() - time) / 1000);
    }
}

static void transit_display_to_xv(tegra_pqt *pqt)
{
    tegra_surface *surf = pqt->disp_surf;

    if (surf)
        DebugMsg("surface %u\n", surf->surface_id);

    pqt_destroy_dri2_drawable(pqt);

    pqt->disp_state = TEGRA_PQT_XV;
}

static void transit_display_to_dri(tegra_pqt *pqt)
{
    tegra_surface *surf = pqt->disp_surf;
    tegra_device *dev = pqt->dev;

    if (surf)
        DebugMsg("surface %u\n", surf->surface_id);

    XvStopVideo(dev->display, dev->xv_port, pqt->drawable);
    memset(&pqt->bg_old_state, 0, sizeof(pqt->bg_old_state));
    tegra_xv_reset_csc(dev);

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

    if (surf->shared) {
        DebugMsg("surface %u transfer YUV\n", surf->surface_id);

        if (surf->set_bg) {
            ret = host1x_gr2d_clear_rect_clipped(surf->stream_2d,
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

        ret = host1x_gr2d_surface_blit(surf->stream_2d,
                                       surf->shared->video->pixbuf,
                                       pqt->dri_pixbuf,
                                       &surf->shared->csc.gr2d,
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

        if (surf->pixbuf->format == pqt->dri_pixbuf->format)
            ret = host1x_gr2d_blit(surf->stream_2d,
                                   surf->pixbuf,
                                   pqt->dri_pixbuf,
                                   IDENTITY,
                                   0,
                                   0,
                                   0,
                                   0,
                                   surf->disp_width,
                                   surf->disp_height);
        else
            ret = host1x_gr2d_surface_blit(surf->stream_2d,
                                           surf->pixbuf,
                                           pqt->dri_pixbuf,
                                           &csc_rgb_default,
                                           0,
                                           0,
                                           surf->disp_width,
                                           surf->disp_height,
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
        pqt_display_xv(pqt, surf, update_status);

        if (transit || pqt->disp_state != TEGRA_PQT_XV) {
            transit_display_to_xv(pqt);
        }
    }

    if (update_status) {
        surf->first_presentation_time = get_time();
        surf->status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;
    }

    pthread_mutex_unlock(&surf->lock);

    if (pqt->disp_surf != surf) {
        pqt_display_surface_to_idle_state(pqt);
        pqt->disp_surf = surf;
    }

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

    if (pqt->threads_running) {
        pthread_join(pqt->x11_thread, NULL);

        pthread_mutex_lock(&pqt->disp_lock);
        pthread_cond_signal(&pqt->disp_cond);
        pthread_mutex_unlock(&pqt->disp_lock);

        pthread_join(pqt->disp_thread, NULL);
    }

    if (pqt->disp_state == TEGRA_PQT_XV)
        XvStopVideo(dev->display, dev->xv_port, pqt->drawable);

    if (pqt->disp_state == TEGRA_PQT_DRI)
        pqt_destroy_dri2_drawable(pqt);

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
    int val, ret;

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

    /* for some odd reason first X11 events may be dropped, pausing helps */
    usleep(100000);

    pthread_mutexattr_init(&mutex_attrs);
    pthread_mutexattr_settype(&mutex_attrs, PTHREAD_MUTEX_RECURSIVE);

    pthread_mutex_init(&pqt->lock, &mutex_attrs);
    atomic_set(&pqt->refcnt, 1);
    ref_device(dev);
    pqt->dev = dev;
    pqt->drawable = drawable;
    pqt->gc = XCreateGC(dev->display, drawable, 0, &values);
    pqt->bg_new_state.colorkey = 0x200507;

    XGetWindowAttributes(dev->display, drawable, &get);
    set.event_mask  = get.all_event_masks;
    set.event_mask |= VisibilityChangeMask;
    set.event_mask |= StructureNotifyMask;
    set.backing_store = Always;
    XChangeWindowAttributes(dev->display, drawable,
                            CWEventMask | CWBackingStore, &set);

    XSetWindowBackground(dev->display, drawable, 0x000000);
    XClearWindow(dev->display, drawable);

    if ((!tegra_vdpau_force_dri || dev->disp_composited) ||
            (!tegra_vdpau_dri_xv_autoswitch && !dev->disp_rotated)) {
        if (tegra_check_xv_atom(dev, "XV_COLORKEY"))
            pqt->xv_ckey_atom = XInternAtom(dev->display, "XV_COLORKEY", false);

        if (pqt->xv_ckey_atom != None) {
            ret = XvGetPortAttribute(dev->display, dev->xv_port,
                                     pqt->xv_ckey_atom, &val);
            if (ret != Success)
                pqt->xv_ckey_atom = None;
        }

        if (pqt->xv_ckey_atom != None) {
            if (dev->disp_composited)
                tegra_vdpau_force_dri = false;

            if (!tegra_vdpau_force_xv && !tegra_vdpau_force_dri) {
                DebugMsg("Color keying support detected, forcing Xv output\n");
                tegra_vdpau_force_xv = true;
                tegra_vdpau_force_dri = false;
                tegra_vdpau_dri_xv_autoswitch = false;
            }
        } else {
            ErrorMsg("XV_COLORKEY not available, update Opentegra Xorg driver and/or Linux kernel to get colorkey support\n");
        }
    }

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

        pqt->threads_running = true;
    }

    *target = i;

    put_device(dev);

    return VDP_STATUS_OK;
}
