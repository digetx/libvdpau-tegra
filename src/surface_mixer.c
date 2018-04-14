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

static bool custom_csc(VdpCSCMatrix const csc_matrix)
{
    int i, k;

    if (memcmp(csc_matrix, CSC_BT_601, sizeof(VdpCSCMatrix)) == 0 ||
            memcmp(csc_matrix, CSC_BT_709, sizeof(VdpCSCMatrix)) == 0)
                return false;

    for (i = 0; i < 3; i++)
        for (k = 0; k < 3; k++)
            if (fabs(csc_matrix[i][k] - CSC_BT_601[i][k]) > 0.01f)
                goto check_709;

    return false;

    /* XXX: Tegra's CSC is hardcoded to BT601 in the kernel driver */
check_709:
    for (i = 0; i < 3; i++)
        for (k = 0; k < 3; k++)
            if (fabs(csc_matrix[i][k] - CSC_BT_709[i][k]) > 0.01f)
                return true;

    return false;
}

static bool mixer_apply_vdp_csc_to_xv(tegra_mixer *mix,
                                      VdpCSCMatrix const cscmat)
{
    tegra_device *dev = mix->dev;
    uint32_t val0, val1;

    if (!tegra_xv_initialize_csc(dev))
        return false;

    val0 = mix->csc.gr2d.yos & 0xff;
    val1 = FLOAT_TO_FIXED_s2_8( CLAMP(cscmat[0][0], 0.0f, 1.98f) );

    mix->csc.xv.yof_kyrgb = (val1 << 16) | val0;

    val0 = FLOAT_TO_FIXED_s2_8( CLAMP(cscmat[0][1], -3.98f, 3.98f) );
    val1 = FLOAT_TO_FIXED_s2_8( CLAMP(cscmat[0][2], -3.98f, 3.98f) );

    mix->csc.xv.kur_kvr = (val1 << 16) | val0;

    val0 = FLOAT_TO_FIXED_s1_8( CLAMP(cscmat[1][1], -1.98f, 1.98f) );
    val1 = FLOAT_TO_FIXED_s1_8( CLAMP(cscmat[1][2], -1.98f, 1.98f) );

    mix->csc.xv.kug_kvg = (val1 << 16) | val0;

    val0 = FLOAT_TO_FIXED_s2_8( CLAMP(cscmat[2][1], -3.98f, 3.98f) );
    val1 = FLOAT_TO_FIXED_s2_8( CLAMP(cscmat[2][2], -3.98f, 3.98f) );

    mix->csc.xv.kub_kvb = (val1 << 16) | val0;

    return true;
}

static void mixer_apply_vdp_csc(tegra_mixer *mix, VdpCSCMatrix const cscmat)
{
    mix->csc.gr2d.yos = -16;
    mix->csc.gr2d.cyx = FLOAT_TO_FIXED_s1_7( CLAMP(cscmat[0][0], -1.98f, 1.98f) );
    mix->csc.gr2d.cur = FLOAT_TO_FIXED_s2_7( CLAMP(cscmat[0][1], -3.98f, 3.98f) );
    mix->csc.gr2d.cvr = FLOAT_TO_FIXED_s2_7( CLAMP(cscmat[0][2], -3.98f, 3.98f) );
    mix->csc.gr2d.cug = FLOAT_TO_FIXED_s1_7( CLAMP(cscmat[1][1], -1.98f, 1.98f) );
    mix->csc.gr2d.cvg = FLOAT_TO_FIXED_s1_7( CLAMP(cscmat[1][2], -1.98f, 1.98f) );
    mix->csc.gr2d.cub = FLOAT_TO_FIXED_s2_7( CLAMP(cscmat[2][1], -3.98f, 3.98f) );
    mix->csc.gr2d.cvb = FLOAT_TO_FIXED_s2_7( CLAMP(cscmat[2][2], -3.98f, 3.98f) );

    mix->custom_csc = (!mixer_apply_vdp_csc_to_xv(mix, cscmat) &&
                       custom_csc(cscmat));
}

