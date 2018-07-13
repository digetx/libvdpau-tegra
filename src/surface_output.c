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
#include "shaders/blend_atop.bin.h"

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

static int blend_surface(tegra_device *dev,
                         tegra_surface *src_surf,
                         tegra_surface *dst_surf,
                         int16_t src_x0,
                         int16_t src_y0,
                         uint32_t src_width,
                         uint32_t src_height,
                         int16_t dst_x0,
                         int16_t dst_y0,
                         uint32_t dst_width,
                         uint32_t dst_height,
                         VdpColor const *colors,
                         uint32_t flags)
{
    struct tegra_stream *stream = &dst_surf->stream_3d;
    struct drm_tegra_bo *attribs_bo;
    __fp16 dst_left, dst_right, dst_top, dst_bottom;
    __fp16 src_left, src_right, src_top, src_bottom;
    __fp16 c[4][4];
    __fp16 tmp;
    __fp16 *map = NULL;
    unsigned attrib_itr = 0;
    unsigned i;
    int err;

    dst_left   = (__fp16) (dst_x0     * 2) / dst_surf->width  - 1.0f;
    dst_right  = (__fp16) (dst_width  * 2) / dst_surf->width  + dst_left;
    dst_bottom = (__fp16) (dst_y0     * 2) / dst_surf->height - 1.0f;
    dst_top    = (__fp16) (dst_height * 2) / dst_surf->height + dst_bottom;

    src_left   = (__fp16) src_x0     / src_surf->width;
    src_right  = (__fp16) src_width  / src_surf->width + src_left;
    src_bottom = (__fp16) src_y0     / src_surf->height;
    src_top    = (__fp16) src_height / src_surf->height + src_bottom;

    if (colors) {
        if (flags & VDP_OUTPUT_SURFACE_RENDER_COLOR_PER_VERTEX) {
            for (i = 0; i < 4; i++) {
                c[i][0] = colors[i].red;
                c[i][1] = colors[i].green;
                c[i][2] = colors[i].blue;
                c[i][3] = colors[i].alpha;
            }
        } else {
            for (i = 0; i < 4; i++) {
                c[i][0] = colors[0].red;
                c[i][1] = colors[0].green;
                c[i][2] = colors[0].blue;
                c[i][3] = colors[0].alpha;
            }
        }
    } else {
        for (i = 0; i < 4; i++) {
            c[i][0] = 1.0f;
            c[i][1] = 1.0f;
            c[i][2] = 1.0f;
            c[i][3] = 1.0f;
        }
    }

    switch (src_surf->rgba_format) {
    case VDP_RGBA_FORMAT_B8G8R8A8:
        for (i = 0; i < 4; i++) {
            tmp = c[i][0];
            c[i][0] = c[i][2];
            c[i][2] = tmp;
        }
        break;

    case VDP_RGBA_FORMAT_R8G8B8A8:
        break;

    default:
        return -EINVAL;
    }

    err = drm_tegra_bo_new(&attribs_bo, dev->drm, 0, 4096);
    if (err) {
        return err;
    }

    err = drm_tegra_bo_map(attribs_bo, (void**)&map);
    if (err) {
        goto out_unref;
    }

#define TegraPushVtxAttr2(x, y)         \
    map[attrib_itr++] = x;              \
    map[attrib_itr++] = y;

#define TegraPushVtxAttr4(x, y, z, w)   \
    map[attrib_itr++] = x;              \
    map[attrib_itr++] = y;              \
    map[attrib_itr++] = z;              \
    map[attrib_itr++] = w;

    /* push first triangle of the quad to the attributes buffer */
    TegraPushVtxAttr2(dst_left, dst_bottom);
    TegraPushVtxAttr4(c[3][0], c[3][1], c[3][2], c[3][3]);
    TegraPushVtxAttr2(src_left, src_bottom);

    TegraPushVtxAttr2(dst_left, dst_top);
    TegraPushVtxAttr4(c[0][0], c[0][1], c[0][2], c[0][3]);
    TegraPushVtxAttr2(src_left, src_top);

    TegraPushVtxAttr2(dst_right, dst_top);
    TegraPushVtxAttr4(c[1][0], c[1][1], c[1][2], c[1][3]);
    TegraPushVtxAttr2(src_right, src_top);

    /* push second */
    TegraPushVtxAttr2(dst_right, dst_top);
    TegraPushVtxAttr4(c[1][0], c[1][1], c[1][2], c[1][3]);
    TegraPushVtxAttr2(src_right, src_top);

    TegraPushVtxAttr2(dst_right, dst_bottom);
    TegraPushVtxAttr4(c[2][0], c[2][1], c[2][2], c[2][3]);
    TegraPushVtxAttr2(src_right, src_bottom);

    TegraPushVtxAttr2(dst_left, dst_bottom);
    TegraPushVtxAttr4(c[3][0], c[3][1], c[3][2], c[3][3]);
    TegraPushVtxAttr2(src_left, src_bottom);

    drm_tegra_bo_unmap(attribs_bo);

    err = tegra_stream_begin(stream);
    if (err) {
        goto out_unref;
    }

    tegra_stream_push_setclass(stream, HOST1X_CLASS_GR3D);

    host1x_gr3d_initialize(stream, &prog_blend_atop);

    host1x_gr3d_setup_scissor(stream, 0, 0, dst_surf->width, dst_surf->height);

    host1x_gr3d_setup_viewport_bias_scale(stream, 0.0f, 0.0f, 0.5f,
                                          dst_surf->width,
                                          dst_surf->height, 0.5f);

    host1x_gr3d_setup_render_target(stream, 1,
                                    dst_surf->bo,
                                    dst_surf->pixbuf->bo_offset[0],
                                    TGR3D_PIXEL_FORMAT_RGBA8888,
                                    dst_surf->pixbuf->pitch);

    host1x_gr3d_enable_render_targets(stream, 1 << 1);

    /* dst position */
    host1x_gr3d_setup_attribute(stream, 0, attribs_bo,
                                0, TGR3D_ATTRIB_TYPE_FLOAT16,
                                2, 16);

    /* colors */
    host1x_gr3d_setup_attribute(stream, 1, attribs_bo,
                                4, TGR3D_ATTRIB_TYPE_FLOAT16,
                                4, 16);

    /* src texcoords */
    host1x_gr3d_setup_attribute(stream, 2, attribs_bo,
                                12, TGR3D_ATTRIB_TYPE_FLOAT16,
                                2, 16);

    host1x_gr3d_setup_texture_desc(stream, 0,
                                   src_surf->bo,
                                   src_surf->pixbuf->bo_offset[0],
                                   src_surf->width,
                                   src_surf->height,
                                   TGR3D_PIXEL_FORMAT_RGBA8888,
                                   false, false, false,
                                   true, false);

    host1x_gr3d_upload_const_vp(stream, 0, 0.0f, 0.0f, 0.0f, 1.0f);

    host1x_gr3d_setup_draw_params(stream, TGR3D_PRIMITIVE_TYPE_TRIANGLES,
                                  TGR3D_INDEX_MODE_NONE, 0);

    host1x_gr3d_draw_primitives(stream, 0, 6);

    err = tegra_stream_end(stream);
    if (err) {
        goto out_unref;
    }

    err = tegra_stream_flush(stream);
    if (err) {
        goto out_unref;
    }

    host1x_pixelbuffer_check_guard(dst_surf->pixbuf);

out_unref:
    drm_tegra_bo_unref(attribs_bo);

    return err;
}

