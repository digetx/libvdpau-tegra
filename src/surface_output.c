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

VdpStatus vdp_output_surface_query_capabilities(
                                            VdpDevice device,
                                            VdpRGBAFormat surface_rgba_format,
                                            VdpBool *is_supported,
                                            uint32_t *max_width,
                                            uint32_t *max_height)
{
    return vdp_bitmap_surface_query_capabilities(device,
                                                 surface_rgba_format,
                                                 is_supported,
                                                 max_width,
                                                 max_height);
}

VdpStatus vdp_output_surface_query_get_put_bits_native_capabilities(
                                        VdpDevice device,
                                        VdpRGBAFormat surface_rgba_format,
                                        VdpBool *is_supported)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    *is_supported = VDP_FALSE;

    return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_query_put_bits_indexed_capabilities(
                                        VdpDevice device,
                                        VdpRGBAFormat surface_rgba_format,
                                        VdpIndexedFormat bits_indexed_format,
                                        VdpColorTableFormat color_table_format,
                                        VdpBool *is_supported)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    *is_supported = VDP_FALSE;

    return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_query_put_bits_y_cb_cr_capabilities(
                                        VdpDevice device,
                                        VdpRGBAFormat surface_rgba_format,
                                        VdpYCbCrFormat bits_ycbcr_format,
                                        VdpBool *is_supported)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    *is_supported = VDP_FALSE;

    return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_create(VdpDevice device,
                                    VdpRGBAFormat rgba_format,
                                    uint32_t width, uint32_t height,
                                    VdpOutputSurface *surface)
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
        return VDP_STATUS_INVALID_RGBA_FORMAT;
    }

    *surface = create_surface(dev, width, height, rgba_format, 1, 0);

    if (*surface == VDP_INVALID_HANDLE) {
        return VDP_STATUS_RESOURCES;
    }

    return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_destroy(VdpOutputSurface surface)
{
    return vdp_bitmap_surface_destroy(surface);
}

VdpStatus vdp_output_surface_get_parameters(VdpOutputSurface surface,
                                            VdpRGBAFormat *rgba_format,
                                            uint32_t *width, uint32_t *height)
{
    tegra_surface *surf = get_surface(surface);
    VdpBool stub;

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    return vdp_bitmap_surface_get_parameters(surface, rgba_format,
                                             width, height, &stub);
}