VdpStatus vdp_video_mixer_query_feature_support(VdpDevice device,
                                                VdpVideoMixerFeature feature,
                                                VdpBool *is_supported)
{
    tegra_device *dev = get_device(device);

    *is_supported = VDP_FALSE;

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_query_parameter_support(
                                            VdpDevice device,
                                            VdpVideoMixerParameter parameter,
                                            VdpBool *is_supported)
{
    tegra_device *dev = get_device(device);

    switch (parameter)
    {
    case VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE:
    case VDP_VIDEO_MIXER_PARAMETER_LAYERS:
    case VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT:
    case VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH:
        *is_supported = VDP_TRUE;
        break;
    default:
        *is_supported = VDP_FALSE;
        break;
    }

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_query_attribute_support(
                                            VdpDevice device,
                                            VdpVideoMixerAttribute attribute,
                                            VdpBool *is_supported)
{
    tegra_device *dev = get_device(device);

    switch (attribute) {
    case VDP_VIDEO_MIXER_ATTRIBUTE_CSC_MATRIX:
    case VDP_VIDEO_MIXER_ATTRIBUTE_BACKGROUND_COLOR:
        *is_supported = VDP_TRUE;
        break;

    default:
        *is_supported = VDP_FALSE;
        break;
    }

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_query_parameter_value_range(
                                            VdpDevice device,
                                            VdpVideoMixerParameter parameter,
                                            void *min_value,
                                            void *max_value)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    switch (parameter)
    {
    case VDP_VIDEO_MIXER_PARAMETER_LAYERS:
        *(uint32_t *)min_value = 0;
        *(uint32_t *)max_value = 128;
        break;
    case VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH:
        *(uint32_t *)min_value = 0;
        *(uint32_t *)max_value = INT_MAX;
        break;
    case VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT:
        *(uint32_t *)min_value = 0;
        *(uint32_t *)max_value = INT_MAX;
        break;
    default:
        put_device(dev);
        return VDP_STATUS_ERROR;
    }

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_query_attribute_value_range(
                                            VdpDevice device,
                                            VdpVideoMixerAttribute attribute,
                                            void *min_value,
                                            void *max_value)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    switch (attribute)
    {
    case VDP_VIDEO_MIXER_ATTRIBUTE_LUMA_KEY_MAX_LUMA:
    case VDP_VIDEO_MIXER_ATTRIBUTE_LUMA_KEY_MIN_LUMA:
    case VDP_VIDEO_MIXER_ATTRIBUTE_NOISE_REDUCTION_LEVEL:
        *(float *)min_value = 0.0;
        *(float *)max_value = 1.0;
        break;
    case VDP_VIDEO_MIXER_ATTRIBUTE_SHARPNESS_LEVEL:
        *(float *)min_value = -1.0;
        *(float *)max_value = 1.0;
        break;
    case VDP_VIDEO_MIXER_ATTRIBUTE_SKIP_CHROMA_DEINTERLACE:
        *(uint8_t *)min_value = 0;
        *(uint8_t *)max_value = 1;
        break;
    default:
        put_device(dev);
        return VDP_STATUS_ERROR;
    }

    put_device(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_create(VdpDevice device,
                                 uint32_t feature_count,
                                 VdpVideoMixerFeature const *features,
                                 uint32_t parameter_count,
                                 VdpVideoMixerParameter const *parameters,
                                 void const *const *parameter_values,
                                 VdpVideoMixer *mixer)
{
    tegra_device *dev = get_device(device);
    tegra_mixer *mix;
    VdpVideoMixer i;
    int ret;

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    while (parameter_count--) {
        switch (parameters[parameter_count]) {
        case VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE:
        {
            const VdpChromaType *chromatype = parameter_values[parameter_count];

            if (*chromatype != VDP_CHROMA_TYPE_420) {
                put_device(dev);
                return VDP_STATUS_ERROR;
            }
            break;
        }
        default:
            break;
        }
    }

    pthread_mutex_lock(&global_lock);

    for (i = 0; i < MAX_MIXERS_NB; i++) {
        mix = __get_mixer(i);

        if (mix == NULL) {
            mix = calloc(1, sizeof(tegra_mixer));
            set_mixer(i, mix);
            break;
        }
    }

    pthread_mutex_unlock(&global_lock);

    if (i == MAX_MIXERS_NB || mix == NULL) {
        put_device(dev);
        return VDP_STATUS_RESOURCES;
    }

    ret = pthread_mutex_init(&mix->lock, NULL);
    if (ret != 0) {
        free(mix);
        set_mixer(i, NULL);
        put_device(dev);
        return VDP_STATUS_RESOURCES;
    }

    atomic_set(&mix->refcnt, 1);
    ref_device(dev);
    mix->dev = dev;

    mixer_apply_vdp_csc(mix, CSC_BT_709);

    *mixer = i;

    put_device(dev);

    return VDP_STATUS_OK;
}

void ref_mixer(tegra_mixer *mix)
{
    atomic_inc(&mix->refcnt);
}

VdpStatus unref_mixer(tegra_mixer *mix)
{
    if (!atomic_dec_and_test(&mix->refcnt)) {
        return VDP_STATUS_OK;
    }

    unref_device(mix->dev);
    free(mix);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_set_feature_enables(
                                        VdpVideoMixer mixer,
                                        uint32_t feature_count,
                                        VdpVideoMixerFeature const *features,
                                        VdpBool const *feature_enables)
{
    tegra_mixer *mix = get_mixer(mixer);

    if (mix == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_mixer(mix);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_set_attribute_values(
                                        VdpVideoMixer mixer,
                                        uint32_t count,
                                        VdpVideoMixerAttribute const *attributes,
                                        void const *const *attribute_values)
{
    tegra_mixer *mix = get_mixer(mixer);
    const VdpColor *color;

    if (mix == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    pthread_mutex_lock(&mix->lock);

    while (count--) {
        switch (attributes[count]) {
        case VDP_VIDEO_MIXER_ATTRIBUTE_CSC_MATRIX:
            mixer_apply_vdp_csc(mix, attribute_values[count]);
            break;

        case VDP_VIDEO_MIXER_ATTRIBUTE_BACKGROUND_COLOR:
            color = attribute_values[count];
            mix->bg_color = *color;
            break;
        }
    }

    pthread_mutex_unlock(&mix->lock);

    put_mixer(mix);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_mixer_get_feature_support(
                                        VdpVideoMixer mixer,
                                        uint32_t feature_count,
                                        VdpVideoMixerFeature const *features,
                                        VdpBool *feature_supports)
{
    tegra_mixer *mix = get_mixer(mixer);

    while (feature_count--) {
        feature_supports[feature_count] = VDP_FALSE;
    }

    if (mix == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_mixer(mix);

    return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_mixer_get_feature_enables(
                                        VdpVideoMixer mixer,
                                        uint32_t feature_count,
                                        VdpVideoMixerFeature const *features,
                                        VdpBool *feature_enables)
{
    tegra_mixer *mix = get_mixer(mixer);

    if (mix == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_mixer(mix);

    return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_mixer_get_parameter_values(
                                        VdpVideoMixer mixer,
                                        uint32_t parameter_count,
                                        VdpVideoMixerParameter const *parameters,
                                        void *const *parameter_values)
{
    tegra_mixer *mix = get_mixer(mixer);

    if (mix == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_mixer(mix);

    return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_mixer_get_attribute_values(
                                        VdpVideoMixer mixer,
                                        uint32_t attribute_count,
                                        VdpVideoMixerAttribute const *attributes,
                                        void *const *attribute_values)
{
    tegra_mixer *mix = get_mixer(mixer);

    if (mix == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    put_mixer(mix);

    return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_mixer_destroy(VdpVideoMixer mixer)
{
    tegra_mixer *mix = get_mixer(mixer);

    if (mix == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    set_mixer(mixer, NULL);
    put_mixer(mix);

    return unref_mixer(mix);
}

VdpStatus vdp_video_mixer_render(
                        VdpVideoMixer mixer,
                        VdpOutputSurface background_surface,
                        VdpRect const *background_source_rect,
                        VdpVideoMixerPictureStructure current_picture_structure,
                        uint32_t video_surface_past_count,
                        VdpVideoSurface const *video_surface_past,
                        VdpVideoSurface video_surface_current,
                        uint32_t video_surface_future_count,
                        VdpVideoSurface const *video_surface_future,
                        VdpRect const *video_source_rect,
                        VdpOutputSurface destination_surface,
                        VdpRect const *destination_rect,
                        VdpRect const *destination_video_rect,
                        uint32_t layer_count,
                        VdpLayer const *layers)
{
    tegra_surface *bg_surf = get_surface_bitmap(background_surface);
    tegra_surface *dest_surf = get_surface_output(destination_surface);
    tegra_surface *video_surf = get_surface_video(video_surface_current);
    tegra_mixer *mix = get_mixer(mixer);
    tegra_shared_surface *shared = NULL;
    uint32_t src_vid_width, src_vid_height, src_vid_x0, src_vid_y0;
    uint32_t dst_vid_width, dst_vid_height, dst_vid_x0, dst_vid_y0;
    uint32_t bg_width, bg_height, bg_x0, bg_y0;
    uint32_t bg_color;
    bool draw_background = false;
    int ret;

    if (dest_surf == NULL || video_surf == NULL || mix == NULL) {
        put_mixer(mix);
        put_surface(bg_surf);
        put_surface(dest_surf);
        put_surface(video_surf);
        return VDP_STATUS_INVALID_HANDLE;
    }

    pthread_mutex_lock(&mix->lock);
    pthread_mutex_lock(&dest_surf->lock);

    shared_surface_kill_disp(dest_surf);

    if (destination_video_rect != NULL) {
        dst_vid_width = destination_video_rect->x1 - destination_video_rect->x0;
        dst_vid_height = destination_video_rect->y1 - destination_video_rect->y0;
        dst_vid_x0 = destination_video_rect->x0;
        dst_vid_y0 = destination_video_rect->y0;
    } else {
        dst_vid_width = video_surf->width;
        dst_vid_height = video_surf->height;
        dst_vid_x0 = 0;
        dst_vid_y0 = 0;
    }

    if (video_source_rect != NULL) {
        src_vid_width = video_source_rect->x1 - video_source_rect->x0;
        src_vid_height = video_source_rect->y1 - video_source_rect->y0;

        if (video_surf->pixbuf->layout == PIX_BUF_LAYOUT_LINEAR) {
            /* linear YUV offsets should be aligned to 2 on Tegra */
            src_vid_x0 = video_source_rect->x0 & ~1;
            src_vid_y0 = video_source_rect->y0;
        } else {
            /* tiled YUV offsets should be aligned to 32 on Tegra */
            src_vid_x0 = video_source_rect->x0 & ~31;
            src_vid_y0 = video_source_rect->y0 & ~31;
        }
    } else {
        src_vid_width = video_surf->width;
        src_vid_height = video_surf->height;
        src_vid_x0 = 0;
        src_vid_y0 = 0;
    }

    if (background_source_rect != NULL) {
        bg_width = background_source_rect->x1 - background_source_rect->x0;
        bg_height = background_source_rect->y1 - background_source_rect->y0;
        bg_x0 = background_source_rect->x0;
        bg_y0 = background_source_rect->y0;
    } else {
        bg_width = bg_surf ? bg_surf->width : dest_surf->width;
        bg_height = bg_surf ? bg_surf->height : dest_surf->height;
        bg_x0 = 0;
        bg_y0 = 0;
    }

    dest_surf->set_bg = false;
    bg_color = (int)(mix->bg_color.alpha * 255) << 24;

    switch (dest_surf->rgba_format) {
    case VDP_RGBA_FORMAT_B8G8R8A8:
        bg_color |= (int)(mix->bg_color.red * 255) << 16;
        bg_color |= (int)(mix->bg_color.green * 255) << 8;
        bg_color |= (int)(mix->bg_color.blue * 255) << 0;
        break;

    case VDP_RGBA_FORMAT_R8G8B8A8:
        bg_color |= (int)(mix->bg_color.blue * 255) << 16;
        bg_color |= (int)(mix->bg_color.green * 255) << 8;
        bg_color |= (int)(mix->bg_color.red * 255) << 0;
        break;

    default:
        abort();
    }

    draw_background = (dst_vid_y0 != bg_y0 ||
                       dst_vid_x0 != bg_x0 ||
                       dst_vid_height < bg_height ||
                       dst_vid_width < bg_width);

    if (bg_surf) {
        pthread_mutex_lock(&bg_surf->lock);
    }

    if (draw_background && (!bg_surf || !bg_surf->data_allocated)) {
        if (background_source_rect) {
            ret = dynamic_alloc_surface_data(dest_surf);
            if (ret) {
                if (bg_surf) {
                    pthread_mutex_unlock(&bg_surf->lock);
                }
                pthread_mutex_unlock(&dest_surf->lock);
                pthread_mutex_unlock(&mix->lock);
                put_mixer(mix);
                put_surface(bg_surf);
                put_surface(dest_surf);
                put_surface(video_surf);
                return VDP_STATUS_RESOURCES;
            }

            ret = host1x_gr2d_clear_rect_clipped(&dest_surf->stream_2d,
                                                 dest_surf->pixbuf,
                                                 bg_color,
                                                 bg_x0,
                                                 bg_y0,
                                                 bg_width,
                                                 bg_height,
                                                 dst_vid_x0,
                                                 dst_vid_y0,
                                                 dst_vid_x0 + dst_vid_width,
                                                 dst_vid_y0 + dst_vid_height,
                                                 true);
            if (ret) {
                ErrorMsg("setting BG failed %d\n", ret);
            }
        } else {
            dest_surf->bg_color = bg_color;
            dest_surf->set_bg = true;
            draw_background = false;
        }
    }

    if (draw_background && (bg_surf && bg_surf->data_allocated)) {
        ret = dynamic_alloc_surface_data(dest_surf);
        if (ret) {
            pthread_mutex_unlock(&bg_surf->lock);
            pthread_mutex_unlock(&dest_surf->lock);
            pthread_mutex_unlock(&mix->lock);
            put_mixer(mix);
            put_surface(bg_surf);
            put_surface(dest_surf);
            put_surface(video_surf);
            return VDP_STATUS_RESOURCES;
        }

        ret = host1x_gr2d_surface_blit(&dest_surf->stream_2d,
                                       bg_surf->pixbuf,
                                       dest_surf->pixbuf,
                                       &csc_rgb_default,
                                       bg_x0,
                                       bg_y0,
                                       bg_width,
                                       bg_height,
                                       0,
                                       0,
                                       dest_surf->width,
                                       dest_surf->height);
        if (ret) {
            ErrorMsg("copying BG failed %d\n", ret);
        }
    }

    if (bg_surf) {
        pthread_mutex_unlock(&bg_surf->lock);
    }

    if (!draw_background) {
        float w_ratio = (float) src_vid_width / dst_vid_width;
        float h_ratio = (float) src_vid_height / dst_vid_height;

        if ((tegra_vdpau_force_dri || !mix->custom_csc) &&
            ((w_ratio < 5.0f) && (h_ratio < 15.0f)))
        {
            shared = create_shared_surface(dest_surf,
                                           video_surf,
                                           &mix->csc,
                                           src_vid_x0,
                                           src_vid_y0,
                                           src_vid_width,
                                           src_vid_height,
                                           dst_vid_x0,
                                           dst_vid_y0,
                                           dst_vid_width,
                                           dst_vid_height);
        }

        if (!shared) {
            ret = dynamic_alloc_surface_data(dest_surf);
            if (ret) {
                pthread_mutex_unlock(&dest_surf->lock);
                pthread_mutex_unlock(&mix->lock);
                put_mixer(mix);
                put_surface(bg_surf);
                put_surface(dest_surf);
                put_surface(video_surf);
                return VDP_STATUS_RESOURCES;
            }

            ret = host1x_gr2d_clear_rect_clipped(&dest_surf->stream_2d,
                                                 dest_surf->pixbuf,
                                                 bg_color,
                                                 bg_x0,
                                                 bg_y0,
                                                 bg_width,
                                                 bg_height,
                                                 dst_vid_x0,
                                                 dst_vid_y0,
                                                 dst_vid_x0 + dst_vid_width,
                                                 dst_vid_y0 + dst_vid_height,
                                                 true);
            if (ret) {
                ErrorMsg("setting BG failed %d\n", ret);
            }

            dest_surf->set_bg = false;
        }
    }

    if (!shared) {
        ret = host1x_gr2d_surface_blit(&dest_surf->stream_2d,
                                       video_surf->pixbuf,
                                       dest_surf->pixbuf,
                                       &mix->csc.gr2d,
                                       src_vid_x0,
                                       src_vid_y0,
                                       src_vid_width,
                                       src_vid_height,
                                       dst_vid_x0,
                                       dst_vid_y0,
                                       dst_vid_width,
                                       dst_vid_height);
        if (ret) {
            ErrorMsg("video transfer failed %d\n", ret);
        }
    }

    while (layer_count--) {
        if (layers[layer_count].struct_version != VDP_LAYER_VERSION) {
            pthread_mutex_unlock(&dest_surf->lock);
            pthread_mutex_unlock(&mix->lock);
            put_mixer(mix);
            put_surface(bg_surf);
            put_surface(dest_surf);
            put_surface(video_surf);
            return VDP_STATUS_INVALID_STRUCT_VERSION;
        }

        vdp_output_surface_render_bitmap_surface(
                                    destination_surface,
                                    layers[layer_count].destination_rect,
                                    layers[layer_count].source_surface,
                                    layers[layer_count].source_rect,
                                    NULL,
                                    NULL,
                                    VDP_OUTPUT_SURFACE_RENDER_ROTATE_0);
    }

    pthread_mutex_unlock(&dest_surf->lock);
    pthread_mutex_unlock(&mix->lock);

    put_mixer(mix);
    put_surface(bg_surf);
    put_surface(dest_surf);
    put_surface(video_surf);

    return VDP_STATUS_OK;
}
