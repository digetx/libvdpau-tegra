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
        return VDP_STATUS_INVALID_CHROMA_TYPE;
    }

    *surface = create_surface(dev, width, height, PIXMAN_x8r8g8b8, 0, 1);

    if (*surface == VDP_INVALID_HANDLE) {
        return VDP_STATUS_RESOURCES;
    }

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_destroy(VdpVideoSurface surface)
{
    return vdp_bitmap_surface_destroy(surface);
}

VdpStatus vdp_video_surface_get_parameters(VdpVideoSurface surface,
                                           VdpChromaType *chroma_type,
                                           uint32_t *width, uint32_t *height)
{
    tegra_surface *surf = get_surface(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    assert(surf->flags & SURFACE_VIDEO);

    if (chroma_type != NULL) {
        *chroma_type = VDP_CHROMA_TYPE_420;
    }

    if (width != NULL) {
        *width = pixman_image_get_width(surf->pix);
    }

    if (width != NULL) {
        *height = pixman_image_get_height(surf->pix);
    }

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_get_bits_y_cb_cr(
                                        VdpVideoSurface surface,
                                        VdpYCbCrFormat destination_ycbcr_format,
                                        void *const *destination_data,
                                        uint32_t const *destination_pitches)
{
    tegra_surface *surf = get_surface(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    return VDP_STATUS_NO_IMPLEMENTATION;
}

VdpStatus vdp_video_surface_put_bits_y_cb_cr(
                                            VdpVideoSurface surface,
                                            VdpYCbCrFormat source_ycbcr_format,
                                            void const *const *source_data,
                                            uint32_t const *source_pitches)
{
    tegra_surface *surf = get_surface(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    return VDP_STATUS_NO_IMPLEMENTATION;
}
