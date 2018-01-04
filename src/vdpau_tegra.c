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

pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

bool tegra_vdpau_debug;
bool tegra_vdpau_force_xv;
bool tegra_vdpau_force_dri;

static tegra_device  * tegra_devices[MAX_DEVICES_NB];
static tegra_decoder * tegra_decoders[MAX_DECODERS_NB];
static tegra_mixer   * tegra_mixers[MAX_MIXERS_NB];
static tegra_surface * tegra_surfaces[MAX_SURFACES_NB];
static tegra_pqt     * tegra_pqts[MAX_PRESENTATION_QUEUE_TARGETS_NB];
static tegra_pq      * tegra_pqs[MAX_PRESENTATION_QUEUES_NB];

VdpCSCMatrix CSC_BT_601 = {
    { 1.164384f, 0.000000f, 1.596027f },
    { 1.164384f,-0.391762f,-0.812968f },
    { 1.164384f, 2.017232f, 0.000000f },
};

VdpCSCMatrix CSC_BT_709 = {
    { 1.164384f, 0.000000f, 1.792741f },
    { 1.164384f,-0.213249f,-0.532909f },
    { 1.164384f, 2.112402f, 0.000000f },
};

VdpTime get_time(void)
{
    struct timespec tp;

    if (clock_gettime(CLOCK_MONOTONIC, &tp) != 0)
        ErrorMsg("failed\n");

    return (VdpTime)tp.tv_sec * 1000000000ULL + (VdpTime)tp.tv_nsec;
}

tegra_decoder * __get_decoder(VdpDecoder decoder)
{
    if (decoder >= MAX_DECODERS_NB) {
        return NULL;
    }

    return tegra_decoders[decoder];
}

tegra_device * get_device(VdpDevice device)
{
    tegra_device *dev = NULL;

    pthread_mutex_lock(&global_lock);

    if (device < MAX_DEVICES_NB) {
        dev = tegra_devices[device];

        if (dev) {
            atomic_inc(&dev->refcnt);
        }
    } else {
        ErrorMsg("Invalid handle %u\n", device);
    }

    pthread_mutex_unlock(&global_lock);

    return dev;
}

tegra_decoder * get_decoder(VdpDecoder decoder)
{
    tegra_decoder *dec = NULL;

    pthread_mutex_lock(&global_lock);

    if (decoder < MAX_DECODERS_NB) {
        dec = tegra_decoders[decoder];

        if (dec) {
            atomic_inc(&dec->refcnt);
        }
    }

    pthread_mutex_unlock(&global_lock);

    return dec;
}

void set_decoder(VdpDecoder decoder, tegra_decoder *dec)
{
    if (decoder >= MAX_DECODERS_NB) {
        return;
    }

    if (dec != NULL) {
        assert(tegra_decoders[decoder] == NULL);
    }

    tegra_decoders[decoder] = dec;
}

tegra_mixer * __get_mixer(VdpVideoMixer mixer)
{
    if (mixer >= MAX_MIXERS_NB) {
        return NULL;
    }

    return tegra_mixers[mixer];
}

tegra_mixer * get_mixer(VdpVideoMixer mixer)
{
    tegra_mixer *mix = NULL;

    pthread_mutex_lock(&global_lock);

    if (mixer < MAX_MIXERS_NB) {
        mix = tegra_mixers[mixer];

        if (mix) {
            atomic_inc(&mix->refcnt);
        }
    }

    pthread_mutex_unlock(&global_lock);

    return mix;
}

void set_mixer(VdpVideoMixer mixer, tegra_mixer *mix)
{
    if (mixer >= MAX_MIXERS_NB) {
        return;
    }

    if (mix != NULL) {
        assert(tegra_mixers[mixer] == NULL);
    }

    tegra_mixers[mixer] = mix;
}

tegra_surface * __get_surface(VdpBitmapSurface surface)
{
    if (surface >= MAX_SURFACES_NB) {
        return NULL;
    }

    return tegra_surfaces[surface];
}

tegra_surface * get_surface(VdpBitmapSurface surface)
{
    tegra_surface *surf = NULL;

    pthread_mutex_lock(&global_lock);

    if (surface < MAX_SURFACES_NB) {
        surf = tegra_surfaces[surface];

        if (surf) {
            atomic_inc(&surf->refcnt);
        }
    }

    pthread_mutex_unlock(&global_lock);

    return surf;
}

