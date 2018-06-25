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

VdpStatus vdp_bitmap_surface_query_capabilities(
                                            VdpDevice device,
                                            VdpRGBAFormat surface_rgba_format,
                                            VdpBool *is_supported,
                                            uint32_t *max_width,
                                            uint32_t *max_height)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    switch (surface_rgba_format) {
    case VDP_RGBA_FORMAT_R8G8B8A8:
    case VDP_RGBA_FORMAT_B8G8R8A8:
        *is_supported = VDP_TRUE;
        break;
    default:
        *is_supported = VDP_FALSE;
        break;
    }

    *max_width = INT_MAX;
    *max_height = INT_MAX;

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_create(VdpDevice device,
                                    VdpRGBAFormat rgba_format,
                                    uint32_t width, uint32_t height,
                                    VdpBool frequently_accessed,
                                    VdpBitmapSurface *surface)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    switch (rgba_format) {
    case VDP_RGBA_FORMAT_R8G8B8A8:
    case VDP_RGBA_FORMAT_B8G8R8A8:
        break;
    default:
        put_device(dev);
        return VDP_STATUS_INVALID_RGBA_FORMAT;
    }

    *surface = create_surface(dev, width, height, rgba_format, 0, 0);

    if (*surface == VDP_INVALID_HANDLE) {
        put_device(dev);
        return VDP_STATUS_RESOURCES;
    }

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_destroy(VdpBitmapSurface surface)
{
    tegra_surface *surf = get_surface_bitmap(surface);

    if (surf == NULL) {
        return VDP_INVALID_HANDLE;
    }

    put_surface(surf);

    return destroy_surface(surf);
}

VdpStatus vdp_bitmap_surface_get_parameters(VdpBitmapSurface surface,
                                            VdpRGBAFormat *rgba_format,
                                            uint32_t *width, uint32_t *height,
                                            VdpBool *frequently_accessed)
{
    tegra_surface *surf = get_surface(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    *rgba_format = surf->rgba_format;
    *width = surf->width;
    *height = surf->height;
    *frequently_accessed = VDP_FALSE;

    put_surface(surf);

    return VDP_STATUS_OK;
}

VdpStatus vdp_bitmap_surface_put_bits_native(VdpBitmapSurface surface,
                                             void const *const *source_data,
                                             uint32_t const *source_pitches,
                                             VdpRect const *destination_rect)
{
    tegra_surface *surf;
    pixman_image_t *pix;
    pixman_format_code_t pfmt;
    pixman_bool_t ret;
    void *surf_data;
    uint32_t width = 0, height = 0;
    uint32_t x0 = 0, y0 = 0;
    int err;

    if (destination_rect) {
        width = destination_rect->x1 - destination_rect->x0;
        height = destination_rect->y1 - destination_rect->y0;
        x0 = destination_rect->x0;
        y0 = destination_rect->y0;

        if (!width || !height) {
            return VDP_STATUS_OK;
        }
    }

    surf = get_surface(surface);
    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    pthread_mutex_lock(&surf->lock);

    if (surf->flags & SURFACE_OUTPUT) {
        err = shared_surface_transfer_video(surf);
        if (err) {
                pthread_mutex_unlock(&surf->lock);
                put_surface(surf);
                return err;
        }

        /* XXX: check whether dirty data is overridden by surface blit */
        surf->data_dirty = true;
    }

    err = map_surface_data(surf);
    if (err) {
        pthread_mutex_unlock(&surf->lock);
        put_surface(surf);
        return VDP_STATUS_RESOURCES;
    }

    pix       = surf->pix;
    pfmt      = pixman_image_get_format(pix);
    surf_data = pixman_image_get_data(pix);

    assert(!(surf->flags & SURFACE_VIDEO));

    if (destination_rect == NULL) {
        width = pixman_image_get_width(pix);
        height = pixman_image_get_height(pix);
        x0 = 0;
        y0 = 0;
    }

    ret = pixman_blt((void *)source_data[0], surf_data,
                     source_pitches[0] / 4, pixman_image_get_stride(pix) / 4,
                     PIXMAN_FORMAT_BPP(pfmt), PIXMAN_FORMAT_BPP(pfmt),
                     0, 0,
                     x0, y0,
                     width, height);
    if (!ret) {
        ErrorMsg("pixman_blt failed\n");
    }

    host1x_pixelbuffer_check_guard(surf->pixbuf);

    unmap_surface_data(surf);

    pthread_mutex_unlock(&surf->lock);

    put_surface(surf);

    return VDP_STATUS_OK;
}
