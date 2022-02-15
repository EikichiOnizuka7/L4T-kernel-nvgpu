/*
 * Copyright (c) 2017-2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>

#include <nvgpu/comptags.h>
#include <nvgpu/enabled.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/linux/vm.h>
#include <nvgpu/bug.h>
#include <nvgpu/fence.h>
#include <nvgpu/vm.h>

#include "platform_gk20a.h"
#include "dmabuf_priv.h"
#include "os_linux.h"
#include "dmabuf_vidmem.h"

void gk20a_mm_delete_priv(struct gk20a_dmabuf_priv *priv);

enum nvgpu_aperture gk20a_dmabuf_aperture(struct gk20a *g,
					  struct dma_buf *dmabuf)
{
#ifdef CONFIG_NVGPU_DGPU
	struct gk20a *buf_owner = nvgpu_vidmem_buf_owner(dmabuf);
	bool unified_memory = nvgpu_is_enabled(g, NVGPU_MM_UNIFIED_MEMORY);

	if (buf_owner == NULL) {
		/* Not nvgpu-allocated, assume system memory */
		return APERTURE_SYSMEM;
	} else if ((buf_owner == g) && unified_memory) {
		/* Looks like our video memory, but this gpu doesn't support
		 * it. Warn about a bug and bail out */
		nvgpu_do_assert_print(g,
			"dmabuf is our vidmem but we don't have local vidmem");
		return APERTURE_INVALID;
	} else if (buf_owner != g) {
		/* Someone else's vidmem */
		return APERTURE_INVALID;
	} else {
		/* Yay, buf_owner == g */
		return APERTURE_VIDMEM;
	}
#else
	return APERTURE_SYSMEM;
#endif
}

static struct gk20a_dmabuf_priv *dma_buf_ops_to_gk20a_priv(
		struct dma_buf_ops *ops)
{
	struct gk20a_dmabuf_priv *priv = container_of(ops,
			struct gk20a_dmabuf_priv, local_ops);

	return priv;
}

static void nvgpu_dma_buf_release(struct dma_buf *dmabuf)
{
	struct gk20a_dmabuf_priv *priv = NULL;
	struct nvgpu_os_linux *l = NULL;

	priv = dma_buf_ops_to_gk20a_priv((struct dma_buf_ops *)dmabuf->ops);
	if (priv != NULL) {
		l = nvgpu_os_linux_from_gk20a(priv->g);
	} else {
		BUG();
		return;
	}

	/* remove this entry from the global tracking list */
	nvgpu_mutex_acquire(&l->dmabuf_priv_list_lock);
	gk20a_mm_delete_priv(priv);
	nvgpu_mutex_release(&l->dmabuf_priv_list_lock);

	dmabuf->ops->release(dmabuf);
}

static int gk20a_dma_buf_set_drvdata(struct dma_buf *dmabuf, struct device *device,
			struct gk20a_dmabuf_priv *priv)
{
	nvgpu_mutex_acquire(&priv->lock);

	priv->dmabuf = dmabuf;

	mutex_lock(&dmabuf->lock);
	priv->previous_ops = dmabuf->ops;
	/*
	 * Make a copy of the original ops struct and then update the
	 * release pointer
	 */
	priv->local_ops = *(dmabuf->ops);
	priv->local_ops.release = nvgpu_dma_buf_release;
	dmabuf->ops = &priv->local_ops;
	mutex_unlock(&dmabuf->lock);

	nvgpu_mutex_release(&priv->lock);

	return 0;
}

static struct gk20a_dmabuf_priv *gk20a_dmabuf_priv_from_list(
						struct nvgpu_list_node *node)
{
	return container_of(node, struct gk20a_dmabuf_priv, list);
}

struct gk20a_dmabuf_priv *gk20a_dma_buf_get_drvdata(
		struct dma_buf *dmabuf, struct device *device)
{
	struct gk20a_dmabuf_priv *priv = NULL;

	mutex_lock(&dmabuf->lock);
	if (dmabuf->ops->release == nvgpu_dma_buf_release) {
		priv = dma_buf_ops_to_gk20a_priv((struct dma_buf_ops *)dmabuf->ops);
	}
	mutex_unlock(&dmabuf->lock);

	return priv;
}

struct sg_table *nvgpu_mm_pin(struct device *dev,
			struct dma_buf *dmabuf, struct dma_buf_attachment **attachment)
{
	struct gk20a *g = get_gk20a(dev);
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *sgt = NULL;

	attach = dma_buf_attach(dmabuf, dev);
	if (IS_ERR(attach)) {
		nvgpu_err(g, "Failed to attach dma_buf (err = %ld)!",
				PTR_ERR(attach));
		return ERR_CAST(attach);
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		dma_buf_detach(dmabuf, attach);
		nvgpu_err(g, "Failed to map attachment (err = %ld)!",
				PTR_ERR(sgt));
		return ERR_CAST(sgt);
	}

	*attachment = attach;

	return sgt;
}

