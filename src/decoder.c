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
    uint32_t bo_flags = 0;
    int drm_ver;
    int ret;

    drm_ver = drm_tegra_version(dec->dev->drm);

    if (drm_ver >= GRATE_KERNEL_DRM_VERSION)
        bo_flags |= DRM_TEGRA_GEM_CREATE_DONT_KMAP;

    ret = drm_tegra_bo_new(&bo, dec->dev->drm, bo_flags, size);

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
                                          uint32_t *bitstream_data_size,
                                          uint32_t *bitstream_size,
                                          bitstream_reader *reader)
{
    char *start, *end;
    char *bitstream;
    uint32_t total_size = 0;
    uint32_t aligned_size = 0;
    int i;

    for (i = 0; i < count; i++) {
        if (bufs[i].struct_version != VDP_BITSTREAM_BUFFER_VERSION) {
            return VDP_STATUS_INVALID_STRUCT_VERSION;
        }

        total_size += bufs[i].bitstream_bytes;
    }

    *bitstream_data_size = total_size;
    *bo = NULL;

    /* at first try to allocate / reserve 512KB for common allocations */
    if (total_size + 16 <= 512 * 1024) {
        aligned_size = ALIGN(total_size + 16, 512 * 1024);
        *bo = alloc_data(dec, (void **)&start, data_fd, aligned_size);
    }

    if (*bo == NULL) {
        /* try again without reservation */
        aligned_size = ALIGN(total_size + 16, dec->bitstream_min_size);
        *bo = alloc_data(dec, (void **)&start, data_fd, aligned_size);

        if (*bo == NULL) {
            return VDP_STATUS_RESOURCES;
        }
    }

    total_size = aligned_size;
    *bitstream_size = aligned_size;

    end = start + total_size;
    bitstream = start;

    for (i = 0; i < count; i++) {
        memcpy(bitstream, bufs[i].bitstream, bufs[i].bitstream_bytes);
        bitstream += bufs[i].bitstream_bytes;
    }
    memset(bitstream, 0x0, end - bitstream);

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
        surf = get_surface_video(ref->surface);

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
        surf = get_surface_video(ref->surface);

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
    struct tegra_vde_h264_decoder_ctx_v1 ctx_v1;
    struct tegra_vde_h264_decoder_ctx ctx;
    union f_u {
        struct tegra_vde_h264_frame_v1 dpb_frames_v1[17];
        struct tegra_vde_h264_frame dpb_frames[17];
    } f_u;
    tegra_device *dev = dec->dev;
    int32_t max_frame_num               = 1 << (info->log2_max_frame_num_minus4 + 4);
    int32_t delim_pic_order_cnt         = INT32_MAX;
    int ref_frames_with_earlier_poc_num = 0;
    int slice_type                      = get_slice_type(reader);
    int slice_type_mod                  = slice_type % 5;
    int frame_num_wrap                  = (info->frame_num == 0);
    int refs_num                        = 0;
    int err;
    int i;

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

    f_u.dpb_frames[0]        = *surf->frame;
    f_u.dpb_frames[0].flags |= info->is_reference ? FLAG_REFERENCE : 0;
    f_u.dpb_frames[0].flags |= (slice_type_mod == B_FRAME) ? FLAG_B_FRAME : 0;

    memset(&f_u.dpb_frames[0].reserved, 0, sizeof(f_u.dpb_frames[0].reserved));

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

            refs_num = get_refs_sorted(f_u.dpb_frames, info->referenceFrames,
                                       frame_num_wrap, max_frame_num,
                                       &ref_frames_with_earlier_poc_num,
                                       delim_pic_order_cnt);
        } else {
            refs_num = get_refs_dpb_order(f_u.dpb_frames, info->referenceFrames,
                                          frame_num_wrap, max_frame_num);
        }
    }

    ctx.bitstream_data_fd                   = bitstream_data_fd;
    ctx.bitstream_data_offset               = 0;
    ctx.dpb_frames_nb                       = 1 + refs_num;
    ctx.dpb_frames_ptr                      = (uintptr_t) f_u.dpb_frames;
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

    memset(&ctx.reserved, 0, sizeof(ctx.reserved));

