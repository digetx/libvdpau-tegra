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
