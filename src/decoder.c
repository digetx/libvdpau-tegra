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

struct frames_list {
    struct frames_list   *next;
    struct tegra_surface *ref_surf;
};

static int tegra_level_idc(int level)
{
    switch (level) {
    case 11: return 2;
    case 12: return 3;
    case 13: return 4;
    case 20: return 5;
    case 21: return 6;
    case 22: return 7;
    case 30: return 8;
    case 31: return 9;
    case 32: return 10;
    case 40: return 11;
    case 41: return 12;
    case 42: return 13;
    case 50: return 14;
    default:
        break;
    }

    return 15;
}

static struct drm_tegra_bo *alloc_data(tegra_decoder *dec, void **map,
                                       int *dmabuf_fd, uint32_t size)
{
    struct drm_tegra_bo *bo;
    int ret;

    ret = drm_tegra_bo_new(&bo, dec->dev->drm, 0, size);

    if (ret < 0) {
        return NULL;
    }

    ret = drm_tegra_bo_map(bo, map);

    if (ret < 0) {
        drm_tegra_bo_unref(bo);
        return NULL;
    }

    ret = drm_tegra_bo_to_dmabuf(bo, (uint32_t *) dmabuf_fd);

    if (ret < 0) {
        drm_tegra_bo_unref(bo);
        return NULL;
    }

    return bo;
}

static void free_data(struct drm_tegra_bo *bo, int dmabuf_fd)
{
    drm_tegra_bo_unref(bo);
    close(dmabuf_fd);
}

static VdpStatus copy_bitstream_to_dmabuf(tegra_decoder *dec,
                                          uint32_t count,
                                          VdpBitstreamBuffer const *bufs,
                                          struct drm_tegra_bo **bo,
                                          int *data_fd,
                                          bitstream_reader *reader)
{
    char *start, *end;
    char *bitstream;
    uint32_t total_size = 0;
    uint32_t aligned_size = 0;
    int ret, i;

    for (i = 0; i < count; i++) {
        if (bufs[i].struct_version != VDP_BITSTREAM_BUFFER_VERSION) {
            return VDP_STATUS_INVALID_STRUCT_VERSION;
        }

        total_size += bufs[i].bitstream_bytes;
    }

    *bo = NULL;

    /* at first try to allocate / reserve 512KB for common allocations */
    if (total_size < 512 * 1024) {
        aligned_size = ALIGN(total_size, 512 * 1024);
        *bo = alloc_data(dec, (void **)&start, data_fd, aligned_size);
    }

    if (*bo == NULL) {
        /* try again without reservation */
        aligned_size = ALIGN(total_size, 16 * 1024);
        *bo = alloc_data(dec, (void **)&start, data_fd, aligned_size);

        if (*bo == NULL) {
            return VDP_STATUS_RESOURCES;
        }
    }

    total_size = aligned_size;

    ret = sync_dmabuf_write_start(*data_fd);
    if (ret) {
        free_data(*bo, *data_fd);
        return VDP_STATUS_ERROR;
    }

    end = start + total_size;
    bitstream = start;

    for (i = 0; i < count; i++) {
        memcpy(bitstream, bufs[i].bitstream, bufs[i].bitstream_bytes);
        bitstream += bufs[i].bitstream_bytes;
    }
    memset(bitstream, 0x0, end - bitstream);

    ret = sync_dmabuf_write_end(*data_fd);
    if (ret) {
        free_data(*bo, *data_fd);
        return VDP_STATUS_ERROR;
    }

    bitstream = start;
    bitstream_init(reader, bitstream, total_size);

    if (bitstream[0] != 0x00) {
        ErrorMsg("Invalid NAL byte[0] %02X\n", bitstream[0]);
    }

    if(bitstream[1] != 0x00) {
        ErrorMsg("Invalid NAL byte[1] %02X\n", bitstream[0]);
    }

    if (bitstream[2] == 0x01) {
        bitstream_reader_inc_offset(reader, 4);
        return VDP_STATUS_OK;
    }

    if (bitstream[2] != 0x00) {
        ErrorMsg("Invalid NAL byte[2] %02X\n", bitstream[2]);
    }

    if (bitstream[3] == 0x01) {
        bitstream_reader_inc_offset(reader, 5);
        return VDP_STATUS_OK;
    } else {
        ErrorMsg("Invalid NAL byte[3] %02X\n", bitstream[3]);
    }

    return VDP_STATUS_ERROR;
}