to_v1:
    if (dec->v1) {
        struct tegra_vde_h264_frame f;

        for (i = 0; i < ctx.dpb_frames_nb; i++) {
            f = f_u.dpb_frames[i];

            f_u.dpb_frames_v1[i].y_fd = f.y_fd;
            f_u.dpb_frames_v1[i].cb_fd = f.cb_fd;
            f_u.dpb_frames_v1[i].cr_fd = f.cr_fd;
            f_u.dpb_frames_v1[i].aux_fd = f.aux_fd;
            f_u.dpb_frames_v1[i].y_offset = f.y_offset;
            f_u.dpb_frames_v1[i].cb_offset = f.cb_offset;
            f_u.dpb_frames_v1[i].cr_offset = f.cr_offset;
            f_u.dpb_frames_v1[i].aux_offset = f.aux_offset;
            f_u.dpb_frames_v1[i].frame_num = f.frame_num;
            f_u.dpb_frames_v1[i].flags = f.flags;
            f_u.dpb_frames_v1[i].reserved = 0;
        }

        ctx_v1.bitstream_data_fd                   = ctx.bitstream_data_fd;
        ctx_v1.bitstream_data_offset               = ctx.bitstream_data_offset;
        ctx_v1.dpb_frames_nb                       = ctx.dpb_frames_nb;
        ctx_v1.dpb_frames_ptr                      = ctx.dpb_frames_ptr;
        ctx_v1.dpb_ref_frames_with_earlier_poc_nb  = ctx.dpb_ref_frames_with_earlier_poc_nb;
        ctx_v1.baseline_profile                    = ctx.baseline_profile;
        ctx_v1.level_idc                           = ctx.level_idc;
        ctx_v1.log2_max_pic_order_cnt_lsb          = ctx.log2_max_pic_order_cnt_lsb;
        ctx_v1.log2_max_frame_num                  = ctx.log2_max_frame_num;
        ctx_v1.pic_order_cnt_type                  = ctx.pic_order_cnt_type;
        ctx_v1.direct_8x8_inference_flag           = ctx.direct_8x8_inference_flag;
        ctx_v1.pic_width_in_mbs                    = ctx.pic_width_in_mbs;
        ctx_v1.pic_height_in_mbs                   = ctx.pic_height_in_mbs;
        ctx_v1.pic_init_qp                         = ctx.pic_init_qp;
        ctx_v1.deblocking_filter_control_present_flag = ctx.deblocking_filter_control_present_flag;
        ctx_v1.constrained_intra_pred_flag         = ctx.constrained_intra_pred_flag;
        ctx_v1.chroma_qp_index_offset              = ctx.chroma_qp_index_offset;
        ctx_v1.pic_order_present_flag              = ctx.pic_order_present_flag;
        ctx_v1.num_ref_idx_l0_active_minus1        = ctx.num_ref_idx_l0_active_minus1;
        ctx_v1.num_ref_idx_l1_active_minus1        = ctx.num_ref_idx_l1_active_minus1;
        ctx_v1.reserved                            = 0;
    }

    if (dec->v1)
        err = tegra_ioctl(dev->vde_fd, TEGRA_VDE_IOCTL_DECODE_H264_V1, &ctx_v1);
    else
        err = tegra_ioctl(dev->vde_fd, TEGRA_VDE_IOCTL_DECODE_H264, &ctx);

    if (err != 0) {
        if (errno == ENOTTY && !dec->v1) {
            DebugMsg("switching to v1 IOCTL\n");
            dec->v1 = true;
            goto to_v1;
        }
        return VDP_STATUS_ERROR;
    }

    host1x_pixelbuffer_check_guard(surf->pixbuf);

    return VDP_STATUS_OK;
}

static void h264_vdpau_picture_to_v4l2(tegra_decoder *dec,
                                       VdpPictureInfoH264 const *info,
                                       struct v4l2_ctrl_h264_decode_params *decode,
                                       struct v4l2_ctrl_h264_sps *sps,
                                       struct v4l2_ctrl_h264_pps *pps)
{
    int32_t max_frame_num = 1 << (info->log2_max_frame_num_minus4 + 4);
    VdpReferenceFrameH264 const *ref;
    tegra_surface *surf;
    unsigned int i;

