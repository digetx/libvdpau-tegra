/*
 * Copyright (c) 2016-2017 Dmitry Osipenko <digetx@gmail.com>
 * Copyright (C) 2012-2013 NVIDIA Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS\n", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Arto Merilainen <amerilainen@nvidia.com>
 */

#include "vdpau_tegra.h"

int tegra_stream_create(struct tegra_stream *stream, tegra_device *tegra)
{
    int err;

    stream->status = TEGRADRM_STREAM_FREE;
    stream->drm_fd = tegra->drm_fd;
    stream->drm = tegra->drm;

    err = drm_tegra_job_new_v2(&stream->job, tegra->drm, 16,
                               0x10000 /* 64K is enough for everything! */);
    if (err != 0) {
        ErrorMsg("drm_tegra_job_new() failed %d\n", err);
        return err;
    }

    return 0;
}

/*
 * tegra_stream_destroy(stream)
 *
 * Destroy the given stream object. All resrouces are released.
 */

void tegra_stream_destroy(struct tegra_stream *stream)
{
    if (!stream)
        return;

    tegra_stream_wait_fence(stream->last_fence);
    tegra_stream_put_fence(stream->last_fence);
    drm_tegra_job_free_v2(stream->job);
}

int tegra_stream_cleanup(struct tegra_stream *stream)
{
    if (!stream)
        return -1;

    drm_tegra_job_reset_v2(stream->job);
    stream->status = TEGRADRM_STREAM_FREE;

    return 0;
}

int tegra_stream_flush(struct tegra_stream *stream, bool gr2d)
{
    struct tegra_fence *f;
    int result = 0;

    if (!stream)
        return -1;

    tegra_stream_wait_fence(stream->last_fence);
    tegra_stream_put_fence(stream->last_fence);
    stream->last_fence = NULL;

    /* Reflushing is fine */
    if (stream->status == TEGRADRM_STREAM_FREE)
        return 0;

    /* Return error if stream is constructed badly */
    if (stream->status != TEGRADRM_STREAM_READY) {
        result = -1;
        goto cleanup;
    }

    f = tegra_stream_create_fence(stream, gr2d);
    if (!f) {
        result = -1;
        goto cleanup;
    }

    result = drm_tegra_job_submit_v2(stream->job, f->syncobj_handle, ~0ull);
    if (result != 0) {
        ErrorMsg("drm_tegra_job_submit_v2() failed %d\n", result);
        result = -1;
        goto cleanup;
    }

    tegra_stream_wait_fence(f);
    tegra_stream_put_fence(f);

cleanup:
    tegra_stream_cleanup(stream);

    return result;
}

struct tegra_fence * tegra_stream_submit(struct tegra_stream *stream, bool gr2d)
{
    struct tegra_fence *f;
    int result;

    if (!stream)
        return NULL;

    f = stream->last_fence;

    /* Resubmitting is fine */
    if (stream->status == TEGRADRM_STREAM_FREE)
        return f;

    /* Return error if stream is constructed badly */
    if (stream->status != TEGRADRM_STREAM_READY) {
        result = -1;
        goto cleanup;
    }

    f = tegra_stream_create_fence(stream, gr2d);
    tegra_stream_put_fence(stream->last_fence);
    stream->last_fence = f;

    if (!f) {
        result = -1;
        goto cleanup;
    }

    result = drm_tegra_job_submit_v2(stream->job, f->syncobj_handle, ~0ull);
    if (result != 0) {
        ErrorMsg("drm_tegra_job_submit() failed %d\n", result);
        result = -1;
    }

cleanup:
    drm_tegra_job_reset_v2(stream->job);
    stream->status = TEGRADRM_STREAM_FREE;

    return f;
}

struct tegra_fence * tegra_stream_ref_fence(struct tegra_fence *f, void *opaque)
{
    if (f) {
        f->opaque = opaque;
        f->refcnt++;
    }

    return f;
}

struct tegra_fence * tegra_stream_get_last_fence(struct tegra_stream *stream)
{
    if (stream->last_fence)
        return tegra_stream_ref_fence(stream->last_fence,
                                      stream->last_fence->opaque);

    return NULL;
}

static int tegra_stream_create_syncobj(struct tegra_stream *stream,
                                       uint32_t *syncobj_handle)
{
    int err;

    err = drmSyncobjCreate(stream->drm_fd, 0, syncobj_handle);
    if (err < 0) {
        ErrorMsg("drmSyncobjCreate() failed %d\n", err);
        return err;
    }

    return 0;
}

struct tegra_fence * tegra_stream_create_fence(struct tegra_stream *stream,
                                               bool gr2d)
{
    struct tegra_fence *f = calloc(1, sizeof(*f));
    int err;

    if (f) {
        err = tegra_stream_create_syncobj(stream, &f->syncobj_handle);
        if (err) {
            free(f);
            return NULL;
        }

        f->drm_fd = stream->drm_fd;
        f->gr2d = gr2d;
    }

    return f;
}

static uint64_t gettime_ns(void)
{
    struct timespec current;
    clock_gettime(CLOCK_MONOTONIC, &current);
    return (uint64_t)current.tv_sec * 1000000000ull + current.tv_nsec;
}

bool tegra_stream_wait_fence(struct tegra_fence *f)
{
    int result;

    if (f) {
        result = drmSyncobjWait(f->drm_fd, &f->syncobj_handle, 1,
                                gettime_ns() + 1000000000,
                                DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
                                NULL);
        if (result) {
            ErrorMsg("drmSyncobjWait() failed %d (%s)\n",
                     result, strerror(result));
            return result;
        }

        return true;
    }

    return false;
}

