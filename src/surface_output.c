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

    put_device(dev);

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

    put_device(dev);

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

    put_device(dev);

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
        put_device(dev);
        return VDP_STATUS_INVALID_RGBA_FORMAT;
    }

    *surface = create_surface(dev, width, height, rgba_format, 1, 0);

    if (*surface == VDP_INVALID_HANDLE) {
        put_device(dev);
        return VDP_STATUS_RESOURCES;
    }

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_output_surface_destroy(VdpOutputSurface surface)
{
    tegra_surface *surf = get_surface_output(surface);

    if (surf == NULL) {
        return VDP_INVALID_HANDLE;
    }

    put_surface(surf);

    return destroy_surface(surf);
}

VdpStatus vdp_output_surface_get_parameters(VdpOutputSurface surface,
                                            VdpRGBAFormat *rgba_format,
                                            uint32_t *width, uint32_t *height)
{
    VdpBool stub;

    return vdp_bitmap_surface_get_parameters(surface, rgba_format,
                                             width, height, &stub);
}

VdpStatus vdp_output_surface_get_bits_native(VdpOutputSurface surface,
                                             VdpRect const *source_rect,
                                             void *const *destination_data,
                                             uint32_t const *destination_pitches)
{
    tegra_surface *surf = get_surface_output(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_surface(surf);

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
    tegra_surface *surf = get_surface_output(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_surface(surf);

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
    tegra_surface *surf = get_surface_output(surface);

    if (surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_surface(surf);

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
    tegra_surface *dst_surf = get_surface_output(destination_surface);
    tegra_surface *src_surf = get_surface(source_surface);
    tegra_surface *tmp_surf = NULL;
    tegra_shared_surface *shared;
    enum host1x_2d_rotate rotate;
    pixman_image_t *src_pix_region = NULL;
    pixman_image_t *src_pix;
    pixman_image_t *dst_pix;
    pixman_format_code_t pfmt;
    pixman_transform_t transform;
    void *dst_data;
    uint32_t *offset;
    uint32_t src_width, src_height;
    int16_t src_x0, src_y0;
    uint32_t dst_width, dst_height;
    int16_t dst_x0, dst_y0;
    uint32_t rot_width, rot_height;
    int need_scale = 0;
    int need_rotate = 0;
    int ret;

    if (dst_surf == NULL ||
            (src_surf == NULL && source_surface != VDP_INVALID_HANDLE))
    {
        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_INVALID_HANDLE;
    }

    if (source_rect != NULL) {
        src_width = source_rect->x1 - source_rect->x0;
        src_height = source_rect->y1 - source_rect->y0;
        src_x0 = source_rect->x0;
        src_y0 = source_rect->y0;
    } else {
        src_width = src_surf ? src_surf->width : 0;
        src_height = src_surf ? src_surf->height : 0;
        src_x0 = 0;
        src_y0 = 0;
    }

    if (destination_rect != NULL) {
        dst_width = destination_rect->x1 - destination_rect->x0;
        dst_height = destination_rect->y1 - destination_rect->y0;
        dst_x0 = destination_rect->x0;
        dst_y0 = destination_rect->y0;
    } else {
        dst_width = dst_surf->width;
        dst_height = dst_surf->height;
        dst_x0 = 0;
        dst_y0 = 0;
    }

    if (flags & ~3ul) {
        ErrorMsg("invalid flags %X\n", flags);
    }

    switch (flags & 3) {
    case VDP_OUTPUT_SURFACE_RENDER_ROTATE_90:
        rotate = ROT_270;
        rot_width = dst_height;
        rot_height = dst_width;
        need_rotate = 1;
        break;
    case VDP_OUTPUT_SURFACE_RENDER_ROTATE_180:
        rotate = ROT_180;
        rot_width = dst_width;
        rot_height = dst_height;
        need_rotate = 1;
        break;
    case VDP_OUTPUT_SURFACE_RENDER_ROTATE_270:
        rotate = ROT_270;
        rot_width = dst_height;
        rot_height = dst_width;
        need_rotate = 1;
        break;
    default:
        rotate = IDENTITY;
        break;
    }

    if (src_surf) {
        if (need_rotate) {
            shared = shared_surface_get(src_surf);

            if (shared) {
                if (src_x0 == shared->dst_x0 &&
                    src_y0 == shared->dst_y0 &&
                    src_width == shared->dst_width &&
                    src_height == shared->dst_height)
                {
                    DebugMsg("HW-offloaded video rotation\n");

                    tmp_surf = alloc_surface(src_surf->dev,
                                             rot_width, rot_height,
                                             src_surf->rgba_format, 0, 0);
                    if (tmp_surf) {
                        ret = host1x_gr2d_surface_blit(shared->video->dev->stream,
                                                       shared->video->pixbuf,
                                                       tmp_surf->pixbuf,
                                                       &shared->csc,
                                                       shared->src_x0,
                                                       shared->src_y0,
                                                       shared->src_width,
                                                       shared->src_height,
                                                       0,
                                                       0,
                                                       rot_width,
                                                       rot_height);
                        if (ret) {
                            ErrorMsg("video surface blitting failed %d\n", ret);
                            unref_surface(tmp_surf);
                            tmp_surf = NULL;
                        } else {
                            src_x0 = 0;
                            src_y0 = 0;
                        }
                    } else {
                        ErrorMsg("Failed to allocate tmp surface\n");
                    }
                } else {
                    DebugMsg("rotation can't be offloaded to HW\n");
                }

                unref_shared_surface(shared);
            } else {
                DebugMsg("HW-offloaded surface rotation\n");

                tmp_surf = alloc_surface(src_surf->dev, rot_width, rot_height,
                                         src_surf->rgba_format, 0, 0);
                if (tmp_surf) {
                    ret = host1x_gr2d_surface_blit(src_surf->dev->stream,
                                                   src_surf->pixbuf,
                                                   tmp_surf->pixbuf,
                                                   &csc_rgb_default,
                                                   src_x0,
                                                   src_y0,
                                                   src_width,
                                                   src_height,
                                                   0,
                                                   0,
                                                   rot_width,
                                                   rot_height);
                    if (ret) {
                        ErrorMsg("tmo surface blitting failed %d\n", ret);
                        unref_surface(tmp_surf);
                        tmp_surf = NULL;
                    } else {
                        src_x0 = 0;
                        src_y0 = 0;
                    }
                } else {
                    ErrorMsg("Failed to allocate tmp surface\n");
                }
            }
        }

        if (!tmp_surf) {
            ret = shared_surface_transfer_video(src_surf);
            if (ret) {
                put_surface(dst_surf);
                put_surface(src_surf);
                return VDP_STATUS_RESOURCES;
            }
        }
    }

    ret = shared_surface_transfer_video(dst_surf);
    if (ret) {
        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_RESOURCES;
    }

    if (source_surface == VDP_INVALID_HANDLE) {
        pthread_mutex_lock(&dst_surf->dev->lock);

        ret = host1x_gr2d_clear_rect(dst_surf->dev->stream,
                                     dst_surf->pixbuf,
                                     0xFFFFFFFF,
                                     dst_x0, dst_y0,
                                     dst_width, dst_height);

        pthread_mutex_unlock(&dst_surf->dev->lock);

        if (ret == 0) {
            put_surface(dst_surf);
            put_surface(src_surf);
            return VDP_STATUS_OK;
        }
    }

    if (source_surface == VDP_INVALID_HANDLE) {
        ret = map_surface_data(dst_surf);
        if (ret) {
            put_surface(dst_surf);
            put_surface(src_surf);
            return VDP_STATUS_RESOURCES;
        }

        dst_pix  = dst_surf->pix;
        dst_data = pixman_image_get_data(dst_pix);

        pfmt = pixman_image_get_format(dst_pix);

        ret = pixman_format_supported_destination(pfmt);
        if (!ret) {
            ErrorMsg("pixman_format_supported_destination failed\n");
        }

        ret = pixman_fill(dst_data,
                          pixman_image_get_stride(dst_pix) / 4,
                          PIXMAN_FORMAT_BPP(pfmt),
                          dst_x0, dst_y0,
                          dst_width, dst_height,
                          0xFFFFFFFF);
        if (!ret) {
            ErrorMsg("pixman_fill failed\n");
        }

        unmap_surface_data(dst_surf);

        put_surface(dst_surf);
        put_surface(src_surf);

        return VDP_STATUS_OK;
    }

    if (blend_state != NULL) {
        if (blend_state->struct_version != VDP_OUTPUT_SURFACE_RENDER_BLEND_STATE_VERSION) {
            put_surface(dst_surf);
            put_surface(src_surf);
            return VDP_STATUS_INVALID_STRUCT_VERSION;
        }
    }

    DebugMsg("src_width %u src_height %u src_x0 %u src_y0 %u dst_width %u dst_height %u dst_x0 %u dst_y0 %u\n",
             src_width, src_height, src_x0, src_y0, dst_width, dst_height, dst_x0, dst_y0);

    switch (flags & 3) {
    case VDP_OUTPUT_SURFACE_RENDER_ROTATE_90:
    case VDP_OUTPUT_SURFACE_RENDER_ROTATE_270:
        if (dst_width != src_height || dst_height != src_width) {
            need_scale = 1;
        }
        break;
    default:
        if (dst_width != src_width || dst_height != src_height) {
            need_scale = 1;
        }
        break;
    }

    if (!need_rotate || tmp_surf) {
        pthread_mutex_lock(&dst_surf->dev->lock);

        if (tmp_surf) {
            ret = host1x_gr2d_blit(tmp_surf->dev->stream,
                                   tmp_surf->pixbuf,
                                   dst_surf->pixbuf,
                                   rotate,
                                   src_x0, src_y0,
                                   dst_x0, dst_y0,
                                   rot_width, rot_height);

            unref_surface(tmp_surf);
        } else {
            ret = host1x_gr2d_surface_blit(dst_surf->dev->stream,
                                           src_surf->pixbuf,
                                           dst_surf->pixbuf,
                                           &csc_rgb_default,
                                           src_x0, src_y0,
                                           src_width, src_height,
                                           dst_x0, dst_y0,
                                           dst_width, dst_height);
        }
        if (ret) {
            ErrorMsg("surface copying failed %d\n", ret);
        }

        pthread_mutex_unlock(&dst_surf->dev->lock);

        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_OK;
    }

    ret = map_surface_data(dst_surf);
    if (ret) {
        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_RESOURCES;
    }

    ret = map_surface_data(src_surf);
    if (ret) {
        unmap_surface_data(dst_surf);

        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_RESOURCES;
    }

    dst_pix = dst_surf->pix;
    pfmt = pixman_image_get_format(dst_pix);
    ret = pixman_format_supported_destination(pfmt);
    if (!ret) {
        ErrorMsg("pixman_format_supported_destination failed\n");
    }

    src_pix = src_surf->pix;
    pfmt = pixman_image_get_format(src_pix);
    ret = pixman_format_supported_destination(pfmt);
    if (!ret) {
        ErrorMsg("pixman_format_supported_destination failed\n");
    }

    if (need_scale || need_rotate) {
        DebugMsg("need_scale %d need_rotate %d\n", need_scale, need_rotate);

        offset = pixman_image_get_data(src_pix);
        offset += src_x0;
        offset += src_surf->pixbuf->pitch / 4 * src_y0;

        src_pix_region = pixman_image_create_bits_no_clear(
            pfmt, src_width, src_height, offset, src_surf->pixbuf->pitch);

        if (!src_pix_region) {
            unmap_surface_data(dst_surf);
            unmap_surface_data(src_surf);

            put_surface(dst_surf);
            put_surface(src_surf);
            return VDP_STATUS_RESOURCES;
        }

        src_pix = src_pix_region;

        pixman_image_set_repeat(src_pix, PIXMAN_REPEAT_NORMAL);
        pixman_transform_init_identity(&transform);

        switch (flags & 3) {
        case VDP_OUTPUT_SURFACE_RENDER_ROTATE_90:
            ret = pixman_transform_rotate(&transform, NULL, 0, -pixman_fixed_1);
            break;
        case VDP_OUTPUT_SURFACE_RENDER_ROTATE_180:
            ret = pixman_transform_rotate(&transform, NULL, pixman_fixed_1, 0);
            break;
        case VDP_OUTPUT_SURFACE_RENDER_ROTATE_270:
            ret = pixman_transform_rotate(&transform, NULL, 0, pixman_fixed_1);
            break;
        default:
            ret = 1;
            break;
        }

        if (!ret) {
            ErrorMsg("pixman_transform_rotate failed\n");
        }

        if (need_scale) {
            double scalew = (double)src_width / (double)dst_width;
            double scaleh = (double)src_height / (double)dst_height;

            ret = pixman_transform_scale(&transform, NULL,
                                         pixman_double_to_fixed(scalew),
                                         pixman_double_to_fixed(scaleh));
            if (!ret) {
                ErrorMsg("pixman_transform_scale failed\n");
            }
        }

        ret = pixman_image_set_transform(src_pix, &transform);
        if (!ret) {
            ErrorMsg("pixman_image_set_transform failed\n");
        }

        src_x0 = 0;
        src_y0 = 0;
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
        if (!ret) {
            ErrorMsg("pixman_image_set_transform failed\n");
        }
    }

    if (src_pix_region) {
        pixman_image_unref(src_pix_region);
    }

    unmap_surface_data(dst_surf);
    unmap_surface_data(src_surf);

    put_surface(dst_surf);
    put_surface(src_surf);

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