static VdpStatus surface_render_bitmap_surface(
                            tegra_surface *dst_surf,
                            VdpRect const *destination_rect,
                            tegra_surface *src_surf,
                            VdpRect const *source_rect,
                            VdpColor const *colors,
                            VdpOutputSurfaceRenderBlendState const *blend_state,
                            uint32_t flags)
{
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
    uint32_t clear_color = 0xFFFFFFFF;
    int need_scale = 0;
    int need_rotate = 0;
    int ret;

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
        rotate = ROT_90;
        rot_width = dst_height;
        rot_height = dst_width;
        need_rotate = 1;
        break;
    default:
        rotate = IDENTITY;
        rot_width = dst_width;
        rot_height = dst_height;
        break;
    }

    if (src_surf) {
        pthread_mutex_lock(&src_surf->lock);

        shared = shared_surface_get(src_surf);
        if (!shared && !src_surf->data_allocated) {
            put_surface(src_surf);
            clear_color = 0;
            src_surf = NULL;
        }

        pthread_mutex_unlock(&src_surf->lock);

        if (src_surf == NULL) {
            goto out_1;
        }

        if (need_rotate) {
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
                        ret = host1x_gr2d_surface_blit(&tmp_surf->stream_2d,
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
            } else {
                DebugMsg("HW-offloaded surface rotation\n");

                tmp_surf = alloc_surface(src_surf->dev, rot_width, rot_height,
                                         src_surf->rgba_format, 0, 0);
                if (tmp_surf) {
                    ret = host1x_gr2d_surface_blit(&tmp_surf->stream_2d,
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

        if (shared) {
            unref_shared_surface(shared);
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

out_1:
    pthread_mutex_lock(&dst_surf->lock);

    if (src_surf == NULL && clear_color == 0) {
        shared = shared_surface_get(dst_surf);

        if (!shared && !dst_surf->data_allocated) {
            pthread_mutex_unlock(&dst_surf->lock);
            put_surface(dst_surf);
            return VDP_STATUS_OK;
        }

        if (shared){
            unref_shared_surface(shared);
        }
    }

    ret = shared_surface_transfer_video(dst_surf);
    if (ret) {
        pthread_mutex_unlock(&dst_surf->lock);
        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_RESOURCES;
    }

    ret = dynamic_alloc_surface_data(dst_surf);
    if (ret) {
        pthread_mutex_unlock(&dst_surf->lock);
        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_RESOURCES;
    }

    if (src_surf == NULL) {
        ret = host1x_gr2d_clear_rect(&dst_surf->stream_2d,
                                     dst_surf->pixbuf,
                                     clear_color,
                                     dst_x0, dst_y0,
                                     dst_width, dst_height);

        if (ret == 0) {
            pthread_mutex_unlock(&dst_surf->lock);
            put_surface(dst_surf);
            return VDP_STATUS_OK;
        }
    }

    if (src_surf == NULL) {
        ret = map_surface_data(dst_surf);
        if (ret) {
            pthread_mutex_unlock(&dst_surf->lock);
            put_surface(dst_surf);
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

        pthread_mutex_unlock(&dst_surf->lock);
        put_surface(dst_surf);
        put_surface(src_surf);

        return VDP_STATUS_OK;
    }

    if (blend_state != NULL) {
        if (blend_state->struct_version != VDP_OUTPUT_SURFACE_RENDER_BLEND_STATE_VERSION) {
            pthread_mutex_unlock(&dst_surf->lock);
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
        if (src_surf->rgba_format == dst_surf->rgba_format &&
            blend_state &&
            blend_state->blend_factor_source_color      == VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE &&
            blend_state->blend_factor_destination_color == VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA &&
            blend_state->blend_factor_source_alpha      == VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ZERO &&
            blend_state->blend_factor_destination_alpha == VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ZERO &&
            blend_state->blend_equation_color           == VDP_OUTPUT_SURFACE_RENDER_BLEND_EQUATION_ADD &&
            blend_state->blend_equation_alpha           == VDP_OUTPUT_SURFACE_RENDER_BLEND_EQUATION_ADD)
        {
            if (tmp_surf) {
                ret = blend_surface(dst_surf->dev, src_surf, tmp_surf,
                                    src_x0, src_y0, rot_width, rot_height,
                                    dst_x0, dst_y0, rot_width, rot_height,
                                    colors, flags);

                unref_surface(tmp_surf);
            } else {
                ret = blend_surface(dst_surf->dev, src_surf, dst_surf,
                                    src_x0, src_y0, src_width, src_height,
                                    dst_x0, dst_y0, dst_width, dst_height,
                                    colors, flags);
            }
        } else {
            if (tmp_surf) {
                ret = host1x_gr2d_blit(&tmp_surf->stream_2d,
                                       tmp_surf->pixbuf,
                                       dst_surf->pixbuf,
                                       rotate,
                                       src_x0, src_y0,
                                       dst_x0, dst_y0,
                                       rot_width, rot_height);

                unref_surface(tmp_surf);
            } else {
                ret = host1x_gr2d_surface_blit(&dst_surf->stream_2d,
                                               src_surf->pixbuf,
                                               dst_surf->pixbuf,
                                               &csc_rgb_default,
                                               src_x0, src_y0,
                                               src_width, src_height,
                                               dst_x0, dst_y0,
                                               dst_width, dst_height);
            }
        }

        if (ret) {
            ErrorMsg("surface copying failed %d\n", ret);
        }

        pthread_mutex_unlock(&dst_surf->lock);
        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_OK;
    }

    ret = map_surface_data(dst_surf);
    if (ret) {
        pthread_mutex_unlock(&dst_surf->lock);
        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_RESOURCES;
    }

    ret = map_surface_data(src_surf);
    if (ret) {
        unmap_surface_data(dst_surf);

        pthread_mutex_unlock(&dst_surf->lock);
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

            pthread_mutex_unlock(&dst_surf->lock);
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

    pthread_mutex_unlock(&dst_surf->lock);
    put_surface(dst_surf);
    put_surface(src_surf);

    return VDP_STATUS_OK;
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
    tegra_surface *src_surf = get_surface_bitmap(source_surface);

    if (dst_surf == NULL ||
            (src_surf == NULL && source_surface != VDP_INVALID_HANDLE))
    {
        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_INVALID_HANDLE;
    }

    return surface_render_bitmap_surface(dst_surf,
                                         destination_rect,
                                         src_surf,
                                         source_rect,
                                         colors,
                                         blend_state,
                                         flags);
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
    tegra_surface *dst_surf = get_surface_output(destination_surface);
    tegra_surface *src_surf = get_surface_output(source_surface);

    if (dst_surf == NULL ||
            (src_surf == NULL && source_surface != VDP_INVALID_HANDLE))
    {
        put_surface(dst_surf);
        put_surface(src_surf);
        return VDP_STATUS_INVALID_HANDLE;
    }

    return surface_render_bitmap_surface(dst_surf,
                                         destination_rect,
                                         src_surf,
                                         source_rect,
                                         colors,
                                         blend_state,
                                         flags);
}