void set_surface(VdpBitmapSurface surface, tegra_surface *surf)
{
    if (surface >= MAX_SURFACES_NB) {
        return;
    }

    if (surf != NULL) {
        assert(tegra_surfaces[surface] == NULL);
    }

    tegra_surfaces[surface] = surf;
}

void replace_surface(tegra_surface *old_surf, tegra_surface *new_surf)
{
    if (old_surf->surface_id >= MAX_SURFACES_NB) {
        return;
    }

    if (old_surf != new_surf) {
        assert(tegra_surfaces[old_surf->surface_id] == old_surf);
        new_surf->surface_id = old_surf->surface_id;
        old_surf->surface_id = MAX_SURFACES_NB;

        DebugMsg("surface %u %p -> %p\n",
                 new_surf->surface_id, new_surf, old_surf);

        tegra_surfaces[new_surf->surface_id] = new_surf;
    }
}

tegra_pqt * __get_presentation_queue_target(VdpPresentationQueueTarget target)
{
    if (target >= MAX_PRESENTATION_QUEUE_TARGETS_NB) {
        return NULL;
    }

    return tegra_pqts[target];
}

tegra_pqt * get_presentation_queue_target(VdpPresentationQueueTarget target)
{
    tegra_pqt *pqt = NULL;

    pthread_mutex_lock(&global_lock);

    if (target < MAX_PRESENTATION_QUEUE_TARGETS_NB) {
        pqt = tegra_pqts[target];

        if (pqt) {
            atomic_inc(&pqt->refcnt);
        }
    }

    pthread_mutex_unlock(&global_lock);

    return pqt;
}

void set_presentation_queue_target(VdpPresentationQueueTarget target,
                                   tegra_pqt *pqt)
{
    if (target >= MAX_PRESENTATION_QUEUE_TARGETS_NB) {
        return;
    }

    if (pqt != NULL) {
        assert(tegra_pqts[target] == NULL);
    }

    tegra_pqts[target] = pqt;
}

tegra_pq * __get_presentation_queue(VdpPresentationQueue presentation_queue)
{
    if (presentation_queue >= MAX_PRESENTATION_QUEUES_NB) {
        return NULL;
    }

    return tegra_pqs[presentation_queue];
}

tegra_pq * get_presentation_queue(VdpPresentationQueue presentation_queue)
{
    tegra_pq *pq = NULL;

    pthread_mutex_lock(&global_lock);

    if (presentation_queue < MAX_PRESENTATION_QUEUES_NB) {
        pq = tegra_pqs[presentation_queue];

        if (pq) {
            atomic_inc(&pq->refcnt);
        }
    }

    pthread_mutex_unlock(&global_lock);

    return pq;
}

void set_presentation_queue(VdpPresentationQueue presentation_queue,
                            tegra_pq *pq)
{
    if (presentation_queue >= MAX_PRESENTATION_QUEUES_NB ) {
        return;
    }

    if (pq != NULL) {
        assert(tegra_pqs[presentation_queue] == NULL);
    }

    tegra_pqs[presentation_queue] = pq;
}

VdpStatus vdp_get_proc_address(VdpDevice device,
                               VdpFuncId function_id,
                               void **function_pointer);

VdpStatus vdp_get_api_version(uint32_t *api_version)
{
    *api_version = TEGRA_VDPAU_INTERFACE_VERSION;

    return VDP_STATUS_OK;
}

VdpStatus vdp_get_information_string(char const **information_string)
{
    *information_string = "NVIDIA Tegra VDPAU back-end driver";

    return VDP_STATUS_OK;
}