void tegra_stream_put_fence(struct tegra_fence *f)
{
    if (f && --f->refcnt < 0) {
        drmSyncobjDestroy(f->drm_fd, f->syncobj_handle);
        free(f);
    }
}

/*
 * tegra_stream_begin(stream, num_words, fence, num_fences, num_syncpt_incrs,
 *          num_relocs, class_id)
 *
 * Start constructing a stream.
 *  - num_words refer to the maximum number of words the stream can contain.
 *  - fence is a pointer to a table that contains syncpoint preconditions
 *    before the stream execution can start.
 *  - num_fences indicate the number of elements in the fence table.
 *  - num_relocs indicate the number of memory references in the buffer.
 *  - class_id refers to the class_id that is selected in the beginning of a
 *    stream. If no class id is given, the default class id (=usually the
 *    client device's class) is selected.
 *
 * This function verifies that the current buffer has enough room for holding
 * the whole stream (this is computed using num_words and num_relocs). The
 * function blocks until the stream buffer is ready for use.
 */

int tegra_stream_begin(struct tegra_stream *stream)
{
    /* check stream and its state */
    if (!(stream && stream->status == TEGRADRM_STREAM_FREE)) {
        ErrorMsg("Stream status isn't FREE\n");
        return -1;
    }

    stream->class_id = 0;
    stream->status = TEGRADRM_STREAM_CONSTRUCT;
    stream->op_done_synced = false;

    return 0;
}

int tegra_stream_push_reloc(struct tegra_stream *stream,
                            struct drm_tegra_bo *bo,
                            unsigned offset)
{
    int ret;

    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        ErrorMsg("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    ret = drm_tegra_job_push_reloc_v2(stream->job, bo, offset,
                                      DRM_TEGRA_BO_TABLE_WRITE);
    if (ret != 0) {
        stream->status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
        ErrorMsg("drm_tegra_job_push_reloc_v2() failed %d\n", ret);
        return -1;
    }

    return 0;
}

/*
 * tegra_stream_push(stream, word)
 *
 * Push a single word to given stream.
 */

int tegra_stream_push(struct tegra_stream *stream, uint32_t word)
{
    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        ErrorMsg("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    *stream->job->ptr++ = word;
    stream->op_done_synced = false;

    return 0;
}

/*
 * tegra_stream_push_setclass(stream, class_id)
 *
 * Push "set class" opcode to the stream. Do nothing if the class is already
 * active
 */

int tegra_stream_push_setclass(struct tegra_stream *stream, unsigned class_id)
{
    int result;

    if (stream->class_id == class_id)
        return 0;

    result = tegra_stream_push(stream, HOST1X_OPCODE_SETCL(0, class_id, 0));

    if (result == 0)
        stream->class_id = class_id;

    return result;
}

/*
 * tegra_stream_end(stream)
 *
 * Mark end of stream. This function pushes last syncpoint increment for
 * marking end of stream.
 */

int tegra_stream_end(struct tegra_stream *stream)
{
    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        ErrorMsg("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    if (stream->op_done_synced)
        goto ready;

    tegra_stream_push(stream,
                      HOST1X_OPCODE_IMM(0, DRM_TEGRA_SYNCPT_COND_OP_DONE << 8));

ready:
    stream->status = TEGRADRM_STREAM_READY;
    stream->op_done_synced = false;

    return 0;
}

int tegra_stream_prep(struct tegra_stream *stream, uint32_t words)
{
    int ret;

    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        ErrorMsg("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    if (stream->job->ptr + words >
        stream->job->start + stream->job->num_words) {
        if (words < 1024)
            words = 1024;

        ret = drm_tegra_job_resize_v2(stream->job,
                                      stream->job->num_words + words,
                                      stream->job->num_bos,
                                      true);
        if (ret != 0) {
            stream->status = TEGRADRM_STREAM_CONSTRUCTION_FAILED;
            ErrorMsg("drm_tegra_job_resize_words_v2() failed %d\n", ret);
            return -1;
        }
    }

    return 0;
}

int tegra_stream_sync(struct tegra_stream *stream,
                      enum drm_tegra_syncpt_cond cond,
                      bool keep_class)
{
    struct drm_tegra_cmdstream_wait_syncpt wait;

    if (!(stream && stream->status == TEGRADRM_STREAM_CONSTRUCT)) {
        ErrorMsg("Stream status isn't CONSTRUCT\n");
        return -1;
    }

    wait.threshold = DRM_TEGRA_WAIT_FOR_LAST_SYNCPT_INCR;

    tegra_stream_prep(stream, 4);
    tegra_stream_push(stream, HOST1X_OPCODE_IMM(0, cond << 8));

    /* switch to host1x class to await the sync point increment */
    tegra_stream_push(stream, HOST1X_OPCODE_SETCL(8, HOST1X_CLASS_HOST1X, 1));
    tegra_stream_push(stream, wait.u_data);

    /* return to the original class if desired */
    if (keep_class)
        tegra_stream_push(stream, HOST1X_OPCODE_SETCL(0, stream->class_id , 0));

    if (cond == DRM_TEGRA_SYNCPT_COND_OP_DONE)
        stream->op_done_synced = true;

    return 0;
}

int tegra_stream_pushf(struct tegra_stream *stream, float f)
{
    union {
        uint32_t u;
        float f;
    } value;

    value.f = f;

    return tegra_stream_push(stream, value.u);
}
