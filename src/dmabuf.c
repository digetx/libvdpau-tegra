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

int alloc_dmabuf(int dev_fd, void **dmabuf_virt, size_t size)
{
    int dmabuf_fd = ioctl(dev_fd, TEGRA_VDE_IOCTL_ALLOC_DMA_BUF, size);

    assert(dmabuf_fd >= 0);

    if (dmabuf_fd < 0) {
        perror("DMABUF allocation from VDE failed");
        return dmabuf_fd;
    }

    if (dmabuf_virt == NULL) {
        return dmabuf_fd;
    }

    *dmabuf_virt = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        dmabuf_fd, 0);

    assert(*dmabuf_virt != MAP_FAILED);

    if (dmabuf_virt == MAP_FAILED) {
        *dmabuf_virt = NULL;
    }

    return dmabuf_fd;
}

void free_dmabuf(int dmabuf_fd, void *dmabuf_virt, size_t size)
{
    int ret;

    if (dmabuf_virt != NULL) {
        ret = munmap(dmabuf_virt, size);
        assert(ret == 0);
    }

    if (dmabuf_fd >= 0) {
        ret = close(dmabuf_fd);
        assert(ret == 0);
    }
}

int sync_dmabuf_write_start(int dmabuf_fd)
{
    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_START,
    };

    return ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

int sync_dmabuf_write_end(int dmabuf_fd)
{
    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_END,
    };

    return ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

int sync_dmabuf_read_start(int dmabuf_fd)
{
    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_READ | DMA_BUF_SYNC_START,
    };

    return ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}

int sync_dmabuf_read_end(int dmabuf_fd)
{
    struct dma_buf_sync sync = {
        .flags = DMA_BUF_SYNC_READ | DMA_BUF_SYNC_END,
    };

    return ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
}