VdpStatus vdp_output_surface_get_bits_native(VdpOutputSurface surface,
                                             VdpRect const *source_rect,
                                             void *const *destination_data,
                                             uint32_t const *destination_pitches)
{
    tegra_surface *surf = get_surface(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    return VDP_STATUS_NO_IMPLEMENTATION;
}

VdpStatus vdp_output_surface_put_bits_native(VdpOutputSurface surface,
                                             void const *const *source_data,
                                             uint32_t const *source_pitches,
                                             VdpRect const *destination_rect)
{
    return vdp_bitmap_surface_put_bits_native(surface, source_data,
                                              source_pitches, destination_rect);
}

VdpStatus vdp_output_surface_put_bits_indexed(
                                        VdpOutputSurface surface,
                                        VdpIndexedFormat source_indexed_format,
                                        void const *const *source_data,
                                        uint32_t const *source_pitch,
                                        VdpRect const *destination_rect,
                                        VdpColorTableFormat color_table_format,
                                        void const *color_table)
{
    tegra_surface *surf = get_surface(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    return VDP_STATUS_NO_IMPLEMENTATION;
}

VdpStatus vdp_output_surface_put_bits_y_cb_cr(
                                        VdpOutputSurface surface,
                                        VdpYCbCrFormat source_ycbcr_format,
                                        void const *const *source_data,
                                        uint32_t const *source_pitches,
                                        VdpRect const *destination_rect,
                                        VdpCSCMatrix const *csc_matrix)
{
    tegra_surface *surf = get_surface(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    return VDP_STATUS_NO_IMPLEMENTATION;
}

VdpStatus vdp_output_surface_render_bitmap_surface(
                            VdpOutputSurface destination_surface,
                            VdpRect const *destination_rect,
                            VdpBitmapSurface source_surface,
                            VdpRect const *source_rect,
                            VdpColor const *colors,
                            VdpOutputSurfaceRenderBlendState const *blend_state,
                            uint32_t flags)
{
    tegra_surface *dst_surf = get_surface(destination_surface);
    tegra_surface *src_surf;
    pixman_image_t *src_pix;
    pixman_image_t *dst_pix;
    pixman_format_code_t pfmt;
    pixman_transform_t transform;
    void *dst_data;
    uint32_t src_width, src_height;
    uint32_t src_x0, src_y0;
    uint32_t dst_width, dst_height;
    uint32_t dst_x0, dst_y0;
    int need_scale = 0;
    int need_rotate = 0;
    int ret;

    if (dst_surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    assert(dst_surf->idle_hack ||
           dst_surf->status == VDP_PRESENTATION_QUEUE_STATUS_IDLE);

    src_surf = get_surface(source_surface);

    dst_pix  = dst_surf->pix;
    dst_data = pixman_image_get_data(dst_pix);

    pfmt = pixman_image_get_format(dst_pix);
    ret = pixman_format_supported_destination(pfmt);

    assert(ret != 0);

    if (destination_rect != NULL) {
        dst_width = destination_rect->x1 - destination_rect->x0;
        dst_height = destination_rect->y1 - destination_rect->y0;
        dst_x0 = destination_rect->x0;
        dst_y0 = destination_rect->y0;
    } else {
        dst_width = pixman_image_get_width(dst_pix);
        dst_height = pixman_image_get_height(dst_pix);
        dst_x0 = 0;
        dst_y0 = 0;
    }

    if (source_surface == VDP_INVALID_HANDLE) {
        ret = host1x_gr2d_clear_rect(dst_surf->dev->stream,
                                     dst_surf->pixbuf,
                                     0xFFFFFFFF,
                                     dst_x0, dst_y0,
                                     dst_width, dst_height);
        if (ret == 0) {
            return VDP_STATUS_OK;
        }

        ret = pixman_fill(dst_data,
                          pixman_image_get_stride(dst_pix) / 4,
                          PIXMAN_FORMAT_BPP(pfmt),
                          dst_x0, dst_y0,
                          dst_width, dst_height,
                          0xFFFFFFFF);

        assert(ret != 0);

        return VDP_STATUS_OK;
    }

    if (blend_state != NULL) {
        if (blend_state->struct_version != VDP_OUTPUT_SURFACE_RENDER_BLEND_STATE_VERSION) {
            return VDP_STATUS_INVALID_STRUCT_VERSION;
        }
    }

    src_pix = src_surf->pix;

    pfmt = pixman_image_get_format(src_pix);
    ret = pixman_format_supported_destination(pfmt);

    assert(ret != 0);

    if (source_rect != NULL) {
        src_width = source_rect->x1 - source_rect->x0;
        src_height = source_rect->y1 - source_rect->y0;
        src_x0 = source_rect->x0;
        src_y0 = source_rect->y0;
    } else {
        src_width = pixman_image_get_width(src_pix);
        src_height = pixman_image_get_height(src_pix);
        src_x0 = 0;
        src_y0 = 0;
    }

    assert(!(flags & ~3ul));

    switch (flags & 3) {
    case VDP_OUTPUT_SURFACE_RENDER_ROTATE_90:
    case VDP_OUTPUT_SURFACE_RENDER_ROTATE_180:
    case VDP_OUTPUT_SURFACE_RENDER_ROTATE_270:
        need_rotate = 1;
        break;
    default:
        break;
    }

    if (dst_width != src_width || dst_height != src_height) {
        need_scale = 1;
    }

    if (!need_rotate) {
        host1x_gr2d_surface_blit(dst_surf->dev->stream,
                                 src_surf->pixbuf,
                                 dst_surf->pixbuf,
                                 NULL,
                                 src_x0, src_y0,
                                 src_width, src_height,
                                 dst_x0, dst_y0,
                                 dst_width, dst_height);
        return VDP_STATUS_OK;
    }

    if (need_scale || need_rotate) {
        pixman_transform_init_identity(&transform);

        switch (flags & 3) {
        case VDP_OUTPUT_SURFACE_RENDER_ROTATE_90:
            ret = pixman_transform_rotate(&transform, NULL, 0.0, 1.0);
            break;
        case VDP_OUTPUT_SURFACE_RENDER_ROTATE_180:
            ret = pixman_transform_rotate(&transform, NULL, -1.0, 0.0);
            break;
        case VDP_OUTPUT_SURFACE_RENDER_ROTATE_270:
            ret = pixman_transform_rotate(&transform, NULL, 0.0, -1.0);
            break;
        default:
            ret = 1;
            break;
        }

        assert(ret != 0);

        if (need_scale) {
            double scalew = (double)src_width / (double)dst_width;
            double scaleh = (double)src_height / (double)dst_height;

            ret = pixman_transform_scale(&transform, NULL,
                                         pixman_double_to_fixed(scalew),
                                         pixman_double_to_fixed(scaleh));

            assert(ret != 0);
        }

        ret = pixman_image_set_transform(src_pix, &transform);

        assert(ret != 0);
    }

    pixman_image_composite(PIXMAN_OP_SRC,
                           src_pix,
                           NULL,
                           dst_pix,
                           src_x0, src_y0,
                           0, 0,
                           dst_x0, dst_y0,
                           dst_width, dst_height);

    if (need_scale || need_rotate) {
        ret = pixman_image_set_transform(src_pix, NULL);
        assert(ret != 0);
    }

    return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_render_output_surface(
                            VdpOutputSurface destination_surface,
                            VdpRect const *destination_rect,
                            VdpOutputSurface source_surface,
                            VdpRect const *source_rect,
                            VdpColor const *colors,
                            VdpOutputSurfaceRenderBlendState const *blend_state,
                            uint32_t flags)
{
    return vdp_output_surface_render_bitmap_surface(destination_surface,
                                                    destination_rect,
                                                    source_surface,
                                                    source_rect,
                                                    colors,
                                                    blend_state,
                                                    flags);
}
