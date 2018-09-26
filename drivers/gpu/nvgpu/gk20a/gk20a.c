/*
 * GK20A Graphics
 *
 * Copyright (c) 2011-2018, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <nvgpu/nvgpu_common.h>
#include <nvgpu/kmem.h>
#include <nvgpu/allocator.h>
#include <nvgpu/timers.h>
#include <nvgpu/soc.h>
#include <nvgpu/enabled.h>
#include <nvgpu/pmu.h>
#include <nvgpu/gmmu.h>
#include <nvgpu/ltc.h>
#include <nvgpu/vidmem.h>
#include <nvgpu/mm.h>
#include <nvgpu/ctxsw_trace.h>
#include <nvgpu/soc.h>
#include <nvgpu/clk_arb.h>
#include <nvgpu/therm.h>
#include <nvgpu/mc.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel_sync.h>

#include <trace/events/gk20a.h>

#include "gk20a/ce2_gk20a.h"

#include "pstate/pstate.h"

void __nvgpu_check_gpu_state(struct gk20a *g)
{
	u32 boot_0 = 0xffffffff;

	boot_0 = nvgpu_mc_boot_0(g, NULL, NULL, NULL);
	if (boot_0 == 0xffffffff) {
		nvgpu_err(g, "GPU has disappeared from bus!!");
		nvgpu_err(g, "Rebooting system!!");
		nvgpu_kernel_restart(NULL);
	}
}

void __gk20a_warn_on_no_regs(void)
{
	WARN_ONCE(1, "Attempted access to GPU regs after unmapping!");
}

static void gk20a_mask_interrupts(struct gk20a *g)
{
	if (g->ops.mc.intr_mask != NULL) {
		g->ops.mc.intr_mask(g);
	}

	if (g->ops.mc.log_pending_intrs != NULL) {
		g->ops.mc.log_pending_intrs(g);
	}
}

int gk20a_prepare_poweroff(struct gk20a *g)
{
	int ret = 0;

	nvgpu_log_fn(g, " ");

	if (g->ops.fifo.channel_suspend != NULL) {
		ret = g->ops.fifo.channel_suspend(g);
		if (ret != 0) {
			return ret;
		}
	}

	/* disable elpg before gr or fifo suspend */
	if (g->ops.pmu.is_pmu_supported(g)) {
		ret |= nvgpu_pmu_destroy(g);
	}

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_SEC2_RTOS)) {
		ret |= nvgpu_sec2_destroy(g);
	}

	ret |= gk20a_gr_suspend(g);
	ret |= nvgpu_mm_suspend(g);
	ret |= gk20a_fifo_suspend(g);

	gk20a_ce_suspend(g);

	/* Disable GPCPLL */
	if (g->ops.clk.suspend_clk_support != NULL) {
		ret |= g->ops.clk.suspend_clk_support(g);
	}

	if (nvgpu_is_enabled(g, NVGPU_PMU_PSTATE)) {
		gk20a_deinit_pstate_support(g);
	}

	gk20a_mask_interrupts(g);

	g->power_on = false;

	return ret;
}