static int get_refs_sorted(struct tegra_vde_h264_frame *dpb_frames,
                           VdpReferenceFrameH264 const *referenceFrames,
                           int frame_num_wrap, int32_t max_frame_num,
                           int *ref_frames_with_earlier_poc_num,
                           int32_t delim_pic_order_cnt)
{
    VdpReferenceFrameH264 const *ref;
    struct frames_list nodes[16] = { 0 };
    struct frames_list *list_head = NULL;
    struct frames_list *prev;
    struct frames_list *itr;
    tegra_surface *surf;
    int32_t frame_num;
    int refs_num;
    int i;

    for (i = 0, refs_num = 0; i < 16; i++) {
        ref  = &referenceFrames[i];
        surf = get_surface(ref->surface);

        if (!surf) {
            if (ref->surface != VDP_INVALID_HANDLE) {
                ErrorMsg("invalid refs list\n");
            }
            continue;
        }

        refs_num++;

        if (frame_num_wrap) {
            frame_num = ref->frame_idx;
            frame_num -= max_frame_num;
            surf->frame->frame_num = frame_num & 0x7FFFFF;
        }

        nodes[i].ref_surf = surf;

        if (list_head == NULL) {
            list_head = &nodes[i];
            put_surface(surf);
            continue;
        }

        itr  = list_head;
        prev = NULL;

        while (1) {
            if (itr->ref_surf->pic_order_cnt == surf->pic_order_cnt) {
                ErrorMsg("invalid pic_order_cnt\n");
            }

            if (itr->ref_surf->pic_order_cnt == delim_pic_order_cnt) {
                ErrorMsg("invalid pic_order_cnt\n");
            }

            if (itr->ref_surf->pic_order_cnt <= 0) {
                ErrorMsg("invalid pic_order_cnt\n");
            }

            if (surf->pic_order_cnt < delim_pic_order_cnt) {
                if (surf->pic_order_cnt > itr->ref_surf->pic_order_cnt) {
                    goto insert_node;
                }
                if (itr->ref_surf->pic_order_cnt > delim_pic_order_cnt) {
                    goto insert_node;
                }
            } else {
                if (surf->pic_order_cnt < itr->ref_surf->pic_order_cnt) {
                    goto insert_node;
                }
            }

            if (itr->next == NULL) {
                itr->next = &nodes[i];
                break;
            }

            prev = itr;
            itr  = itr->next;
            continue;

insert_node:
            if (prev != NULL) {
                prev->next = &nodes[i];
            } else {
                list_head = &nodes[i];
            }

            nodes[i].next = itr;
            break;
        }

        put_surface(surf);
    }

    if (!refs_num) {
        ErrorMsg("invalid refs list\n");
    }

    for (i = 0, itr = list_head; itr != NULL; i++, itr = itr->next) {
        dpb_frames[1 + i] = *itr->ref_surf->frame;

        if (itr->ref_surf->pic_order_cnt < delim_pic_order_cnt) {
            *ref_frames_with_earlier_poc_num += 1;
        }
    }

    return refs_num;
}

static int get_refs_dpb_order(struct tegra_vde_h264_frame *dpb_frames,
                              VdpReferenceFrameH264 const *referenceFrames,
                              int frame_num_wrap, int32_t max_frame_num)
{
    VdpReferenceFrameH264 const *ref;
    tegra_surface *surf;
    int32_t frame_num;
    int refs_num;
    int i;

    for (i = 0, refs_num = 0; i < 16; i++) {
        ref  = &referenceFrames[i];
        surf = get_surface(ref->surface);

        if (!surf) {
            if (ref->surface != VDP_INVALID_HANDLE) {
                ErrorMsg("invalid DPB frames list\n");
            }
            continue;
        }

        if (frame_num_wrap) {
            frame_num = ref->frame_idx;
            frame_num -= max_frame_num;
            surf->frame->frame_num = frame_num & 0x7FFFFF;
        }

        dpb_frames[1 + refs_num++] = *surf->frame;

        put_surface(surf);
    }

    if (!refs_num) {
        ErrorMsg("invalid DPB frames list\n");
    }

    return refs_num;
}