static char const *error_strings[] = {
   [VDP_STATUS_OK]                                    = "The operation completed successfully; no error.",
   [VDP_STATUS_NO_IMPLEMENTATION]                     = "No backend implementation could be loaded.",
   [VDP_STATUS_DISPLAY_PREEMPTED]                     = "The display was preempted, or a fatal error occurred. The application must re-initialize VDPAU.",
   [VDP_STATUS_INVALID_HANDLE]                        = "An invalid handle value was provided. Either the handle does not exist at all, or refers to an object of an incorrect type.",
   [VDP_STATUS_INVALID_POINTER]                       = "An invalid pointer was provided. Typically, this means that a NULL pointer was provided for an 'output' parameter.",
   [VDP_STATUS_INVALID_CHROMA_TYPE]                   = "An invalid/unsupported VdpChromaType value was supplied.",
   [VDP_STATUS_INVALID_Y_CB_CR_FORMAT]                = "An invalid/unsupported VdpYCbCrFormat value was supplied.",
   [VDP_STATUS_INVALID_RGBA_FORMAT]                   = "An invalid/unsupported VdpRGBAFormat value was supplied.",
   [VDP_STATUS_INVALID_INDEXED_FORMAT]                = "An invalid/unsupported VdpIndexedFormat value was supplied.",
   [VDP_STATUS_INVALID_COLOR_STANDARD]                = "An invalid/unsupported VdpColorStandard value was supplied.",
   [VDP_STATUS_INVALID_COLOR_TABLE_FORMAT]            = "An invalid/unsupported VdpColorTableFormat value was supplied.",
   [VDP_STATUS_INVALID_BLEND_FACTOR]                  = "An invalid/unsupported VdpOutputSurfaceRenderBlendFactor value was supplied.",
   [VDP_STATUS_INVALID_BLEND_EQUATION]                = "An invalid/unsupported VdpOutputSurfaceRenderBlendEquation value was supplied.",
   [VDP_STATUS_INVALID_FLAG]                          = "An invalid/unsupported flag value/combination was supplied.",
   [VDP_STATUS_INVALID_DECODER_PROFILE]               = "An invalid/unsupported VdpDecoderProfile value was supplied.",
   [VDP_STATUS_INVALID_VIDEO_MIXER_FEATURE]           = "An invalid/unsupported VdpVideoMixerFeature value was supplied.",
   [VDP_STATUS_INVALID_VIDEO_MIXER_PARAMETER]         = "An invalid/unsupported VdpVideoMixerParameter value was supplied.",
   [VDP_STATUS_INVALID_VIDEO_MIXER_ATTRIBUTE]         = "An invalid/unsupported VdpVideoMixerAttribute value was supplied.",
   [VDP_STATUS_INVALID_VIDEO_MIXER_PICTURE_STRUCTURE] = "An invalid/unsupported VdpVideoMixerPictureStructure value was supplied.",
   [VDP_STATUS_INVALID_FUNC_ID]                       = "An invalid/unsupported VdpFuncId value was supplied.",
   [VDP_STATUS_INVALID_SIZE]                          = "The size of a supplied object does not match the object it is being used with. " \
                                                        "For example, a VdpVideoMixer is configured to process VdpVideoSurface objects of a specific size. " \
                                                        "If presented with a VdpVideoSurface of a different size, this error will be raised.",
   [VDP_STATUS_INVALID_VALUE]                         = "An invalid/unsupported value was supplied. " \
                                                        "This is a catch-all error code for values of type other than those with a specific error code.",
   [VDP_STATUS_INVALID_STRUCT_VERSION]                = "An invalid/unsupported structure version was specified in a versioned structure. " \
                                                        "This implies that the implementation is older than the header file the application was built against.",
   [VDP_STATUS_RESOURCES]                             = "The system does not have enough resources to complete the requested operation at this time.",
   [VDP_STATUS_HANDLE_DEVICE_MISMATCH]                = "The set of handles supplied are not all related to the same VdpDevice.When performing operations " \
                                                        "that operate on multiple surfaces, such as VdpOutputSurfaceRenderOutputSurface or VdpVideoMixerRender, " \
                                                        "all supplied surfaces must have been created within the context of the same VdpDevice object. " \
                                                        "This error is raised if they were not.",
   [VDP_STATUS_ERROR]                                 = "A catch-all error, used when no other error code applies.",
};

char const *vdp_get_error_string(VdpStatus status)
{
    switch (status) {
    case VDP_STATUS_OK ... VDP_STATUS_ERROR:
        return error_strings[status];
    }

    return "Bad status value, shouldn't happen!";
}