void nvgpu_mm_unpin(struct device *dev,
			struct dma_buf *dmabuf,
			struct dma_buf_attachment *attachment,
			struct sg_table *sgt)
{
	dma_buf_unmap_attachment(attachment, sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(dmabuf, attachment);
}

/* This function must be called after acquiring the global level
 * dmabuf_priv_list_lock.
 */
void gk20a_mm_delete_priv(struct gk20a_dmabuf_priv *priv)
{
	struct gk20a_buffer_state *s, *s_tmp;
	struct gk20a *g;
	struct dma_buf *dmabuf;

	if (!priv)
		return;

	g = priv->g;

	dmabuf = priv->dmabuf;

	if (priv->comptags.allocated && priv->comptags.lines) {
		WARN_ON(!priv->comptag_allocator);
		gk20a_comptaglines_free(priv->comptag_allocator,
				priv->comptags.offset,
				priv->comptags.lines);
	}

	/* Free buffer states */
	nvgpu_list_for_each_entry_safe(s, s_tmp, &priv->states,
				gk20a_buffer_state, list) {
		nvgpu_user_fence_release(&s->fence);
		nvgpu_list_del(&s->list);
		nvgpu_kfree(g, s);
	}

	/* The original pointer to dma_buf_ops is always put back here*/
	mutex_lock(&dmabuf->lock);
	dmabuf->ops = priv->previous_ops;
	mutex_unlock(&dmabuf->lock);

	/* Remove this entry from the global tracking list */
	nvgpu_list_del(&priv->list);

	nvgpu_kfree(g, priv);
}

void gk20a_dma_buf_priv_list_clear(struct nvgpu_os_linux *l)
{
	struct gk20a_dmabuf_priv *priv, *priv_next;

	nvgpu_mutex_acquire(&l->dmabuf_priv_list_lock);
	nvgpu_list_for_each_entry_safe(priv, priv_next, &l->dmabuf_priv_list,
				gk20a_dmabuf_priv, list) {
		gk20a_mm_delete_priv(priv);
	}
	nvgpu_mutex_release(&l->dmabuf_priv_list_lock);
}

int gk20a_dmabuf_alloc_drvdata(struct dma_buf *dmabuf, struct device *dev)
{
	struct gk20a *g = gk20a_get_platform(dev)->g;
	struct gk20a_dmabuf_priv *priv;
	struct nvgpu_os_linux *l = nvgpu_os_linux_from_gk20a(g);

	priv = gk20a_dma_buf_get_drvdata(dmabuf, dev);

	if (likely(priv))
		return 0;

	nvgpu_mutex_acquire(&g->mm.priv_lock);
	priv = gk20a_dma_buf_get_drvdata(dmabuf, dev);
	if (priv)
		goto priv_exist_or_err;

	priv = nvgpu_kzalloc(g, sizeof(*priv));
	if (!priv) {
		priv = ERR_PTR(-ENOMEM);
		goto priv_exist_or_err;
	}

	nvgpu_mutex_init(&priv->lock);
	nvgpu_init_list_node(&priv->states);
	priv->g = g;
	gk20a_dma_buf_set_drvdata(dmabuf, dev, priv);

	nvgpu_init_list_node(&priv->list);

	/* Append this priv to the global tracker */
	nvgpu_mutex_acquire(&l->dmabuf_priv_list_lock);
	nvgpu_list_add_tail(&l->dmabuf_priv_list, &priv->list);
	nvgpu_mutex_release(&l->dmabuf_priv_list_lock);

priv_exist_or_err:
	nvgpu_mutex_release(&g->mm.priv_lock);
	if (IS_ERR(priv))
		return -ENOMEM;

	return 0;
}

int gk20a_dmabuf_get_state(struct dma_buf *dmabuf, struct gk20a *g,
			   u64 offset, struct gk20a_buffer_state **state)
{
	int err = 0;
	struct gk20a_dmabuf_priv *priv;
	struct gk20a_buffer_state *s;
	struct device *dev = dev_from_gk20a(g);

	if (offset >= (u64)dmabuf->size) {
		nvgpu_do_assert();
		return -EINVAL;
	}

	err = gk20a_dmabuf_alloc_drvdata(dmabuf, dev);
	if (err)
		return err;

	priv = gk20a_dma_buf_get_drvdata(dmabuf, dev);
	if (!priv) {
		nvgpu_do_assert();
		return -ENOSYS;
	}

	nvgpu_mutex_acquire(&priv->lock);

	nvgpu_list_for_each_entry(s, &priv->states, gk20a_buffer_state, list)
		if (s->offset == offset)
			goto out;

	/* State not found, create state. */
	s = nvgpu_kzalloc(g, sizeof(*s));
	if (!s) {
		err = -ENOMEM;
		goto out;
	}

	s->offset = offset;
	nvgpu_init_list_node(&s->list);
	nvgpu_mutex_init(&s->lock);
	nvgpu_list_add_tail(&s->list, &priv->states);

out:
	nvgpu_mutex_release(&priv->lock);
	if (!err)
		*state = s;
	return err;
}