    decode->frame_num = info->frame_num;
    decode->nal_ref_idc = info->is_reference;
    decode->top_field_order_cnt = info->field_order_cnt[0];
    decode->bottom_field_order_cnt = info->field_order_cnt[1];

    sps->level_idc = 51;
    sps->chroma_format_idc = 1;
    sps->pic_order_cnt_type = info->pic_order_cnt_type;
    sps->pic_width_in_mbs_minus1 = dec->width / 16 - 1;
    sps->profile_idc = dec->is_baseline_profile ? 66 : 77;
    sps->pic_height_in_map_units_minus1 = dec->height / 16 - 1;
    sps->log2_max_frame_num_minus4 = info->log2_max_frame_num_minus4;
    sps->log2_max_pic_order_cnt_lsb_minus4 = info->log2_max_pic_order_cnt_lsb_minus4;

    if (info->frame_mbs_only_flag)
        sps->flags |= V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY;

    if (info->mb_adaptive_frame_field_flag)
        sps->flags |= V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD;

    if (info->direct_8x8_inference_flag)
        sps->flags |= V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE;

    if (info->delta_pic_order_always_zero_flag)
        sps->flags |= V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO;

    pps->weighted_bipred_idc = info->weighted_bipred_idc;
    pps->pic_init_qp_minus26 = info->pic_init_qp_minus26;
    pps->chroma_qp_index_offset = info->chroma_qp_index_offset;
    pps->second_chroma_qp_index_offset = info->second_chroma_qp_index_offset;
    pps->num_ref_idx_l0_default_active_minus1 = info->num_ref_idx_l0_active_minus1;
    pps->num_ref_idx_l1_default_active_minus1 = info->num_ref_idx_l1_active_minus1;

    if (info->entropy_coding_mode_flag)
        pps->flags |= V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE;

    if (info->weighted_pred_flag)
        pps->flags |= V4L2_H264_PPS_FLAG_WEIGHTED_PRED;

    if (info->transform_8x8_mode_flag)
        pps->flags |= V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE;

    if (info->constrained_intra_pred_flag)
        pps->flags |= V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED;

    if (info->pic_order_present_flag)
        pps->flags |= V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT;

    if (info->deblocking_filter_control_present_flag)
        pps->flags |= V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT;

    if (info->redundant_pic_cnt_present_flag)
        pps->flags |= V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT;

    for (i = 0; i < 16; i++) {
        ref = &info->referenceFrames[i];
        surf = get_surface_video(ref->surface);

        if (!surf) {
            if (ref->surface != VDP_INVALID_HANDLE) {
                ErrorMsg("invalid DPB frames list\n");
            }
            continue;
        }

        if (surf->v4l2.buf_idx < 0) {
            ErrorMsg("invalid DPB frames list\n");
            continue;
        }

        if (dec->v4l2.surfaces[surf->v4l2.buf_idx] != surf) {
            ErrorMsg("invalid DPB frames list\n");
            continue;
        }

        if (info->frame_num == 0)
            decode->dpb[i].pic_num = surf->frame->frame_num - max_frame_num;
        else
            decode->dpb[i].pic_num = surf->frame->frame_num;

        decode->dpb[i].fields = V4L2_H264_FRAME_REF;
        decode->dpb[i].frame_num = surf->frame->frame_num;
        decode->dpb[i].top_field_order_cnt = surf->pic_order_cnt;
        decode->dpb[i].bottom_field_order_cnt = surf->pic_order_cnt;
        decode->dpb[i].reference_ts = v4l2_timeval_to_ns(&dec->v4l2.timestamps[surf->v4l2.buf_idx]);

        decode->dpb[i].flags  = V4L2_H264_DPB_ENTRY_FLAG_ACTIVE;
        decode->dpb[i].flags |= V4L2_H264_DPB_ENTRY_FLAG_VALID;

        if (ref->is_long_term)
            decode->dpb[i].flags |= V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM;

        put_surface(surf);
    }
}