VdpStatus vdp_generate_csc_matrix(VdpProcamp *procamp,
                                  VdpColorStandard standard,
                                  VdpCSCMatrix *csc_matrix)
{
    float uvcos, uvsin;
    int i;

    if (csc_matrix == NULL) {
        return VDP_STATUS_INVALID_POINTER;
    }

    switch (standard) {
    case VDP_COLOR_STANDARD_ITUR_BT_601:
        memcpy(csc_matrix, CSC_BT_601, sizeof(VdpCSCMatrix));
        break;
    case VDP_COLOR_STANDARD_ITUR_BT_709:
        memcpy(csc_matrix, CSC_BT_709, sizeof(VdpCSCMatrix));
        break;
    default:
        return VDP_STATUS_NO_IMPLEMENTATION;
    }

    if (procamp == NULL) {
        return VDP_STATUS_OK;
    }

    if (procamp->struct_version != VDP_PROCAMP_VERSION) {
        return VDP_STATUS_INVALID_STRUCT_VERSION;
    }

    if (procamp->hue != 0.0f ||
            procamp->saturation != 1.0f ||
                procamp->contrast != 1.0f) {
        uvcos = procamp->saturation * cosf(procamp->hue);
        uvsin = procamp->saturation * sinf(procamp->hue);

        for (i = 0; i < 3; i++) {
            float u = (*csc_matrix)[i][1] * uvcos + (*csc_matrix)[i][2] * uvsin;
            float v = (*csc_matrix)[i][1] * uvsin + (*csc_matrix)[i][2] * uvcos;
            (*csc_matrix)[i][0] = procamp->contrast;
            (*csc_matrix)[i][1] = u;
            (*csc_matrix)[i][2] = v;
            (*csc_matrix)[i][3] = -(u + v) / 2;
            (*csc_matrix)[i][3] += 0.5 - procamp->contrast / 2;
            (*csc_matrix)[i][3] += procamp->brightness;
        }
    }

    return VDP_STATUS_OK;
}

