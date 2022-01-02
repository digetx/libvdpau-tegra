/*
 * NVIDIA TEGRA 2 VDPAU backend driver
 *
 * Copyright (c) 2022 Dmitry Osipenko <digetx@gmail.com>
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

#define CACHE_EXPIRE_NSEC    (30 * NSEC_PER_SEC)

static struct list_head tegra_cache_list = {
    .prev = &tegra_cache_list,
    .next = &tegra_cache_list,
};

static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

void tegra_surface_cache_init(tegra_surface_cache *cache)
{
    LIST_INITHEAD(&cache->list);

    pthread_mutex_lock(&cache_lock);
    LIST_ADD(&cache->cache_list_entry, &tegra_cache_list);
    pthread_mutex_unlock(&cache_lock);
}

static void
tegra_surface_cache_remove_surface_locked(tegra_surface *surf)
{
    DebugMsg("surface %u %p cache %p\n",
             surf->surface_id, surf, surf->cache_entry.cache);

    LIST_DEL(&surf->cache_entry.entry);
    surf->cache_entry.cache = NULL;
    unref_surface(surf);
}

static void tegra_surface_cache_clear_locked(tegra_surface_cache *cache)
{
    tegra_surface *surf, *tmp;

    DebugMsg("cache %p\n", cache);

    LIST_FOR_EACH_ENTRY_SAFE(surf, tmp, &cache->list, cache_entry.entry)
        tegra_surface_cache_remove_surface_locked(surf);
}

void tegra_surface_cache_release(tegra_surface_cache *cache)
{
    pthread_mutex_lock(&cache_lock);

    tegra_surface_cache_clear_locked(cache);
    LIST_DEL(&cache->cache_list_entry);

    pthread_mutex_unlock(&cache_lock);
}

void tegra_surface_drop_caches(void)
{
    tegra_surface_cache *cache;

    DebugMsg("\n");

    pthread_mutex_lock(&cache_lock);

    LIST_FOR_EACH_ENTRY(cache, &tegra_cache_list, cache_list_entry)
        tegra_surface_cache_clear_locked(cache);

    pthread_mutex_unlock(&cache_lock);
}

static void tegra_surface_cache_cleanup_locked(tegra_surface_cache *cache,
                                               VdpTime time)
{
    tegra_surface *surf, *tmp;

    LIST_FOR_EACH_ENTRY_SAFE(surf, tmp, &cache->list, cache_entry.entry) {
        if (!surf->destroyed)
            continue;

        if (time - surf->cache_entry.last_use >= CACHE_EXPIRE_NSEC) {
            DebugMsg("evicted surface %u %p cache %p\n",
                     surf->surface_id, surf, cache);

            tegra_surface_cache_remove_surface_locked(surf);
        }
    }
}

void tegra_surface_cache_surface_update_last_use(tegra_surface *surf)
{
    DebugMsg("surface %u %p cache %p\n",
             surf->surface_id, surf, surf->cache_entry.cache);

    pthread_mutex_lock(&surf->lock);
    surf->cache_entry.last_use = get_time();
    pthread_mutex_unlock(&surf->lock);
}

void tegra_surface_cache_add_surface(tegra_surface_cache *cache,
                                     tegra_surface *surf)
{
    DebugMsg("surface %u %p cache %p\n",
             surf->surface_id, surf, surf->cache_entry.cache);

    pthread_mutex_lock(&cache_lock);

    if (!surf->cache_entry.cache) {
        LIST_ADDTAIL(&surf->cache_entry.entry, &cache->list);
        surf->cache_entry.cache = cache;
        ref_surface(surf);

        DebugMsg("surface %u %p added to cache %p\n",
                 surf->surface_id, surf, cache);
    } else {
        DebugMsg("surface %u %p not added to cache %p\n",
                 surf->surface_id, surf, cache);
    }

    tegra_surface_cache_surface_update_last_use(surf);
    tegra_surface_cache_cleanup_locked(cache, surf->cache_entry.last_use);

    pthread_mutex_unlock(&cache_lock);
}

void tegra_surface_cache_surface_self_remove(tegra_surface *surf)
{
    DebugMsg("surface %u %p cache %p\n",
             surf->surface_id, surf, surf->cache_entry.cache);

    pthread_mutex_lock(&cache_lock);

    if (surf->cache_entry.cache)
        tegra_surface_cache_remove_surface_locked(surf);

    pthread_mutex_unlock(&cache_lock);
}

tegra_surface *
tegra_surface_cache_take_surface(tegra_device *dev,
                                 uint32_t width, uint32_t height,
                                 VdpRGBAFormat rgba_format,
                                 int output, int video)
{
    tegra_surface_cache *cache;
    tegra_surface *surf, *tmp;

    pthread_mutex_lock(&cache_lock);

    DebugMsg("want dev %p width %d height %d rgba_format %d output %d video %d\n",
             dev, width, height, rgba_format, output, video);

    if (tegra_vdpau_debug) {
        LIST_FOR_EACH_ENTRY(cache, &tegra_cache_list, cache_list_entry) {
            LIST_FOR_EACH_ENTRY_SAFE(surf, tmp, &cache->list, cache_entry.entry) {
                pthread_mutex_lock(&surf->lock);

                DebugMsg("surface %u %p cache %p dev %p width %d height %d rgba_format %d destroyed %d detached %d shared %d output %d video %d time %llums\n",
                         surf->surface_id, surf, cache, surf->dev,
                         surf->width, surf->height, surf->rgba_format,
                         surf->destroyed, surf->detached, !!surf->shared,
                         !!(surf->flags & SURFACE_OUTPUT),
                         !!(surf->flags & SURFACE_VIDEO),
                         (get_time() - surf->cache_entry.last_use) / 1000000);

                pthread_mutex_unlock(&surf->lock);
            }
        }
    }

    LIST_FOR_EACH_ENTRY(cache, &tegra_cache_list, cache_list_entry) {
        LIST_FOR_EACH_ENTRY_SAFE(surf, tmp, &cache->list, cache_entry.entry) {
            pthread_mutex_lock(&surf->lock);

            if (surf->destroyed &&
                surf->dev == dev &&
                surf->width == width &&
                surf->height == height &&
                surf->rgba_format == rgba_format &&
                !!output == !!(surf->flags & SURFACE_OUTPUT) &&
                !!video == !!(surf->flags & SURFACE_VIDEO))
            {
                    ref_surface(surf);
                    tegra_surface_cache_remove_surface_locked(surf);

                    DebugMsg("surface %u %p cache %p\n",
                             surf->surface_id, surf, cache);

                    pthread_mutex_unlock(&surf->lock);
                    pthread_mutex_unlock(&cache_lock);

                    return surf;
            }

            pthread_mutex_unlock(&surf->lock);
        }
    }

    pthread_mutex_unlock(&cache_lock);

    return NULL;
}
