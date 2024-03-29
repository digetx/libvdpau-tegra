/*
 * NVIDIA TEGRA 2 VDPAU backend
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

#ifndef VDPAU_TEGRA_H
#define VDPAU_TEGRA_H

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <pixman.h>
#include <vdpau/vdpau_x11.h>

#include <xf86drm.h>

#define NEED_REPLIES
#include <X11/Xlibint.h>
#include <X11/Xmd.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/dri2proto.h>
#include <X11/extensions/dri2tokens.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xvlib.h>

#include "opentegra_drm.h"
#include "opentegra_lib.h"

#include "atomic.h"
#include "dri2.h"
#include "bitstream.h"
#include "tegra_stream.h"
#include "shaders/prog.h"
#include "host1x.h"
#include "host1x-api.h"
#include "tgr_3d.xml.h"
#include "util_double_list.h"
#include "uapi/dma-buf.h"
#include "uapi/tegra-vde.h"
#include "uapi/tegra-vde-v1.h"

#include <linux/media.h>
#include <linux/videodev2.h>

#include "media.h"
#include "v4l2.h"

#define EXPORTED __attribute__((__visibility__("default")))

#define GRATE_KERNEL_DRM_VERSION            99991

#define TEGRA_VDPAU_INTERFACE_VERSION 1

#define MAX_DEVICES_NB                      1
#define MAX_DECODERS_NB                     1
#define MAX_MIXERS_NB                       16
#define MAX_SURFACES_NB                     256
#define MAX_PRESENTATION_QUEUE_TARGETS_NB   32
#define MAX_PRESENTATION_QUEUES_NB          128
#define MAX_V4L2_BUFFERS                    24
#define MIN_V4L2_BUFFERS                    17

#define SURFACE_VIDEO               (1 << 0)
#define SURFACE_OUTPUT              (1 << 1)

#define PASSTHROUGH_DATA_SIZE   36

#define FOURCC_PASSTHROUGH_YV12     (('1' << 24) + ('2' << 16) + ('V' << 8) + 'Y')
#define FOURCC_PASSTHROUGH_XRGB565  (('1' << 24) + ('B' << 16) + ('G' << 8) + 'R')
#define FOURCC_PASSTHROUGH_XRGB8888 (('X' << 24) + ('B' << 16) + ('G' << 8) + 'R')
#define FOURCC_PASSTHROUGH_XBGR8888 (('X' << 24) + ('R' << 16) + ('G' << 8) + 'B')

#define PASSTHROUGH_DATA_SIZE_V2 128

#define FOURCC_PASSTHROUGH_YV12_V2     (('T' << 24) + ('G' << 16) + ('R' << 8) + '1')
#define FOURCC_PASSTHROUGH_RGB565_V2   (('T' << 24) + ('G' << 16) + ('R' << 8) + '2')
#define FOURCC_PASSTHROUGH_XRGB8888_V2 (('T' << 24) + ('G' << 16) + ('R' << 8) + '3')
#define FOURCC_PASSTHROUGH_XBGR8888_V2 (('T' << 24) + ('G' << 16) + ('R' << 8) + '4')

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define CONTAINER_OFFSETOF(TYPE, MEMBER) \
    ((size_t) &((TYPE *)0)->MEMBER)

#define CONTAINER_OF(ptr, type, member) ({                                  \
        const typeof(((type *)0)->member) *__mptr = (ptr);                  \
        (type *)((char *)__mptr - CONTAINER_OFFSETOF(type, member));  \
    })

#define __align_mask(value, mask)  (((value) + (mask)) & ~(mask))
#define ALIGN(value, alignment)    __align_mask(value, (typeof(value))((alignment) - 1))

#define ALIGNED(x, a)               (!((x) & ((a) - 1)))

#define ErrorMsg(fmt, args...) \
    fprintf(stderr, "%s:%d/%s(): " fmt, \
            __FILE__, __LINE__, __func__, ##args)

#define InfoMsg(fmt, args...) \
    fprintf(stderr, "vdpau-tegra: " fmt, ##args)

#define DebugMsg(fmt, args...) \
do { \
    if (tegra_vdpau_debug) \
        fprintf(stderr, "%s:%d/%s(): " fmt, \
                __FILE__, __LINE__, __func__, ##args); \
} while (0)

#define request_log(fmt, args...) ErrorMsg(fmt, ##args)

#define CLAMP(_v, _vmin, _vmax) \
    (((_v) < (_vmin) ? (_vmin) : (((_v) > (_vmax)) ? (_vmax) : (_v))))

#define FLOAT_TO_FIXED_6_12(fp) \
    (((int32_t) (fp * 4096.0f + 0.5f)) & ((1 << 18) - 1))

#define FLOAT_TO_FIXED_s2_8(fp) \
    (((int32_t) (fp * 256.0f + 0.5f)) & ((1 << 11) - 1))

#define FLOAT_TO_FIXED_s1_8(fp) \
    (((int32_t) (fp * 256.0f + 0.5f)) & ((1 << 10) - 1))

#define FLOAT_TO_FIXED_s2_7(fp) \
    (((fp < 0.0f) << 9) | (((int32_t) (fabs(fp) * 128.0f + 0.5f)) & ((1 << 9) - 1)))

#define FLOAT_TO_FIXED_s1_7(fp) \
    (((fp < 0.0f) << 8) | (((int32_t) (fabs(fp) * 128.0f + 0.5f)) & ((1 << 8) - 1)))

#define FLOAT_TO_FIXED_0_8(fp) \
    (((int32_t) (fp * 256.0f + 0.5f)) & ((1 << 8) - 1))

#define NSEC_PER_SEC   1000000000ULL

typedef union TegraXvVdpauInfo {
    struct {
        unsigned int visible : 1;
        unsigned int crtc_pipe : 1;
    };

    uint32_t data;
} TegraXvVdpauInfo;

extern bool tegra_vdpau_debug;
extern bool tegra_vdpau_force_xv;
extern bool tegra_vdpau_force_dri;
extern bool tegra_vdpau_dri_xv_autoswitch;

extern VdpCSCMatrix CSC_BT_601;
extern VdpCSCMatrix CSC_BT_709;

typedef struct {
    int atomic;
} atomic_t;

extern pthread_mutex_t global_lock;

typedef struct tegra_csc {
    struct host1x_csc_params gr2d;

    struct xv_csc {
        uint32_t yof_kyrgb;
        uint32_t kur_kvr;
        uint32_t kug_kvg;
        uint32_t kub_kvb;
    } xv;
} tegra_csc;

typedef struct tegra_surface_cache {
    struct list_head list;
    struct list_head cache_list_entry;
} tegra_surface_cache;

typedef struct tegra_surface_cache_entry {
    struct list_head entry;
    VdpTime last_use;
    tegra_surface_cache *cache;
} tegra_surface_cache_entry;

typedef struct tegra_device_v4l2 {
    bool presents;
    int video_fd;
} tegra_device_v4l2;

typedef struct tegra_device {
    struct drm_tegra *drm;
    struct drm_tegra_channel *gr3d;
    struct drm_tegra_channel *gr2d;
    Display *display;
    XvPortID xv_port;
    atomic_t refcnt;
    unsigned int surf_id_itr;
    bool dri2_inited;
    bool dri2_ready;
    bool xv_ready;
    bool xv_v2;
    bool disp_composited;
    bool disp_rotated;
    int screen;
    int vde_fd;
    int drm_fd;

    Atom xvVdpauInfo;

    struct xv_csc_controls {
        Atom xvCSC_YOF_KYRGB;
        Atom xvCSC_KUR_KVR;
        Atom xvCSC_KUG_KVG;
        Atom xvCSC_KUB_KVB;
        Atom xvCSC_update;
        tegra_csc old;
        bool applied;
        bool inited;
        bool ready;
    } xv_csc;

    tegra_device_v4l2 v4l2;
} tegra_device;

struct tegra_surface;

typedef struct tegra_shared_surface {
    atomic_t refcnt;
    struct tegra_surface *video;
    struct tegra_surface *disp;
    struct tegra_csc csc;
    uint32_t src_x0, src_y0, src_width, src_height;
    uint32_t dst_x0, dst_y0, dst_width, dst_height;
    XvImage *xv_img;
} tegra_shared_surface;

typedef struct tegra_surface_v4l2 {
    struct timeval timestamp;
    int buf_idx;
} tegra_surface_v4l2;

typedef struct tegra_surface {
    tegra_device *dev;
    struct tegra_stream *stream_3d;
    struct tegra_stream *stream_2d;

    bool detached;
    bool destroyed;

    struct tegra_vde_h264_frame *frame;
    int32_t pic_order_cnt;

    pixman_format_code_t pfmt;
    pixman_image_t *pix;
    XvImage *xv_img;
    uint32_t flags;

    void *y_data;
    void *cb_data;
    void *cr_data;

    struct host1x_pixelbuffer *pixbuf;
    union {
        struct drm_tegra_bo *y_bo;
        struct drm_tegra_bo *bo;
    };
    struct drm_tegra_bo *cb_bo;
    struct drm_tegra_bo *cr_bo;
    struct drm_tegra_bo *aux_bo;

    uint32_t disp_width;
    uint32_t disp_height;

    uint32_t width;
    uint32_t height;

    VdpPresentationQueueStatus status;
    VdpTime first_presentation_time;
    VdpTime earliest_presentation_time;

    atomic_t refcnt;
    struct list_head list_item;
    pthread_cond_t idle_cond;
    pthread_mutex_t lock;

    uint32_t surface_id;

    tegra_shared_surface *shared;

    uint32_t bg_color;
    bool set_bg;

    VdpRGBAFormat rgba_format;
    bool data_allocated;
    bool data_dirty;

    unsigned int map_cnt;

    tegra_surface_v4l2 v4l2;
    tegra_surface_cache_entry cache_entry;
} tegra_surface;

typedef struct tegra_decoder_v4l2 {
    bool presents;
    int media_fd;
    int video_fd;
    int request_fd;
    unsigned int num_buffers;
    struct timeval timestamps[MAX_V4L2_BUFFERS];
    tegra_surface *surfaces[MAX_V4L2_BUFFERS];
} tegra_decoder_v4l2;

typedef struct tegra_decoder {
    tegra_device *dev;
    atomic_t refcnt;
    int is_baseline_profile;
    uint32_t width;
    uint32_t height;
    bool v1;
    unsigned int bitstream_min_size;
    tegra_decoder_v4l2 v4l2;
    tegra_surface_cache surf_cache;
} tegra_decoder;

typedef struct tegra_mixer {
    struct tegra_csc csc;
    pthread_mutex_t lock;
    atomic_t refcnt;
    VdpColor bg_color;
    tegra_device *dev;
    bool custom_csc;
} tegra_mixer;

enum tegra_pqt_display {
    TEGRA_PQT_NONE,
    TEGRA_PQT_XV,
    TEGRA_PQT_DRI,
};

struct tegra_pqt_bg_state {
    uint32_t bg_color;
    uint32_t colorkey;
    uint32_t surf_x;
    uint32_t surf_y;
    uint32_t surf_w;
    uint32_t surf_h;
    uint32_t disp_w;
    uint32_t disp_h;
    bool shared;
};

typedef struct tegra_pqt {
    tegra_device *dev;
    tegra_surface *disp_surf;
    struct host1x_pixelbuffer *dri_pixbuf;
    Drawable drawable;
    GC gc;
    atomic_t refcnt;
    pthread_t x11_thread;
    pthread_t disp_thread;
    pthread_cond_t disp_cond;
    pthread_mutex_t disp_lock;
    pthread_mutex_t lock;
    bool threads_running;
    bool dri2_drawable_created;
    bool overlapped_current;
    bool overlapped_new;
    bool win_move;
    bool exit;
    tegra_surface *dri_prep_surf;
    Atom xv_ckey_atom;
    struct tegra_pqt_bg_state bg_old_state;
    struct tegra_pqt_bg_state bg_new_state;
    enum tegra_pqt_display disp_state;
} tegra_pqt;

typedef struct tegra_pq {
    tegra_pqt *pqt;
    struct list_head surf_list;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t disp_thread;
    atomic_t refcnt;
    bool exit;
} tegra_pq;

tegra_device * get_device(VdpDevice device);
void ref_device(tegra_device *dev);
VdpStatus unref_device(tegra_device *dev);
#define put_device(__dev) ({ if (__dev) unref_device(__dev); })

tegra_decoder * __get_decoder(VdpDecoder decoder);
tegra_decoder * get_decoder(VdpDecoder decoder);
void ref_decoder(tegra_decoder *dec);
VdpStatus unref_decoder(tegra_decoder *dec);
#define put_decoder(__dec) ({ if (__dec) unref_decoder(__dec); })
void set_decoder(VdpDecoder decoder, tegra_decoder *dec);

tegra_mixer * __get_mixer(VdpVideoMixer mixer);
tegra_mixer * get_mixer(VdpVideoMixer mixer);
void ref_mixer(tegra_mixer *mix);
VdpStatus unref_mixer(tegra_mixer *mix);
#define put_mixer(__mix) ({ if (__mix) unref_mixer(__mix); })
void set_mixer(VdpVideoMixer mixer, tegra_mixer *mix);

tegra_surface * __get_surface(VdpBitmapSurface surface);
tegra_surface * get_surface(VdpBitmapSurface surface);
tegra_surface * get_surface_bitmap(VdpBitmapSurface surface);
tegra_surface * get_surface_output(VdpBitmapSurface surface);
tegra_surface * get_surface_video(VdpBitmapSurface surface);
void ref_surface(tegra_surface *surf);
VdpStatus unref_surface(tegra_surface *surf);
#define put_surface(__surf) ({ if (__surf) unref_surface(__surf); })
void set_surface(VdpBitmapSurface surface, tegra_surface *surf);
void replace_surface(tegra_surface *old_surf, tegra_surface *new_surf);
int map_surface_data(tegra_surface *surf);
void unmap_surface_data(tegra_surface *surf);
int dynamic_alloc_surface_data(tegra_surface *surf);
int dynamic_release_surface_data(tegra_surface *surf);
int alloc_surface_data(tegra_surface *surf);
int release_surface_data(tegra_surface *surf);
uint32_t create_surface(tegra_device *dev,
                        uint32_t width,
                        uint32_t height,
                        VdpRGBAFormat rgba_format,
                        int output,
                        int video);
tegra_surface *alloc_surface(tegra_device *dev,
                             uint32_t width, uint32_t height,
                             VdpRGBAFormat rgba_format,
                             int output, int video);
VdpStatus destroy_surface(tegra_surface *surf);

tegra_pqt * __get_presentation_queue_target(VdpPresentationQueueTarget target);
tegra_pqt * get_presentation_queue_target(VdpPresentationQueueTarget target);
void ref_queue_target(tegra_pqt *pqt);
VdpStatus unref_queue_target(tegra_pqt *pqt);
#define put_queue_target(__pqt) ({ if (__pqt) unref_queue_target(__pqt); })
void set_presentation_queue_target(VdpPresentationQueueTarget target,
                                   tegra_pqt *pqt);
void pqt_display_surface_to_idle_state(tegra_pqt *pqt);
void pqt_display_surface(tegra_pqt *pqt, tegra_surface *surf,
                         bool update_status, bool transit, bool vsync);
void pqt_prepare_dri_surface(tegra_pqt *pqt, tegra_surface *surf);

tegra_pq * __get_presentation_queue(VdpPresentationQueue presentation_queue);
tegra_pq * get_presentation_queue(VdpPresentationQueue presentation_queue);
void ref_presentation_queue(tegra_pq *pq);
VdpStatus unref_presentation_queue(tegra_pq *pq);
#define put_presentation_queue(__pq) ({ if (__pq) unref_presentation_queue(__pq); })
void set_presentation_queue(VdpPresentationQueue presentation_queue,
                            tegra_pq *pq);

int sync_dmabuf_write_start(int dmabuf_fd);
int sync_dmabuf_write_end(int dmabuf_fd);
int sync_dmabuf_read_start(int dmabuf_fd);
int sync_dmabuf_read_end(int dmabuf_fd);

tegra_shared_surface *create_shared_surface(tegra_surface *disp,
                                            tegra_surface *video,
                                            tegra_csc *csc,
                                            uint32_t src_x0,
                                            uint32_t src_y0,
                                            uint32_t src_width,
                                            uint32_t src_height,
                                            uint32_t dst_x0,
                                            uint32_t dst_y0,
                                            uint32_t dst_width,
                                            uint32_t dst_height);
void ref_shared_surface(tegra_shared_surface *shared);
void unref_shared_surface(tegra_shared_surface *shared);
tegra_surface * shared_surface_swap_video(tegra_surface *old);
int shared_surface_transfer_video(tegra_surface *disp);
void shared_surface_kill_disp(tegra_surface *disp);
tegra_shared_surface * shared_surface_get(tegra_surface *disp);

bool tegra_check_xv_atom(tegra_device *dev, char const *atom_name);
bool tegra_xv_initialize_csc(tegra_device *dev);
void tegra_xv_reset_csc(tegra_device *dev);
bool tegra_xv_apply_csc(tegra_device *dev, tegra_csc *csc);

int rotate_surface_gr2d(tegra_surface *src_surf,
                        tegra_surface *dst_surf,
                        struct host1x_csc_params *csc,
                        enum host1x_2d_rotate rotate,
                        unsigned int sx, unsigned int sy,
                        unsigned int src_width, int src_height,
                        unsigned int dx, unsigned int dy,
                        unsigned int dst_width, int dst_height,
                        bool check_only);

VdpTime get_time(void);
int tegra_ioctl(int fd, int request, ...);

void tegra_surface_cache_init(tegra_surface_cache *cache);
void tegra_surface_cache_release(tegra_surface_cache *cache);
void tegra_surface_cache_add_surface(tegra_surface_cache *cache,
                                     tegra_surface *surf);
void tegra_surface_cache_surface_self_remove(tegra_surface *surf);
void tegra_surface_cache_surface_update_last_use(tegra_surface *surf);
void tegra_surface_drop_caches(void);
tegra_surface * tegra_surface_cache_take_surface(tegra_device *dev,
                                                 uint32_t width, uint32_t height,
                                                 VdpRGBAFormat rgba_format,
                                                 int output, int video);

VdpGetErrorString                                   vdp_get_error_string;
VdpGetProcAddress                                   vdp_get_proc_address;
VdpGetApiVersion                                    vdp_get_api_version;
VdpGetInformationString                             vdp_get_information_string;
VdpDeviceDestroy                                    vdp_device_destroy;
VdpGenerateCSCMatrix                                vdp_generate_csc_matrix;
VdpVideoSurfaceQueryCapabilities                    vdp_video_surface_query_capabilities;
VdpVideoSurfaceQueryGetPutBitsYCbCrCapabilities     vdp_video_surface_query_get_put_bits_y_cb_cr_capabilities;
VdpVideoSurfaceCreate                               vdp_video_surface_create;
VdpVideoSurfaceDestroy                              vdp_video_surface_destroy;
VdpVideoSurfaceGetParameters                        vdp_video_surface_get_parameters;
VdpVideoSurfaceGetBitsYCbCr                         vdp_video_surface_get_bits_y_cb_cr;
VdpVideoSurfacePutBitsYCbCr                         vdp_video_surface_put_bits_y_cb_cr;
VdpOutputSurfaceQueryCapabilities                   vdp_output_surface_query_capabilities;
VdpOutputSurfaceQueryGetPutBitsNativeCapabilities   vdp_output_surface_query_get_put_bits_native_capabilities;
VdpOutputSurfaceQueryPutBitsIndexedCapabilities     vdp_output_surface_query_put_bits_indexed_capabilities;
VdpOutputSurfaceQueryPutBitsYCbCrCapabilities       vdp_output_surface_query_put_bits_y_cb_cr_capabilities;
VdpOutputSurfaceCreate                              vdp_output_surface_create;
VdpOutputSurfaceDestroy                             vdp_output_surface_destroy;
VdpOutputSurfaceGetParameters                       vdp_output_surface_get_parameters;
VdpOutputSurfaceGetBitsNative                       vdp_output_surface_get_bits_native;
VdpOutputSurfacePutBitsNative                       vdp_output_surface_put_bits_native;
VdpOutputSurfacePutBitsIndexed                      vdp_output_surface_put_bits_indexed;
VdpOutputSurfacePutBitsYCbCr                        vdp_output_surface_put_bits_y_cb_cr;
VdpBitmapSurfaceQueryCapabilities                   vdp_bitmap_surface_query_capabilities;
VdpBitmapSurfaceCreate                              vdp_bitmap_surface_create;
VdpBitmapSurfaceDestroy                             vdp_bitmap_surface_destroy;
VdpBitmapSurfaceGetParameters                       vdp_bitmap_surface_get_parameters;
VdpBitmapSurfacePutBitsNative                       vdp_bitmap_surface_put_bits_native;
VdpOutputSurfaceRenderOutputSurface                 vdp_output_surface_render_output_surface;
VdpOutputSurfaceRenderBitmapSurface                 vdp_output_surface_render_bitmap_surface;
VdpDecoderQueryCapabilities                         vdp_decoder_query_capabilities;
VdpDecoderCreate                                    vdp_decoder_create;
VdpDecoderDestroy                                   vdp_decoder_destroy;
VdpDecoderGetParameters                             vdp_decoder_get_parameters;
VdpDecoderRender                                    vdp_decoder_render;
VdpVideoMixerQueryFeatureSupport                    vdp_video_mixer_query_feature_support;
VdpVideoMixerQueryParameterSupport                  vdp_video_mixer_query_parameter_support;
VdpVideoMixerQueryParameterValueRange               vdp_video_mixer_query_parameter_value_range;
VdpVideoMixerQueryAttributeSupport                  vdp_video_mixer_query_attribute_support;
VdpVideoMixerQueryAttributeValueRange               vdp_video_mixer_query_attribute_value_range;
VdpVideoMixerSetFeatureEnables                      vdp_video_mixer_set_feature_enables;
VdpVideoMixerCreate                                 vdp_video_mixer_create;
VdpVideoMixerSetAttributeValues                     vdp_video_mixer_set_attribute_values;
VdpVideoMixerGetFeatureSupport                      vdp_video_mixer_get_feature_support;
VdpVideoMixerGetFeatureEnables                      vdp_video_mixer_get_feature_enables;
VdpVideoMixerGetParameterValues                     vdp_video_mixer_get_parameter_values;
VdpVideoMixerGetAttributeValues                     vdp_video_mixer_get_attribute_values;
VdpVideoMixerDestroy                                vdp_video_mixer_destroy;
VdpVideoMixerRender                                 vdp_video_mixer_render;
VdpPresentationQueueTargetDestroy                   vdp_presentation_queue_target_destroy;
VdpPresentationQueueCreate                          vdp_presentation_queue_create;
VdpPresentationQueueDestroy                         vdp_presentation_queue_destroy;
VdpPresentationQueueSetBackgroundColor              vdp_presentation_queue_set_background_color;
VdpPresentationQueueGetBackgroundColor              vdp_presentation_queue_get_background_color;
VdpPresentationQueueGetTime                         vdp_presentation_queue_get_time;
VdpPresentationQueueDisplay                         vdp_presentation_queue_display;
VdpPresentationQueueBlockUntilSurfaceIdle           vdp_presentation_queue_block_until_surface_idle;
VdpPresentationQueueQuerySurfaceStatus              vdp_presentation_queue_query_surface_status;
VdpPreemptionCallbackRegister                       vdp_preemption_callback_register;
VdpPresentationQueueTargetCreateX11                 vdp_presentation_queue_target_create_x11;

#endif // VDPAU_TEGRA_H