static void *const tegra_vdpau_api[] =
{
    [VDP_FUNC_ID_GET_ERROR_STRING]                                      = vdp_get_error_string,
    [VDP_FUNC_ID_GET_PROC_ADDRESS]                                      = vdp_get_proc_address,
    [VDP_FUNC_ID_GET_API_VERSION]                                       = vdp_get_api_version,
    [VDP_FUNC_ID_GET_INFORMATION_STRING]                                = vdp_get_information_string,
    [VDP_FUNC_ID_DEVICE_DESTROY]                                        = vdp_device_destroy,
    [VDP_FUNC_ID_GENERATE_CSC_MATRIX]                                   = vdp_generate_csc_matrix,
    [VDP_FUNC_ID_VIDEO_SURFACE_QUERY_CAPABILITIES]                      = vdp_video_surface_query_capabilities,
    [VDP_FUNC_ID_VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES] = vdp_video_surface_query_get_put_bits_y_cb_cr_capabilities,
    [VDP_FUNC_ID_VIDEO_SURFACE_CREATE]                                  = vdp_video_surface_create,
    [VDP_FUNC_ID_VIDEO_SURFACE_DESTROY]                                 = vdp_video_surface_destroy,
    [VDP_FUNC_ID_VIDEO_SURFACE_GET_PARAMETERS]                          = vdp_video_surface_get_parameters,
    [VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR]                        = vdp_video_surface_get_bits_y_cb_cr,
    [VDP_FUNC_ID_VIDEO_SURFACE_PUT_BITS_Y_CB_CR]                        = vdp_video_surface_put_bits_y_cb_cr,
    [VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_CAPABILITIES]                     = vdp_output_surface_query_capabilities,
    [VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_GET_PUT_BITS_NATIVE_CAPABILITIES] = vdp_output_surface_query_get_put_bits_native_capabilities,
    [VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_PUT_BITS_INDEXED_CAPABILITIES]    = vdp_output_surface_query_put_bits_indexed_capabilities,
    [VDP_FUNC_ID_OUTPUT_SURFACE_QUERY_PUT_BITS_Y_CB_CR_CAPABILITIES]    = vdp_output_surface_query_put_bits_y_cb_cr_capabilities,
    [VDP_FUNC_ID_OUTPUT_SURFACE_CREATE]                                 = vdp_output_surface_create,
    [VDP_FUNC_ID_OUTPUT_SURFACE_DESTROY]                                = vdp_output_surface_destroy,
    [VDP_FUNC_ID_OUTPUT_SURFACE_GET_PARAMETERS]                         = vdp_output_surface_get_parameters,
    [VDP_FUNC_ID_OUTPUT_SURFACE_GET_BITS_NATIVE]                        = vdp_output_surface_get_bits_native,
    [VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_NATIVE]                        = vdp_output_surface_put_bits_native,
    [VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_INDEXED]                       = vdp_output_surface_put_bits_indexed,
    [VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_Y_CB_CR]                       = vdp_output_surface_put_bits_y_cb_cr,
    [VDP_FUNC_ID_BITMAP_SURFACE_QUERY_CAPABILITIES]                     = vdp_bitmap_surface_query_capabilities,
    [VDP_FUNC_ID_BITMAP_SURFACE_CREATE]                                 = vdp_bitmap_surface_create,
    [VDP_FUNC_ID_BITMAP_SURFACE_DESTROY]                                = vdp_bitmap_surface_destroy,
    [VDP_FUNC_ID_BITMAP_SURFACE_GET_PARAMETERS]                         = vdp_bitmap_surface_get_parameters,
    [VDP_FUNC_ID_BITMAP_SURFACE_PUT_BITS_NATIVE]                        = vdp_bitmap_surface_put_bits_native,
    [VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_OUTPUT_SURFACE]                  = vdp_output_surface_render_output_surface,
    [VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_BITMAP_SURFACE]                  = vdp_output_surface_render_bitmap_surface,
    [VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_VIDEO_SURFACE_LUMA]              = NULL,
    [VDP_FUNC_ID_DECODER_QUERY_CAPABILITIES]                            = vdp_decoder_query_capabilities,
    [VDP_FUNC_ID_DECODER_CREATE]                                        = vdp_decoder_create,
    [VDP_FUNC_ID_DECODER_DESTROY]                                       = vdp_decoder_destroy,
    [VDP_FUNC_ID_DECODER_GET_PARAMETERS]                                = vdp_decoder_get_parameters,
    [VDP_FUNC_ID_DECODER_RENDER]                                        = vdp_decoder_render,
    [VDP_FUNC_ID_VIDEO_MIXER_QUERY_FEATURE_SUPPORT]                     = vdp_video_mixer_query_feature_support,
    [VDP_FUNC_ID_VIDEO_MIXER_QUERY_PARAMETER_SUPPORT]                   = vdp_video_mixer_query_parameter_support,
    [VDP_FUNC_ID_VIDEO_MIXER_QUERY_ATTRIBUTE_SUPPORT]                   = vdp_video_mixer_query_attribute_support,
    [VDP_FUNC_ID_VIDEO_MIXER_QUERY_PARAMETER_VALUE_RANGE]               = vdp_video_mixer_query_parameter_value_range,
    [VDP_FUNC_ID_VIDEO_MIXER_QUERY_ATTRIBUTE_VALUE_RANGE]               = vdp_video_mixer_query_attribute_value_range,
    [VDP_FUNC_ID_VIDEO_MIXER_CREATE]                                    = vdp_video_mixer_create,
    [VDP_FUNC_ID_VIDEO_MIXER_SET_FEATURE_ENABLES]                       = vdp_video_mixer_set_feature_enables,
    [VDP_FUNC_ID_VIDEO_MIXER_SET_ATTRIBUTE_VALUES]                      = vdp_video_mixer_set_attribute_values,
    [VDP_FUNC_ID_VIDEO_MIXER_GET_FEATURE_SUPPORT]                       = vdp_video_mixer_get_feature_support,
    [VDP_FUNC_ID_VIDEO_MIXER_GET_FEATURE_ENABLES]                       = vdp_video_mixer_get_feature_enables,
    [VDP_FUNC_ID_VIDEO_MIXER_GET_PARAMETER_VALUES]                      = vdp_video_mixer_get_parameter_values,
    [VDP_FUNC_ID_VIDEO_MIXER_GET_ATTRIBUTE_VALUES]                      = vdp_video_mixer_get_attribute_values,
    [VDP_FUNC_ID_VIDEO_MIXER_DESTROY]                                   = vdp_video_mixer_destroy,
    [VDP_FUNC_ID_VIDEO_MIXER_RENDER]                                    = vdp_video_mixer_render,
    [VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_DESTROY]                     = vdp_presentation_queue_target_destroy,
    [VDP_FUNC_ID_PRESENTATION_QUEUE_CREATE]                             = vdp_presentation_queue_create,
    [VDP_FUNC_ID_PRESENTATION_QUEUE_DESTROY]                            = vdp_presentation_queue_destroy,
    [VDP_FUNC_ID_PRESENTATION_QUEUE_SET_BACKGROUND_COLOR]               = vdp_presentation_queue_set_background_color,
    [VDP_FUNC_ID_PRESENTATION_QUEUE_GET_BACKGROUND_COLOR]               = vdp_presentation_queue_get_background_color,
    [VDP_FUNC_ID_PRESENTATION_QUEUE_GET_TIME]                           = vdp_presentation_queue_get_time,
    [VDP_FUNC_ID_PRESENTATION_QUEUE_DISPLAY]                            = vdp_presentation_queue_display,
    [VDP_FUNC_ID_PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE]           = vdp_presentation_queue_block_until_surface_idle,
    [VDP_FUNC_ID_PRESENTATION_QUEUE_QUERY_SURFACE_STATUS]               = vdp_presentation_queue_query_surface_status,
    [VDP_FUNC_ID_PREEMPTION_CALLBACK_REGISTER]                          = vdp_preemption_callback_register,
};