int gk20a_finalize_poweron(struct gk20a *g)
{
	int err = 0;
#if defined(CONFIG_TEGRA_GK20A_NVHOST)
	u32 nr_pages;
#endif

	nvgpu_log_fn(g, " ");

	if (g->power_on) {
		return 0;
	}

	g->power_on = true;

	/*
	 * Before probing the GPU make sure the GPU's state is cleared. This is
	 * relevant for rebind operations.
	 */
	if ((g->ops.xve.reset_gpu != NULL) && !g->gpu_reset_done) {
		g->ops.xve.reset_gpu(g);
		g->gpu_reset_done = true;
	}

	/*
	 * Do this early so any early VMs that get made are capable of mapping
	 * buffers.
	 */
	err = nvgpu_pd_cache_init(g);
	if (err != 0) {
		return err;
	}

	/* init interface layer support for PMU falcon */
	err = nvgpu_flcn_sw_init(g, FALCON_ID_PMU);
	if (err != 0) {
		nvgpu_err(g, "failed to sw init FALCON_ID_PMU");
		goto done;
	}
	err = nvgpu_flcn_sw_init(g, FALCON_ID_SEC2);
	if (err != 0) {
		nvgpu_err(g, "failed to sw init FALCON_ID_SEC2");
		goto done;
	}
	err = nvgpu_flcn_sw_init(g, FALCON_ID_NVDEC);
	if (err != 0) {
		nvgpu_err(g, "failed to sw init FALCON_ID_NVDEC");
		goto done;
	}
	err = nvgpu_flcn_sw_init(g, FALCON_ID_GSPLITE);
	if (err != 0) {
		nvgpu_err(g, "failed to sw init FALCON_ID_GSPLITE");
		goto done;
	}
	err = nvgpu_flcn_sw_init(g, FALCON_ID_FECS);
	if (err != 0) {
		nvgpu_err(g, "failed to sw init FALCON_ID_FECS");
		goto done;
	}

	if (g->ops.acr.acr_sw_init != NULL &&
		nvgpu_is_enabled(g, NVGPU_SEC_PRIVSECURITY)) {
		g->ops.acr.acr_sw_init(g, &g->acr);
	}

	if (g->ops.bios.init != NULL) {
		err = g->ops.bios.init(g);
	}
	if (err != 0) {
		goto done;
	}

	g->ops.bus.init_hw(g);

	if (g->ops.clk.disable_slowboot != NULL) {
		g->ops.clk.disable_slowboot(g);
	}

	g->ops.priv_ring.enable_priv_ring(g);

	/* TBD: move this after graphics init in which blcg/slcg is enabled.
	   This function removes SlowdownOnBoot which applies 32x divider
	   on gpcpll bypass path. The purpose of slowdown is to save power
	   during boot but it also significantly slows down gk20a init on
	   simulation and emulation. We should remove SOB after graphics power
	   saving features (blcg/slcg) are enabled. For now, do it here. */
	if (g->ops.clk.init_clk_support != NULL) {
		err = g->ops.clk.init_clk_support(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init gk20a clk");
			goto done;
		}
	}

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_NVLINK)) {
		err = g->ops.nvlink.init(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init nvlink");
			goto done;
		}
	}

	if (g->ops.fb.init_fbpa != NULL) {
		err = g->ops.fb.init_fbpa(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init fbpa");
			goto done;
		}
	}

	if (g->ops.fb.mem_unlock != NULL) {
		err = g->ops.fb.mem_unlock(g);
		if (err != 0) {
			nvgpu_err(g, "failed to unlock memory");
			goto done;
		}
	}

	err = g->ops.fifo.reset_enable_hw(g);

	if (err != 0) {
		nvgpu_err(g, "failed to reset gk20a fifo");
		goto done;
	}

	err = nvgpu_init_ltc_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init ltc");
		goto done;
	}

	err = nvgpu_init_mm_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a mm");
		goto done;
	}

	err = gk20a_init_fifo_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a fifo");
		goto done;
	}

	if (g->ops.therm.elcg_init_idle_filters != NULL) {
		g->ops.therm.elcg_init_idle_filters(g);
	}

	g->ops.mc.intr_enable(g);

	/*
	 *  Overwrite can_tpc_powergate to false if the chip is ES fused and
	 *  already optimized with some TPCs already floorswept
	 *  via fuse. We will not support TPC-PG in those cases.
	 */

	if (g->ops.fuse.fuse_status_opt_tpc_gpc(g, 0) != 0x0) {
		g->can_tpc_powergate = false;
		g->tpc_pg_mask = 0x0;
	}

	nvgpu_mutex_acquire(&g->tpc_pg_lock);

	if (g->can_tpc_powergate) {
		if (g->ops.gr.powergate_tpc != NULL)
			g->ops.gr.powergate_tpc(g);
	}

	err = gk20a_enable_gr_hw(g);
	if (err != 0) {
		nvgpu_err(g, "failed to enable gr");
		nvgpu_mutex_release(&g->tpc_pg_lock);
		goto done;
	}

	if (g->ops.pmu.is_pmu_supported(g)) {
		if (g->ops.pmu.prepare_ucode != NULL) {
			err = g->ops.pmu.prepare_ucode(g);
		}
		if (err != 0) {
			nvgpu_err(g, "failed to init pmu ucode");
			nvgpu_mutex_release(&g->tpc_pg_lock);
			goto done;
		}
	}

	if (nvgpu_is_enabled(g, NVGPU_PMU_PSTATE)) {
		err = gk20a_init_pstate_support(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init pstates");
			nvgpu_mutex_release(&g->tpc_pg_lock);
			goto done;
		}
	}

	if (g->acr.bootstrap_hs_acr != NULL &&
		nvgpu_is_enabled(g, NVGPU_SEC_PRIVSECURITY)) {
		err = g->acr.bootstrap_hs_acr(g, &g->acr, &g->acr.acr);
		if (err != 0) {
			nvgpu_err(g, "ACR bootstrap failed");
			goto done;
		}
	}

	if (nvgpu_is_enabled(g, NVGPU_SUPPORT_SEC2_RTOS)) {
		err = nvgpu_init_sec2_support(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init sec2");
				goto done;
		}
	}

	if (g->ops.pmu.is_pmu_supported(g)) {
		err = nvgpu_init_pmu_support(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init gk20a pmu");
			nvgpu_mutex_release(&g->tpc_pg_lock);
			goto done;
		}
	}

	err = gk20a_init_gr_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a gr");
		nvgpu_mutex_release(&g->tpc_pg_lock);
		goto done;
	}

	nvgpu_mutex_release(&g->tpc_pg_lock);

	if (nvgpu_is_enabled(g, NVGPU_PMU_PSTATE)) {
		err = gk20a_init_pstate_pmu_support(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init pstates");
			goto done;
		}
	}

	if ((g->ops.pmu_ver.clk.clk_set_boot_clk != NULL) &&
			nvgpu_is_enabled(g, NVGPU_PMU_PSTATE)) {
		g->ops.pmu_ver.clk.clk_set_boot_clk(g);
	} else {
		err = nvgpu_clk_arb_init_arbiter(g);
		if (err != 0) {
			nvgpu_err(g, "failed to init clk arb");
			goto done;
		}
	}

	err = nvgpu_init_therm_support(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a therm");
		goto done;
	}

	err = g->ops.chip_init_gpu_characteristics(g);
	if (err != 0) {
		nvgpu_err(g, "failed to init gk20a gpu characteristics");
		goto done;
	}

