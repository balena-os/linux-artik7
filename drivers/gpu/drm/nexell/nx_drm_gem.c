/*
 * Copyright (C) 2016  Nexell Co., Ltd.
 * Author: junghyun, kim <jhkim@nexell.co.kr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <drm/drmP.h>
#include <drm/drm_vma_manager.h>

#include <linux/shmem_fs.h>
#include <drm/nexell_drm.h>

#include "nx_drm_gem.h"

struct sg_table *nx_drm_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct drm_gem_cma_object *cma_obj = to_drm_gem_cma_obj(obj);
	struct sg_table *sgt;
	dma_addr_t paddr = cma_obj->paddr;
	size_t size = obj->size;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (unlikely(ret))
		goto out;

	sg_set_page(sgt->sgl, phys_to_page(paddr), PAGE_ALIGN(size), 0);

	return sgt;

out:
	kfree(sgt);
	return NULL;
}

int nx_drm_gem_get_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct nx_drm_gem_info *args = data;
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;

	DRM_DEBUG_DRIVER("enter\n");

	mutex_lock(&dev->struct_mutex);

	gem_obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (!gem_obj) {
		DRM_ERROR("fail : lookup gem object.\n");
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	cma_obj = to_drm_gem_cma_obj(gem_obj);
	args->size = gem_obj->size;

	drm_gem_object_unreference(gem_obj);
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG_DRIVER("get dma p:0x%x, v:%p\n",
			(unsigned int)cma_obj->paddr, cma_obj->vaddr);

	return 0;
}

int nx_drm_gem_create_ioctl(struct drm_device *drm, void *data,
			struct drm_file *file_priv)
{
	struct nx_drm_gem_create *args = data;
	struct drm_gem_cma_object *cma_obj;
	struct drm_gem_object *gem_obj;
	int ret;

	DRM_DEBUG_DRIVER("enter (size: %lld)\n", args->size);

	cma_obj = drm_gem_cma_create(drm, args->size);
	if (IS_ERR(cma_obj))
		return PTR_ERR(cma_obj);

	gem_obj = &cma_obj->base;

	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, gem_obj, &args->handle);
	if (ret)
		goto err_handle_create;

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_unreference_unlocked(gem_obj);

	DRM_DEBUG_DRIVER("alloc dma p:0x%x, v:0x%lx s:%d\n",
		(unsigned int)cma_obj->paddr,
		(unsigned long)cma_obj->vaddr, (int)gem_obj->size);

	return 0;

err_handle_create:
	drm_gem_cma_free_object(gem_obj);

	return PTR_ERR(ERR_PTR(ret));
}