VdpStatus vdp_get_proc_address(VdpDevice device, VdpFuncId function_id,
                               void **function_pointer)
{
    switch (function_id) {
    case VDP_FUNC_ID_GET_ERROR_STRING ... VDP_FUNC_ID_PREEMPTION_CALLBACK_REGISTER:
        *function_pointer = tegra_vdpau_api[function_id];

        if (*function_pointer == NULL) {
            break;
        }

        return VDP_STATUS_OK;

    case VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_CREATE_X11:
        *function_pointer = vdp_presentation_queue_target_create_x11;

        return VDP_STATUS_OK;

    default:
        break;
    }

    return VDP_STATUS_INVALID_FUNC_ID;
}

VdpStatus vdp_preemption_callback_register(VdpDevice device,
                                           VdpPreemptionCallback callback,
                                           void *context)
{
    return VDP_STATUS_OK;
}

void ref_device(tegra_device *dev)
{
    atomic_inc(&dev->refcnt);
}

VdpStatus unref_device(tegra_device *dev)
{
    if (!atomic_dec_and_test(&dev->refcnt))
        return VDP_STATUS_OK;

    DebugMsg("device closed\n");

    XvUngrabPort(dev->display, dev->xv_port, CurrentTime);
    tegra_stream_destroy(dev->stream);
    drm_tegra_channel_close(dev->gr2d);
    drm_tegra_close(dev->drm);
    close(dev->vde_fd);
    close(dev->drm_fd);
    free(dev->stream);
    free(dev);

    return VDP_STATUS_OK;
}

VdpStatus vdp_device_destroy(VdpDevice device)
{
    tegra_device *dev = get_device(device);

    if (dev == NULL) {
        return VDP_INVALID_HANDLE;
    }

    tegra_devices[device] = NULL;
    put_device(dev);

    return unref_device(dev);
}