static int enqueue_surface_v4l2(tegra_decoder *dec, tegra_surface *surf,
                                unsigned int index)
{
    unsigned int offsets[3];
    unsigned int sizes[3];
    int dmafds[3];
    int err;

    dmafds[0] = surf->frame->y_fd;
    dmafds[1] = surf->frame->cb_fd;
    dmafds[2] = surf->frame->cr_fd;

    offsets[0] = surf->frame->y_offset;
    offsets[1] = surf->frame->cb_offset;
    offsets[2] = surf->frame->cr_offset;

    drm_tegra_bo_get_size(surf->y_bo, &sizes[0]);
    drm_tegra_bo_get_size(surf->cb_bo, &sizes[1]);
    drm_tegra_bo_get_size(surf->cr_bo, &sizes[2]);

    err = v4l2_queue_buffer(dec->v4l2.video_fd, -1,
                            V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                            NULL, index, dmafds, sizes, NULL, offsets, 3,
                            surf->frame->flags);
    if (err)
        return err;

    return 0;
}

static VdpStatus tegra_decode_h264_v4l2(tegra_decoder *dec, tegra_surface *surf,
                                        VdpPictureInfoH264 const *info,
                                        int bitstream_data_fd,
                                        unsigned int bitstream_data_size,
                                        unsigned int bitstream_size,
                                        bitstream_reader *reader)
{
    struct v4l2_ctrl_h264_decode_params decode = { 0 };
    unsigned int slice_type = get_slice_type(reader);
    unsigned int slice_type_mod = slice_type % 5;
    unsigned int i, buf_idx, buf_flags = 0;
    unsigned int bitstream_offset = 0;
    struct v4l2_ctrl_h264_sps sps = { 0 };
    struct v4l2_ctrl_h264_pps pps = { 0 };
    bool decode_error = false;
    int err;

    if ((info->weighted_pred_flag && (slice_type_mod == 0 || slice_type_mod == 3)) ||
        (info->weighted_bipred_idc == 1 && slice_type_mod == 1)) {
        ErrorMsg("Explicit weighted prediction unimplemented\n");
        return VDP_STATUS_NO_IMPLEMENTATION;
    }

    if (info->entropy_coding_mode_flag) {
        ErrorMsg("CABAC decoding unimplemented\n");
        return VDP_STATUS_NO_IMPLEMENTATION;
    }

    if (slice_type_mod == I_FRAME)
        buf_flags |= V4L2_BUF_FLAG_KEYFRAME;
    if (slice_type_mod == P_FRAME)
        buf_flags |= V4L2_BUF_FLAG_PFRAME;
    if (slice_type_mod == B_FRAME)
        buf_flags |= V4L2_BUF_FLAG_BFRAME;

    if (surf->v4l2.buf_idx >= 0) {
        for (buf_idx = 0; buf_idx < dec->v4l2.num_buffers; buf_idx++) {
            struct timeval ref_timestamp = surf->v4l2.timestamp;

            if (dec->v4l2.surfaces[buf_idx] == surf &&
                !memcmp(&ref_timestamp, &dec->v4l2.timestamps[buf_idx],
                        sizeof(ref_timestamp))) {
                goto reinit_surface;
            }
        }
    }

    for (buf_idx = 0; buf_idx < dec->v4l2.num_buffers; buf_idx++) {
        struct timeval ref_timestamp = { 0 };

        if (!memcmp(&ref_timestamp, &dec->v4l2.timestamps[buf_idx],
                    sizeof(ref_timestamp))) {
            goto reinit_surface;
        }
    }

    for (buf_idx = 0; buf_idx < dec->v4l2.num_buffers; buf_idx++) {
        bool busy = false;

        for (i = 0; i < 16; i++) {
            VdpReferenceFrameH264 const *ref = &info->referenceFrames[i];
            tegra_surface *ref_surf = get_surface_video(ref->surface);

            if (ref_surf) {
                struct timeval ref_timestamp = ref_surf->v4l2.timestamp;

                put_surface(ref_surf);

                if (!memcmp(&ref_timestamp, &dec->v4l2.timestamps[buf_idx],
                            sizeof(ref_timestamp))) {
                    busy = true;
                    break;
                }
            }
        }

        if (!busy)
            break;
    }

    if (buf_idx == dec->v4l2.num_buffers) {
        ErrorMsg("V4L2 frame buffer overflow\n");
        return VDP_STATUS_ERROR;
    }

reinit_surface:
    gettimeofday(&surf->v4l2.timestamp, NULL);
    surf->pic_order_cnt     = info->field_order_cnt[0];
    surf->frame->frame_num  = info->frame_num;

    h264_vdpau_picture_to_v4l2(dec, info, &decode, &sps, &pps);

    err = media_request_reinit(dec->v4l2.request_fd);
    if (err)
        return VDP_STATUS_ERROR;

    err = v4l2_set_control(dec->v4l2.video_fd, dec->v4l2.request_fd,
                           V4L2_CID_STATELESS_H264_DECODE_PARAMS,
                           &decode, sizeof(decode));
    if (err)
        return VDP_STATUS_ERROR;

    err = v4l2_set_control(dec->v4l2.video_fd, dec->v4l2.request_fd,
                           V4L2_CID_STATELESS_H264_SPS,
                           &sps, sizeof(sps));
    if (err)
        return VDP_STATUS_ERROR;

    err = v4l2_set_control(dec->v4l2.video_fd, dec->v4l2.request_fd,
                           V4L2_CID_STATELESS_H264_PPS,
                           &pps, sizeof(pps));
    if (err)
        return VDP_STATUS_ERROR;

    err = v4l2_queue_buffer(dec->v4l2.video_fd, dec->v4l2.request_fd,
                            V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                            &surf->v4l2.timestamp, 0,
                            &bitstream_data_fd,
                            &bitstream_size,
                            &bitstream_data_size,
                            &bitstream_offset,
                            1, buf_flags);
    if (err)
        return VDP_STATUS_ERROR;

    err = enqueue_surface_v4l2(dec, surf, buf_idx);
    if (err)
            goto dequeue_bitstream;

    err = media_request_queue(dec->v4l2.request_fd);
    if (err)
            goto dequeue_surf;

    err = media_request_wait_completion(dec->v4l2.request_fd);
    if (err)
            goto dequeue_surf;

    surf->v4l2.buf_idx = buf_idx;
    dec->v4l2.surfaces[buf_idx] = surf;
    dec->v4l2.timestamps[buf_idx] = surf->v4l2.timestamp;

dequeue_surf:
    v4l2_dequeue_buffer(dec->v4l2.video_fd,
                        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 3,
                        &decode_error);

dequeue_bitstream:
    v4l2_dequeue_buffer(dec->v4l2.video_fd,
                        V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 1,
                        NULL);

    host1x_pixelbuffer_check_guard(surf->pixbuf);

    return (err || decode_error) ? VDP_STATUS_ERROR : VDP_STATUS_OK;
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

static bool init_v4l2(tegra_decoder *dec)
{
    unsigned int i;
    char path[256];
    char *env_str;
    int err, fd;

    dec->v4l2.video_fd = -1;
    dec->v4l2.media_fd = -1;
    dec->v4l2.request_fd = -1;

    env_str = getenv("VDPAU_TEGRA_FORCE_VDE_UAPI");
    if (env_str && strcmp(env_str, "0")) {
        DebugMsg("VDE UAPI enforced\n");
        return false;
    }

    for (i = 0; i < 256; i++) {
        struct v4l2_capability capability = {};

        sprintf(path, "/dev/video%u", i);

        fd = open(path, O_NONBLOCK);
        if (fd < 0)
            continue;

        err = ioctl(fd, VIDIOC_QUERYCAP, &capability);
        if (!err) {
            if (!strcmp((char *)capability.driver, "tegra-vde"))
                break;
        }

        close(fd);
    }

    if (i == 256)
        goto v4l_fail;

    dec->v4l2.video_fd = fd;

    for (i = 0; i < 256; i++) {
        struct media_device_info info = {};

        sprintf(path, "/dev/media%u", i);

        fd = open(path, 0);
        if (fd < 0)
            continue;

        err = ioctl(fd, MEDIA_IOC_DEVICE_INFO, &info);
        if (!err) {
            if (!strcmp(info.driver, "tegra-vde"))
                break;
        }

        close(fd);
    }

    if (i == 256)
        goto v4l_fail;

    dec->v4l2.media_fd = fd;

    if (v4l2_set_format(dec->v4l2.video_fd,
                        V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                        V4L2_PIX_FMT_H264_SLICE,
                        dec->width, dec->height))
        goto v4l_fail;

    if (v4l2_set_format(dec->v4l2.video_fd,
                        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                        V4L2_PIX_FMT_YUV420M,
                        dec->width, dec->height))
        goto v4l_fail;

    if (v4l2_get_format(dec->v4l2.video_fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                        NULL, NULL, NULL, &dec->bitstream_min_size, NULL))
        goto v4l_fail;

    dec->v4l2.num_buffers = 1;

    if (v4l2_request_buffers(dec->v4l2.video_fd,
                             V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                             &dec->v4l2.num_buffers))
        goto v4l_fail;

    dec->v4l2.num_buffers = MAX_V4L2_BUFFERS;

    if (v4l2_request_buffers(dec->v4l2.video_fd,
                             V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                             &dec->v4l2.num_buffers))
    {
        if (dec->v4l2.num_buffers < MIN_V4L2_BUFFERS)
            goto v4l_fail;
    }

    err = v4l2_set_stream(dec->v4l2.video_fd,
                          V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                          true);
    if (err)
        goto v4l_fail;

    err = v4l2_set_stream(dec->v4l2.video_fd,
                          V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                          true);
    if (err)
        goto v4l_fail;

    fd = media_request_alloc(dec->v4l2.media_fd);
    if (fd < 0)
        goto v4l_fail;

    dec->v4l2.request_fd = fd;
    dec->v4l2.presents = true;

    return true;

v4l_fail:
    if (dec->v4l2.request_fd >= 0)
        close(dec->v4l2.request_fd);
    if (dec->v4l2.media_fd >= 0)
        close(dec->v4l2.media_fd);
    if (dec->v4l2.video_fd >= 0)
        close(dec->v4l2.video_fd);

    dec->v4l2.video_fd = -1;
    dec->v4l2.media_fd = -1;
    dec->v4l2.request_fd = -1;

    return false;
}

static void deinit_v4l2(tegra_decoder *dec)
{
    if (dec->v4l2.presents) {
        close(dec->v4l2.request_fd);
        close(dec->v4l2.media_fd);
        close(dec->v4l2.video_fd);

        dec->v4l2.video_fd = -1;
        dec->v4l2.media_fd = -1;
        dec->v4l2.request_fd = -1;

        dec->v4l2.presents = false;
    }
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
    dec->bitstream_min_size = 128 * 1024;

    if (init_v4l2(dec))
        DebugMsg("V4L2 initialized\n");
    else
        DebugMsg("V4L2 support undetected\n");

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

    deinit_v4l2(dec);
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
    tegra_surface *orig, *surf = get_surface_video(target);
    struct drm_tegra_bo *bitstream_bo;
    bitstream_reader bitstream_reader;
    uint32_t bitstream_data_size;
    uint32_t bitstream_size;
    int bitstream_data_fd;
    VdpTime time = 0;
    VdpStatus ret;

    if (dec == NULL || surf == NULL) {
        put_surface(surf);
        put_decoder(dec);
        return VDP_STATUS_INVALID_HANDLE;
    }

    if (tegra_vdpau_debug)
        time = get_time();

    ret = copy_bitstream_to_dmabuf(dec, bitstream_buffer_count, bufs,
                                   &bitstream_bo, &bitstream_data_fd,
                                   &bitstream_data_size,
                                   &bitstream_size,
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

    if (dec->v4l2.present)
        ret = tegra_decode_h264_v4l2(dec, surf, picture_info,
                                     bitstream_data_fd,
                                     bitstream_data_size,
                                     bitstream_size,
                                     &bitstream_reader);
    else
        ret = tegra_decode_h264(dec, surf, picture_info,
                                bitstream_data_fd, &bitstream_reader);

    free_data(bitstream_bo, bitstream_data_fd);

    if (ret != VDP_STATUS_OK) {
        put_surface(surf);
        put_decoder(dec);
        return ret;
    }

    put_surface(surf);
    put_decoder(dec);

    DebugMsg("waited for %llu usec\n", (get_time() - time) / 1000);

    return VDP_STATUS_OK;
}
