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

VdpStatus vdp_video_surface_query_capabilities(
                                            VdpDevice device,
                                            VdpChromaType surface_chroma_type,
                                            VdpBool *is_supported,
                                            uint32_t *max_width,
                                            uint32_t *max_height)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    *is_supported = (surface_chroma_type == VDP_CHROMA_TYPE_420);
    *max_width = INT_MAX;
    *max_height = INT_MAX;

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_query_get_put_bits_y_cb_cr_capabilities(
                                            VdpDevice device,
                                            VdpChromaType surface_chroma_type,
                                            VdpYCbCrFormat bits_ycbcr_format,
                                            VdpBool *is_supported)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    *is_supported = (bits_ycbcr_format == VDP_YCBCR_FORMAT_YV12);

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_create(VdpDevice device,
                                   VdpChromaType chroma_type,
                                   uint32_t width, uint32_t height,
                                   VdpVideoSurface *surface)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    if (chroma_type != VDP_CHROMA_TYPE_420) {
        put_device(dev);
        return VDP_STATUS_INVALID_CHROMA_TYPE;
    }

    *surface = create_surface(dev, width, height, ~0, 0, 1);

    if (*surface == VDP_INVALID_HANDLE) {
        put_device(dev);
        return VDP_STATUS_RESOURCES;
    }

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_destroy(VdpVideoSurface surface)
{
    tegra_surface *surf = get_surface_video(surface);
    uint32_t flags;

    if (surf == NULL) {
        return VDP_INVALID_HANDLE;
    }

    flags = surf->flags;
    surf->v4l2.buf_idx = -1;

    put_surface(surf);

    if (!(flags & SURFACE_VIDEO)) {
        return VDP_INVALID_HANDLE;
    }

    return destroy_surface(surf);
}

VdpStatus vdp_video_surface_get_parameters(VdpVideoSurface surface,
                                           VdpChromaType *chroma_type,
                                           uint32_t *width, uint32_t *height)
{
    tegra_surface *surf = get_surface_video(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    assert(surf->flags & SURFACE_VIDEO);

    if (chroma_type != NULL) {
        *chroma_type = VDP_CHROMA_TYPE_420;
    }

    if (width != NULL) {
        *width = surf->width;
    }

    if (width != NULL) {
        *height = surf->height;
    }

    put_surface(surf);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_get_bits_y_cb_cr(
                                        VdpVideoSurface surface,
                                        VdpYCbCrFormat destination_ycbcr_format,
                                        void *const *destination_data,
                                        uint32_t const *destination_pitches)
{
    tegra_surface *surf = get_surface_video(surface);
    void *dst_y  = destination_data[0];
    void *dst_cr = destination_data[1];
    void *dst_cb = destination_data[2];
    int width, height;
    int ret;

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    assert(surf->flags & SURFACE_VIDEO);

    pthread_mutex_lock(&surf->lock);

    switch (destination_ycbcr_format) {
    case VDP_YCBCR_FORMAT_YV12:
        break;
    default:
        pthread_mutex_unlock(&surf->lock);
        put_surface(surf);
        return VDP_STATUS_NO_IMPLEMENTATION;
    }

    ret = map_surface_data(surf);
    if (ret) {
        pthread_mutex_unlock(&surf->lock);
        put_surface(surf);
        return ret;
    }

    width  = surf->width;
    height = surf->height;

    /* Copy luma plane.  */
    ret = pixman_blt(surf->y_data, dst_y,
                     surf->pixbuf->pitch / 4, destination_pitches[0] / 4,
                     8, 8,
                     0, 0,
                     0, 0,
                     width, height);
    if (!ret) {
        ErrorMsg("pixman_blt failed\n");
    }

    /* Copy chroma blue plane.  */
    ret = pixman_blt(surf->cb_data, dst_cb,
                     surf->pixbuf->pitch_uv / 4, destination_pitches[1] / 4,
                     8, 8,
                     0, 0,
                     0, 0,
                     width / 2, height / 2);
    if (!ret) {
        ErrorMsg("pixman_blt failed\n");
    }

    /* Copy chroma red plane.  */
    ret = pixman_blt(surf->cr_data, dst_cr,
                     surf->pixbuf->pitch_uv / 4, destination_pitches[2] / 4,
                     8, 8,
                     0, 0,
                     0, 0,
                     width / 2, height / 2);
    if (!ret) {
        ErrorMsg("pixman_blt failed\n");
    }

    unmap_surface_data(surf);

    pthread_mutex_unlock(&surf->lock);
    put_surface(surf);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_put_bits_y_cb_cr(
                                            VdpVideoSurface surface,
                                            VdpYCbCrFormat source_ycbcr_format,
                                            void const *const *source_data,
                                            uint32_t const *source_pitches)
{
    tegra_surface *surf, *orig = get_surface_video(surface);
    void *src_y  = (void *)source_data[0];
    void *src_cr = (void *)source_data[1];
    void *src_cb = (void *)source_data[2];
    int width, height;
    int ret;

    if (orig == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    assert(orig->flags & SURFACE_VIDEO);

    switch (source_ycbcr_format) {
    case VDP_YCBCR_FORMAT_YV12:
        break;
    default:
        put_surface(orig);
        return VDP_STATUS_NO_IMPLEMENTATION;
    }

    surf = shared_surface_swap_video(orig);

    if (orig != surf) {
        put_surface(orig);
        ref_surface(surf);
    }

    ret = map_surface_data(surf);
    if (ret) {
        put_surface(surf);
        return ret;
    }

    width  = surf->width;
    height = surf->height;

    /* Copy luma plane.  */
    ret = pixman_blt(src_y, surf->y_data,
                     source_pitches[0] / 4, surf->pixbuf->pitch / 4,
                     8, 8,
                     0, 0,
                     0, 0,
                     width, height);
    if (!ret) {
        ErrorMsg("pixman_blt failed\n");
    }

    /* Copy chroma blue plane.  */
    ret = pixman_blt(src_cb, surf->cb_data,
                     source_pitches[1] / 4, surf->pixbuf->pitch_uv / 4,
                     8, 8,
                     0, 0,
                     0, 0,
                     width / 2, height / 2);
    if (!ret) {
        ErrorMsg("pixman_blt failed\n");
    }

    /* Copy chroma red plane.  */
    ret = pixman_blt(src_cr, surf->cr_data,
                     source_pitches[2] / 4, surf->pixbuf->pitch_uv / 4,
                     8, 8,
                     0, 0,
                     0, 0,
                     width / 2, height / 2);
    if (!ret) {
        ErrorMsg("pixman_blt failed\n");
    }

    host1x_pixelbuffer_check_guard(surf->pixbuf);

    unmap_surface_data(surf);

    put_surface(surf);

    return VDP_STATUS_OK;
}