#ifdef CONFIG_GK20A_CTXSW_TRACE
	err = gk20a_ctxsw_trace_init(g);
	if (err != 0)
		nvgpu_warn(g, "could not initialize ctxsw tracing");
#endif

	/* Restore the debug setting */
	g->ops.fb.set_debug_mode(g, g->mmu_debug_ctrl);

	gk20a_init_ce_support(g);

	if (g->ops.xve.available_speeds != NULL) {
		u32 speed;

		if (!nvgpu_is_enabled(g, NVGPU_SUPPORT_ASPM) &&
				(g->ops.xve.disable_aspm != NULL)) {
			g->ops.xve.disable_aspm(g);
		}

		g->ops.xve.available_speeds(g, &speed);

		/* Set to max speed */
		speed = 1 << (fls(speed) - 1);
		err = g->ops.xve.set_speed(g, speed);
		if (err != 0) {
			nvgpu_err(g, "Failed to set PCIe bus speed!");
			goto done;
		}
	}

#if defined(CONFIG_TEGRA_GK20A_NVHOST)
	if (nvgpu_has_syncpoints(g) && g->syncpt_unit_size) {
		if (!nvgpu_mem_is_valid(&g->syncpt_mem)) {
			nr_pages = DIV_ROUND_UP(g->syncpt_unit_size, PAGE_SIZE);
			__nvgpu_mem_create_from_phys(g, &g->syncpt_mem,
					g->syncpt_unit_base, nr_pages);
		}
	}
#endif

	if (g->ops.fifo.channel_resume != NULL) {
		g->ops.fifo.channel_resume(g);
	}

done:
	if (err != 0) {
		g->power_on = false;
	}

	return err;
}

/*
 * Check if the device can go busy. Basically if the driver is currently
 * in the process of dying then do not let new places make the driver busy.
 */
int gk20a_can_busy(struct gk20a *g)
{
	if (nvgpu_is_enabled(g, NVGPU_DRIVER_IS_DYING)) {
		return 0;
	}
	return 1;
}

int gk20a_wait_for_idle(struct gk20a *g)
{
	int wait_length = 150; /* 3 second overall max wait. */
	int target_usage_count = 0;

	if (g == NULL) {
		return -ENODEV;
	}

	while ((nvgpu_atomic_read(&g->usage_count) != target_usage_count)
			&& (wait_length-- >= 0)) {
		nvgpu_msleep(20);
	}

	if (wait_length < 0) {
		nvgpu_warn(g, "Timed out waiting for idle (%d)!\n",
			   nvgpu_atomic_read(&g->usage_count));
		return -ETIMEDOUT;
	}

	return 0;
}

