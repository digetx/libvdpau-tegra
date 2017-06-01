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

VdpStatus vdp_video_mixer_query_feature_support(VdpDevice device,
                                                VdpVideoMixerFeature feature,
                                                VdpBool *is_supported)
{
    tegra_device *dev = get_device(device);

    *is_supported = VDP_FALSE;

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

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
        return VDP_STATUS_ERROR;
    }

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
        return VDP_STATUS_ERROR;
    }

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

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    while (parameter_count--) {
        switch (parameters[parameter_count]) {
        case VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE:
        {
            const VdpChromaType *chromatype = parameter_values[parameter_count];

            if (*chromatype != VDP_CHROMA_TYPE_420) {
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
        mix = get_mixer(i);

        if (mix == NULL) {
            mix = calloc(1, sizeof(tegra_mixer));
            set_mixer(i, mix);
            break;
        }
    }

    pthread_mutex_unlock(&global_lock);

    if (i == MAX_MIXERS_NB || mix == NULL) {
        return VDP_STATUS_RESOURCES;
    }

    *mixer = i;

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

    while (count--) {
        switch (attributes[count]) {
        case VDP_VIDEO_MIXER_ATTRIBUTE_CSC_MATRIX:
            memcpy(&mix->csc_matrix, attribute_values[count],
                   sizeof(VdpCSCMatrix));
            break;

        case VDP_VIDEO_MIXER_ATTRIBUTE_BACKGROUND_COLOR:
            color = attribute_values[count];
            mix->bg_color = *color;
            break;
        }
    }

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

    return VDP_STATUS_ERROR;
}

VdpStatus vdp_video_mixer_destroy(VdpVideoMixer mixer)
{
    tegra_mixer *mix = get_mixer(mixer);

    if (mix == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    set_mixer(mixer, NULL);

    free(mix);

    return VDP_STATUS_OK;
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
    tegra_surface *dest_surf = get_surface(destination_surface);
    tegra_mixer *mix = get_mixer(mixer);
    tegra_device *dev;
    uint32_t vid_width, vid_height;
    uint32_t vid_x0, vid_y0;
    uint32_t bg_width, bg_height;
    uint32_t bg_x0, bg_y0;
    uint32_t bg_color;
    bool draw_background = false;

    if (dest_surf == NULL || mix == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    dev = dest_surf->dev;

    assert(dest_surf->idle_hack ||
           dest_surf->status == VDP_PRESENTATION_QUEUE_STATUS_IDLE);

    if (destination_video_rect != NULL) {
        vid_width = destination_video_rect->x1 - destination_video_rect->x0;
        vid_height = destination_video_rect->y1 - destination_video_rect->y0;
        vid_x0 = destination_video_rect->x0;
        vid_y0 = destination_video_rect->y0;
    } else {
        vid_width = dest_surf->pixbuf->width;
        vid_height = dest_surf->pixbuf->height;
        vid_x0 = 0;
        vid_y0 = 0;
    }

    if (background_source_rect != NULL) {
        bg_width = background_source_rect->x1 - background_source_rect->x0;
        bg_height = background_source_rect->y1 - background_source_rect->y0;
        bg_x0 = background_source_rect->x0;
        bg_y0 = background_source_rect->y0;
    } else {
        bg_width = dest_surf->pixbuf->width;
        bg_height = dest_surf->pixbuf->height;
        bg_x0 = 0;
        bg_y0 = 0;
    }

    if (vid_height < bg_height)
        draw_background = true;

    if (vid_width < bg_width)
        draw_background = true;

    if (vid_y0 != bg_y0)
        draw_background = true;

    if (vid_x0 != bg_x0)
        draw_background = true;

    if (draw_background) {
        bg_color = (int)(mix->bg_color.alpha * 255) << 24;

        switch (dest_surf->pixbuf->format) {
        case PIX_BUF_FMT_ARGB8888:
            bg_color |= (int)(mix->bg_color.red * 255) << 16;
            bg_color |= (int)(mix->bg_color.green * 255) << 8;
            bg_color |= (int)(mix->bg_color.blue * 255) << 0;
            break;

        case PIX_BUF_FMT_ABGR8888:
            bg_color |= (int)(mix->bg_color.blue * 255) << 16;
            bg_color |= (int)(mix->bg_color.green * 255) << 8;
            bg_color |= (int)(mix->bg_color.red * 255) << 0;
            break;

        default:
            abort();
        }

        host1x_gr2d_clear_rect_clipped(dev->stream,
                                       dest_surf->pixbuf,
                                       bg_color,
                                       bg_x0, bg_y0,
                                       bg_width, bg_height,
                                       vid_x0, vid_y0,
                                       vid_x0 + vid_width,
                                       vid_y0 + vid_height,
                                       true);
    }

    host1x_gr2d_surface_blit(dev->stream,
                             get_surface(video_surface_current)->pixbuf,
                             dest_surf->pixbuf,
                             mix->csc_matrix,
                             video_source_rect->x0,
                             video_source_rect->y0,
                             video_source_rect->x1 - video_source_rect->x0,
                             video_source_rect->y1 - video_source_rect->y0,
                             vid_x0,
                             vid_y0,
                             vid_width,
                             vid_height);

    while (layer_count--) {
        if (layers[layer_count].struct_version != VDP_LAYER_VERSION) {
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

    return VDP_STATUS_OK;
}