EXPORTED VdpStatus vdp_imp_device_create_x11(Display *display,
                                             int screen,
                                             VdpDevice *device,
                                             VdpGetProcAddress **get_proc_address)
{
    struct drm_tegra *drm = NULL;
    struct drm_tegra_channel *gr2d = NULL;
    struct tegra_stream *stream = NULL;
    XvAdaptorInfo *adaptor_info = NULL;
    XvImageFormatValues *fmt;
    VdpDevice i;
    drm_magic_t magic;
    char *debug_str;
    unsigned int ver, rel, req, ev, err;
    unsigned int num_adaptors;
    int num_formats;
    int vde_fd = -1;
    int drm_fd = -1;
    int ret;

    debug_str = getenv("VDPAU_TEGRA_DEBUG");
    if (debug_str && strcmp(debug_str, "0")) {
        tegra_vdpau_debug = true;
    }

    debug_str = getenv("VDPAU_TEGRA_FORCE_XV");
    if (debug_str && strcmp(debug_str, "0")) {
        tegra_vdpau_force_xv = true;
    }

    debug_str = getenv("VDPAU_TEGRA_FORCE_DRI");
    if (debug_str && strcmp(debug_str, "0")) {
        tegra_vdpau_force_dri = true;
    }

    drm_fd = drmOpen("tegra", NULL);
    if (drm_fd < 0) {
        perror("Failed to open tegra DRM\n");
        goto err_cleanup;
    }

    if (drmGetMagic(drm_fd, &magic)) {
        ErrorMsg("drmGetMagic failed\n");
        goto err_cleanup;
    }

    if (!DRI2Authenticate(display, DefaultRootWindow(display), magic)) {
        ErrorMsg("DRI2Authenticate failed\n");
        goto err_cleanup;
    }

    ret = drm_tegra_new(&drm, drm_fd);
    if (ret < 0) {
        ErrorMsg("Tegra DRM not detected\n");
        goto err_cleanup;
    }

    ret = drm_tegra_channel_open(&gr2d, drm, DRM_TEGRA_GR2D);
    if (ret < 0) {
        ErrorMsg("failed to open 2D channel: %d\n", ret);
        goto err_cleanup;
    }

    stream = calloc(1, sizeof(*stream));
    if (!stream) {
        ErrorMsg("failed to allocate command stream\n");
        goto err_cleanup;
    }

    ret = tegra_stream_create(drm, gr2d, stream, 0);
    if (ret < 0) {
        ErrorMsg("failed to create command stream: %d\n", ret);
        goto err_cleanup;
    }

    ret = XvQueryExtension(display, &ver, &rel, &req, &ev, &err);
    if (ret != Success) {
        ErrorMsg("Xv is disabled in the Xorg driver\n");
        goto err_cleanup;
    }

    ret = XvQueryAdaptors(display, DefaultRootWindow(display),
                          &num_adaptors, &adaptor_info);
    if (ret != Success) {
        goto err_cleanup;
    }

    while (num_adaptors--) {
        if (adaptor_info[num_adaptors].num_ports != 1)
            continue;

        if (!(adaptor_info[num_adaptors].type & XvImageMask))
            continue;

        fmt = XvListImageFormats(display, adaptor_info[num_adaptors].base_id,
                                 &num_formats);

        while (num_formats--) {
            if (!strncmp(fmt[num_formats].guid, "PASSTHROUGH_YV12", 16) &&
                    fmt[num_formats].id == FOURCC_PASSTHROUGH_YV12) {
                goto xv_detected;
            }
        }

        XFree(fmt);
    }

    ErrorMsg("Opentegra Xv undetected\n");

    goto err_cleanup;

xv_detected:
    pthread_mutex_lock(&global_lock);

    XFree(fmt);

    ret = XvGrabPort(display, adaptor_info[num_adaptors].base_id, CurrentTime);
    if (ret != Success) {
        ErrorMsg("Xv port is busy\n");
        goto err_cleanup;
    }

    for (i = 0; i < MAX_DEVICES_NB; i++) {
        if (tegra_devices[i] == NULL) {
            tegra_devices[i] = calloc(1, sizeof(tegra_device));
            break;
        }
    }

    pthread_mutex_unlock(&global_lock);

    if (i == MAX_DEVICES_NB || tegra_devices[i] == NULL) {
        goto err_cleanup;
    }

    pthread_mutex_init(&tegra_devices[i]->lock, NULL);
    atomic_set(&tegra_devices[i]->refcnt, 1);

    tegra_devices[i]->xv_port = adaptor_info[num_adaptors].base_id;
    tegra_devices[i]->display = display;
    tegra_devices[i]->screen = screen;
    tegra_devices[i]->vde_fd = vde_fd;
    tegra_devices[i]->drm_fd = drm_fd;
    tegra_devices[i]->stream = stream;
    tegra_devices[i]->gr2d = gr2d;
    tegra_devices[i]->drm = drm;

    *device = i;
    *get_proc_address = vdp_get_proc_address;

    XvFreeAdaptorInfo(adaptor_info);

    return VDP_STATUS_OK;

err_cleanup:
    tegra_stream_destroy(stream);
    drm_tegra_channel_close(gr2d);
    drm_tegra_close(drm);
    close(drm_fd);
    free(stream);

    if (adaptor_info)
        XvFreeAdaptorInfo(adaptor_info);

    return VDP_STATUS_RESOURCES;
}