int gk20a_init_gpu_characteristics(struct gk20a *g)
{
	__nvgpu_set_enabled(g, NVGPU_SUPPORT_PARTIAL_MAPPINGS, true);
	__nvgpu_set_enabled(g, NVGPU_SUPPORT_MAP_DIRECT_KIND_CTRL, true);
	__nvgpu_set_enabled(g, NVGPU_SUPPORT_MAP_BUFFER_BATCH, true);

	if (IS_ENABLED(CONFIG_SYNC)) {
		__nvgpu_set_enabled(g, NVGPU_SUPPORT_SYNC_FENCE_FDS, true);
	}

	if ((g->ops.mm.support_sparse != NULL) && g->ops.mm.support_sparse(g)) {
		__nvgpu_set_enabled(g, NVGPU_SUPPORT_SPARSE_ALLOCS, true);
	}

	/*
	 * Fast submits are supported as long as the user doesn't request
	 * anything that depends on job tracking. (Here, fast means strictly no
	 * metadata, just the gpfifo contents are copied and gp_put updated).
	 */
	__nvgpu_set_enabled(g,
			NVGPU_SUPPORT_DETERMINISTIC_SUBMIT_NO_JOBTRACKING,
			true);

	/*
	 * Sync framework requires deferred job cleanup, wrapping syncs in FDs,
	 * and other heavy stuff, which prevents deterministic submits. This is
	 * supported otherwise, provided that the user doesn't request anything
	 * that depends on deferred cleanup.
	 */
	if (!nvgpu_channel_sync_needs_os_fence_framework(g)) {
		__nvgpu_set_enabled(g,
				NVGPU_SUPPORT_DETERMINISTIC_SUBMIT_FULL,
				true);
	}

	__nvgpu_set_enabled(g, NVGPU_SUPPORT_DETERMINISTIC_OPTS, true);

	__nvgpu_set_enabled(g, NVGPU_SUPPORT_USERSPACE_MANAGED_AS, true);
	__nvgpu_set_enabled(g, NVGPU_SUPPORT_TSG, true);

	if (g->ops.clk_arb.get_arbiter_clk_domains != NULL &&
		g->ops.clk.support_clk_freq_controller) {
		__nvgpu_set_enabled(g, NVGPU_SUPPORT_CLOCK_CONTROLS, true);
	}

	g->ops.gr.detect_sm_arch(g);

	if (g->ops.gr.init_cyclestats != NULL) {
		g->ops.gr.init_cyclestats(g);
	}

	g->ops.gr.get_rop_l2_en_mask(g);

	return 0;
}

/*
 * Free the gk20a struct.
 */
static void gk20a_free_cb(struct nvgpu_ref *refcount)
{
	struct gk20a *g = container_of(refcount,
		struct gk20a, refcount);

	nvgpu_log(g, gpu_dbg_shutdown, "Freeing GK20A struct!");

	gk20a_ce_destroy(g);

	if (g->remove_support) {
		g->remove_support(g);
	}

	if (g->free) {
		g->free(g);
	}
}

/**
 * gk20a_get() - Increment ref count on driver
 *
 * @g The driver to increment
 * This will fail if the driver is in the process of being released. In that
 * case it will return NULL. Otherwise a pointer to the driver passed in will
 * be returned.
 */
struct gk20a * __must_check gk20a_get(struct gk20a *g)
{
	int success;

	/*
	 * Handle the possibility we are still freeing the gk20a struct while
	 * gk20a_get() is called. Unlikely but plausible race condition. Ideally
	 * the code will never be in such a situation that this race is
	 * possible.
	 */
	success = nvgpu_ref_get_unless_zero(&g->refcount);

	nvgpu_log(g, gpu_dbg_shutdown, "GET: refs currently %d %s",
		nvgpu_atomic_read(&g->refcount.refcount),
			(success != 0) ? "" : "(FAILED)");

	return (success != 0) ? g : NULL;
}

/**
 * gk20a_put() - Decrement ref count on driver
 *
 * @g - The driver to decrement
 *
 * Decrement the driver ref-count. If neccesary also free the underlying driver
 * memory
 */
void gk20a_put(struct gk20a *g)
{
	/*
	 * Note - this is racy, two instances of this could run before the
	 * actual kref_put(0 runs, you could see something like:
	 *
	 *  ... PUT: refs currently 2
	 *  ... PUT: refs currently 2
	 *  ... Freeing GK20A struct!
	 */
	nvgpu_log(g, gpu_dbg_shutdown, "PUT: refs currently %d",
		nvgpu_atomic_read(&g->refcount.refcount));

	nvgpu_ref_put(&g->refcount, gk20a_free_cb);
}