#define P_FRAME         0
#define B_FRAME         1
#define I_FRAME         2
#define SP_FRAME        3
#define SI_FRAME        4
#define P_ONLY_FRAME    5
#define B_ONLY_FRAME    6
#define I_ONLY_FRAME    7
#define SP_ONLY_FRAME   8
#define SI_ONLY_FRAME   9

static const char * slice_type_str(int type)
{
    switch (type) {
    case P_FRAME:       return "P";
    case B_FRAME:       return "B";
    case I_FRAME:       return "I";
    case SP_FRAME:      return "SP";
    case SI_FRAME:      return "SI";
    case P_ONLY_FRAME:  return "P_ONLY";
    case B_ONLY_FRAME:  return "B_ONLY";
    case I_ONLY_FRAME:  return "I_ONLY";
    case SP_ONLY_FRAME: return "SP_ONLY";
    case SI_ONLY_FRAME: return "SI_ONLY";
    }

    return "Bad value";
}

static int get_slice_type(bitstream_reader *reader)
{
    uint32_t slice_type;

    bitstream_read_ue(reader);

    slice_type = bitstream_read_ue(reader);

    if (slice_type >= 10) {
        ErrorMsg("invalid slice_type %u\n", slice_type);
    } else {
        DebugMsg("slice_type %s\n", slice_type_str(slice_type));
    }

    return slice_type;
}

