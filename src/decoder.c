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

#define MAX_BITSTREAM_SIZE  (512 * 1024)

struct frames_list {
    struct frames_list   *restrict next;
    struct tegra_surface *restrict ref_surf;
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

static VdpStatus copy_bitstream_to_dmabuf(tegra_decoder *dec,
                                          uint32_t count,
                                          VdpBitstreamBuffer const *bufs)
{
    char *bitstream = dec->bitstream_data;
    int bytes, i;
    int ret;

    ret = sync_dmabuf_write_start(dec->bitstream_data_fd);

    assert(ret == 0);

    if (ret) {
        return VDP_STATUS_ERROR;
    }

    for (i = 0, bytes = 0; i < count; i++) {
        bytes += bufs[i].bitstream_bytes;

        if (bufs[i].struct_version != VDP_BITSTREAM_BUFFER_VERSION) {
            return VDP_STATUS_INVALID_STRUCT_VERSION;
        }

        assert(bytes <= MAX_BITSTREAM_SIZE);

        memcpy(bitstream, bufs[i].bitstream, bufs[i].bitstream_bytes);

        bitstream += bufs[i].bitstream_bytes;
    }

    ret = sync_dmabuf_write_end(dec->bitstream_data_fd);

    assert(ret == 0);

    if (ret) {
        return VDP_STATUS_ERROR;
    }

    bitstream = dec->bitstream_data;
    bitstream_init(&dec->reader, dec->bitstream_data, bytes);

    assert(bitstream[0] == 0x00);
    assert(bitstream[1] == 0x00);

    if (bitstream[2] == 0x01) {
        bitstream_reader_inc_offset(&dec->reader, 4);
        return VDP_STATUS_OK;
    }

    assert(bitstream[2] == 0x00);

    if (bitstream[3] == 0x01) {
        bitstream_reader_inc_offset(&dec->reader, 5);
        return VDP_STATUS_OK;
    }

    assert(0);

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
    tegra_surface *restrict surf;
    int32_t frame_num;
    int refs_num;
    int i;

    for (i = 0, refs_num = 0; i < 16; i++) {
        ref  = &referenceFrames[i];
        surf = get_surface(ref->surface);

        if (!surf) {
            assert(ref->surface == VDP_INVALID_HANDLE);
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
            continue;
        }

        itr  = list_head;
        prev = NULL;

        while (1) {
            assert(itr->ref_surf->pic_order_cnt != surf->pic_order_cnt);
            assert(itr->ref_surf->pic_order_cnt != delim_pic_order_cnt);
            assert(itr->ref_surf->pic_order_cnt > 0);

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
    }

    assert(refs_num != 0);

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
            assert(ref->surface == VDP_INVALID_HANDLE);
            continue;
        }

        if (frame_num_wrap) {
            frame_num = ref->frame_idx;
            frame_num -= max_frame_num;
            surf->frame->frame_num = frame_num & 0x7FFFFF;
        }

        dpb_frames[1 + refs_num++] = *surf->frame;
    }

    assert(refs_num != 0);

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

    assert(slice_type < 10);

    if (0) {
        printf("slice_type %s\n", slice_type_str(slice_type));
    }

    return slice_type;
}

static VdpStatus tegra_decode_h264(tegra_decoder *dec, tegra_surface *surf,
                                   VdpPictureInfoH264 const *info)
{
    struct tegra_vde_h264_decoder_ctx ctx;
    struct tegra_vde_h264_frame dpb_frames[17];
    tegra_device *dev = dec->dev;
    int32_t max_frame_num               = 1 << (info->log2_max_frame_num_minus4 + 4);
    int32_t delim_pic_order_cnt         = INT32_MAX;
    int ref_frames_with_earlier_poc_num = 0;
    int slice_type                      = get_slice_type(&dec->reader);
    int slice_type_mod                  = slice_type % 5;
    int frame_num_wrap                  = (info->frame_num == 0);
    int refs_num                        = 0;

    if (dev == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
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

            assert(delim_pic_order_cnt > 0);

            refs_num = get_refs_sorted(dpb_frames, info->referenceFrames,
                                       frame_num_wrap, max_frame_num,
                                       &ref_frames_with_earlier_poc_num,
                                       delim_pic_order_cnt);
        } else {
            refs_num = get_refs_dpb_order(dpb_frames, info->referenceFrames,
                                          frame_num_wrap, max_frame_num);
        }
    }

    ctx.bitstream_data_fd                   = dec->bitstream_data_fd;
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

    if (ioctl(dev->vde_fd, TEGRA_VDE_IOCTL_DECODE_H264, &ctx) != 0) {
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

static struct drm_tegra_bo *alloc_data(struct drm_tegra *drm, void **map,
                                       int *dmabuf_fd, uint32_t size)
{
    struct drm_tegra_bo *bo;
    uint32_t fd;
    int ret;

    ret = drm_tegra_bo_new(&bo, drm, 0, size);

    if (ret < 0) {
        return NULL;
    }

    ret = drm_tegra_bo_map(bo, map);

    if (ret < 0) {
        drm_tegra_bo_unref(bo);
        return NULL;
    }

    ret = drm_tegra_bo_to_dmabuf(bo, &fd);

    if (ret < 0) {
        drm_tegra_bo_unref(bo);
        return NULL;
    }

    *dmabuf_fd = fd;

    return bo;
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
    int dmabuf_fd;

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
        return VDP_STATUS_INVALID_DECODER_PROFILE;
    }

    pthread_mutex_lock(&global_lock);

    for (i = 0; i < MAX_DECODERS_NB; i++) {
        dec = get_decoder(i);

        if (dec == NULL) {
            dec = calloc(1, sizeof(tegra_decoder));
            set_decoder(i, dec);
            break;
        }
    }

    pthread_mutex_unlock(&global_lock);

    if (i == MAX_SURFACES_NB || dec == NULL) {
        return VDP_STATUS_RESOURCES;
    }

    dec->bitstream_bo = alloc_data(dev->drm, &dec->bitstream_data,
                                   &dmabuf_fd, MAX_BITSTREAM_SIZE);
    if (dec->bitstream_bo == NULL) {
        vdp_decoder_destroy(i);
        return VDP_STATUS_RESOURCES;
    }

    dec->dev = dev;
    dec->width = ALIGN(width, 16);
    dec->height = ALIGN(height, 16);
    dec->bitstream_data_fd = dmabuf_fd;
    dec->is_baseline_profile = is_baseline_profile;

    *decoder = i;

    return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_destroy(VdpDecoder decoder)
{
    tegra_decoder *dec = get_decoder(decoder);

    if (dec == NULL) {
        return VDP_INVALID_HANDLE;
    }

    set_decoder(decoder, NULL);

    close(dec->bitstream_data_fd);
    drm_tegra_bo_unref(dec->bitstream_bo);
    free(dec);

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
    tegra_surface *surf = get_surface(target);
    VdpStatus ret;

    if (dec == NULL || surf == NULL) {
        return VDP_STATUS_INVALID_HANDLE;
    }

    ret = copy_bitstream_to_dmabuf(dec, bitstream_buffer_count, bufs);

    if (ret != VDP_STATUS_OK) {
        return ret;
    }

    ret = tegra_decode_h264(dec, surf, picture_info);

    if (ret != VDP_STATUS_OK) {
        return ret;
    }

    surf->flags |= SURFACE_YUV_UNCONVERTED;
    surf->flags |= SURFACE_DATA_NEEDS_SYNC;

    return VDP_STATUS_OK;
}