static VdpStatus tegra_decode_h264(tegra_decoder *dec, tegra_surface *surf,
                                   VdpPictureInfoH264 const *info,
                                   int bitstream_data_fd,
                                   bitstream_reader *reader)
{
    struct tegra_vde_h264_decoder_ctx ctx;
    struct tegra_vde_h264_frame dpb_frames[17];
    tegra_device *dev = dec->dev;
    int32_t max_frame_num               = 1 << (info->log2_max_frame_num_minus4 + 4);
    int32_t delim_pic_order_cnt         = INT32_MAX;
    int ref_frames_with_earlier_poc_num = 0;
    int slice_type                      = get_slice_type(reader);
    int slice_type_mod                  = slice_type % 5;
    int frame_num_wrap                  = (info->frame_num == 0);
    int refs_num                        = 0;
    int err;

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    if ((info->weighted_pred_flag && (slice_type_mod == 0 || slice_type_mod == 3)) ||
        (info->weighted_bipred_idc == 1 && slice_type_mod == 1)) {
        ErrorMsg("Explicit weighted prediction unimplemented\n");
        return VDP_STATUS_NO_IMPLEMENTATION;
    }

    if (info->entropy_coding_mode_flag) {
        ErrorMsg("CABAC decoding unimplemented\n");
        return VDP_STATUS_NO_IMPLEMENTATION;
    }

    if (dev->vde_fd < 0) {
        dev->vde_fd = open("/dev/tegra_vde", O_RDWR);
        if (dev->vde_fd < 0) {
            perror("Failed to open /dev/tegra_vde");
            return VDP_STATUS_RESOURCES;
        }
    }

    surf->frame->frame_num = info->frame_num;
    surf->pic_order_cnt    = info->field_order_cnt[0];
    surf->frame->flags    &= ~FLAG_B_FRAME;
    surf->frame->flags    |= (slice_type == B_FRAME) ? FLAG_B_FRAME : 0;

    dpb_frames[0]        = *surf->frame;
    dpb_frames[0].flags |= info->is_reference ? FLAG_REFERENCE : 0;
    dpb_frames[0].flags |= (slice_type_mod == B_FRAME) ? FLAG_B_FRAME : 0;

    if (slice_type_mod != I_FRAME) {
        if (info->pic_order_cnt_type == 0) {
            if (slice_type_mod == B_FRAME) {
                delim_pic_order_cnt = surf->pic_order_cnt;
            }

            if (delim_pic_order_cnt <= 0) {
                ErrorMsg("invalid delim_pic_order_cnt %d\n",
                         delim_pic_order_cnt);
                return VDP_STATUS_ERROR;
            }

            refs_num = get_refs_sorted(dpb_frames, info->referenceFrames,
                                       frame_num_wrap, max_frame_num,
                                       &ref_frames_with_earlier_poc_num,
                                       delim_pic_order_cnt);
        } else {
            refs_num = get_refs_dpb_order(dpb_frames, info->referenceFrames,
                                          frame_num_wrap, max_frame_num);
        }
    }

    ctx.bitstream_data_fd                   = bitstream_data_fd;
    ctx.bitstream_data_offset               = 0;
    ctx.dpb_frames_nb                       = 1 + refs_num;
    ctx.dpb_frames_ptr                      = (uintptr_t) dpb_frames;
    ctx.dpb_ref_frames_with_earlier_poc_nb  = ref_frames_with_earlier_poc_num;
    ctx.baseline_profile                    = dec->is_baseline_profile;
    ctx.level_idc                           = tegra_level_idc(51);
    ctx.log2_max_pic_order_cnt_lsb          = info->log2_max_pic_order_cnt_lsb_minus4 + 4;
    ctx.log2_max_frame_num                  = info->log2_max_frame_num_minus4 + 4;
    ctx.pic_order_cnt_type                  = info->pic_order_cnt_type;
    ctx.direct_8x8_inference_flag           = info->direct_8x8_inference_flag;
    ctx.pic_width_in_mbs                    = dec->width  / 16;
    ctx.pic_height_in_mbs                   = dec->height / 16;
    ctx.pic_init_qp                         = info->pic_init_qp_minus26 + 26;
    ctx.deblocking_filter_control_present_flag = info->deblocking_filter_control_present_flag;
    ctx.constrained_intra_pred_flag         = info->constrained_intra_pred_flag;
    ctx.chroma_qp_index_offset              = info->chroma_qp_index_offset & 0x1F;
    ctx.pic_order_present_flag              = info->pic_order_present_flag;
    ctx.num_ref_idx_l0_active_minus1        = info->num_ref_idx_l0_active_minus1;
    ctx.num_ref_idx_l1_active_minus1        = info->num_ref_idx_l1_active_minus1;
    ctx.reserved                            = 0;

repeat:
    err = ioctl(dev->vde_fd, TEGRA_VDE_IOCTL_DECODE_H264, &ctx);
    if (err != 0) {
        if (errno == EINTR || errno == EAGAIN) {
            goto repeat;
        }
        return VDP_STATUS_ERROR;
    }

    host1x_pixelbuffer_check_guard(surf->pixbuf);

    return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_query_capabilities(VdpDevice device,
                                         VdpDecoderProfile profile,
                                         VdpBool *is_supported,
                                         uint32_t *max_level,
                                         uint32_t *max_macroblocks,
                                         uint32_t *max_width,
                                         uint32_t *max_height)
{
    *max_width = 2032;
    *max_height = 2032;
    *max_macroblocks = 9000;

    switch (profile) {
    case VDP_DECODER_PROFILE_H264_BASELINE:
    case VDP_DECODER_PROFILE_H264_CONSTRAINED_BASELINE:
    case VDP_DECODER_PROFILE_H264_MAIN:
        *max_level = VDP_DECODER_LEVEL_H264_5_1;
        *is_supported = VDP_TRUE;
        break;
    default:
        *is_supported = VDP_FALSE;
        break;
    }

    return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_create(VdpDevice device,
                             VdpDecoderProfile profile,
                             uint32_t width, uint32_t height,
                             uint32_t max_references,
                             VdpDecoder *decoder)
{
    tegra_device *dev = get_device(device);
    tegra_decoder *dec;
    VdpDecoder i;
    int is_baseline_profile = 0;

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    switch (profile) {
    case VDP_DECODER_PROFILE_H264_BASELINE:
    case VDP_DECODER_PROFILE_H264_CONSTRAINED_BASELINE:
        is_baseline_profile = 1;
        break;
    case VDP_DECODER_PROFILE_H264_MAIN:
    case VDP_DECODER_PROFILE_H264_HIGH: /* MPlayer compatibility */
        break;
    default:
        put_device(dev);
        return VDP_STATUS_INVALID_DECODER_PROFILE;
    }

    pthread_mutex_lock(&global_lock);

    for (i = 0; i < MAX_DECODERS_NB; i++) {
        dec = __get_decoder(i);

        if (dec == NULL) {
            dec = calloc(1, sizeof(tegra_decoder));
            set_decoder(i, dec);
            break;
        }
    }

    pthread_mutex_unlock(&global_lock);

    if (i == MAX_SURFACES_NB || dec == NULL) {
        put_device(dev);
        return VDP_STATUS_RESOURCES;
    }

    atomic_set(&dec->refcnt, 1);
    ref_device(dev);
    dec->dev = dev;
    dec->width = ALIGN(width, 16);
    dec->height = ALIGN(height, 16);
    dec->is_baseline_profile = is_baseline_profile;

    *decoder = i;

    put_device(dev);

    return VDP_STATUS_OK;
}

void ref_decoder(tegra_decoder *dec)
{
    atomic_inc(&dec->refcnt);
}

VdpStatus unref_decoder(tegra_decoder *dec)
{
    if (!atomic_dec_and_test(&dec->refcnt)) {
        return VDP_STATUS_OK;
    }

    unref_device(dec->dev);
    free(dec);

    return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_destroy(VdpDecoder decoder)
{
    tegra_decoder *dec = get_decoder(decoder);

    if (dec == NULL) {
        return VDP_INVALID_HANDLE;
    }

    set_decoder(decoder, NULL);
    put_decoder(dec);

    unref_decoder(dec);

    return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_get_parameters(VdpDecoder decoder,
                                     VdpDecoderProfile *profile,
                                     uint32_t *width, uint32_t *height)
{
    if (width)
        *width = ALIGN(*width, 16);

    if (height)
        *height = ALIGN(*height, 16);

    return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_render(VdpDecoder decoder,
                             VdpVideoSurface target,
                             VdpPictureInfo const *picture_info,
                             uint32_t bitstream_buffer_count,
                             VdpBitstreamBuffer const *bufs)
{
    tegra_decoder *dec = get_decoder(decoder);
    tegra_surface *orig, *surf = get_surface(target);
    struct drm_tegra_bo *bitstream_bo;
    bitstream_reader bitstream_reader;
    int bitstream_data_fd;
    VdpStatus ret;

    if (dec == NULL || surf == NULL) {
        put_surface(surf);
        put_decoder(dec);
        return VDP_STATUS_INVALID_HANDLE;
    }

    ret = copy_bitstream_to_dmabuf(dec, bitstream_buffer_count, bufs,
                                   &bitstream_bo, &bitstream_data_fd,
                                   &bitstream_reader);

    if (ret != VDP_STATUS_OK) {
        put_surface(surf);
        put_decoder(dec);
        return ret;
    }

    orig = surf;
    surf = shared_surface_swap_video(surf);

    if (surf != orig) {
        put_surface(orig);
        ref_surface(surf);
    }

    ret = tegra_decode_h264(dec, surf, picture_info,
                            bitstream_data_fd, &bitstream_reader);

    free_data(bitstream_bo, bitstream_data_fd);

    if (ret != VDP_STATUS_OK) {
        put_surface(surf);
        put_decoder(dec);
        return ret;
    }

    surf->flags |= SURFACE_DATA_NEEDS_SYNC;

    put_surface(surf);
    put_decoder(dec);

    return VDP_STATUS_OK;
}
