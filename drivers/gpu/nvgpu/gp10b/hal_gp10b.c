/*
 * GP10B Tegra HAL interface
 *
 * Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "gk20a/gk20a.h"
#include "gk20a/dbg_gpu_gk20a.h"
#include "gk20a/css_gr_gk20a.h"
#include "gk20a/bus_gk20a.h"
#include "gk20a/pramin_gk20a.h"
#include "gk20a/flcn_gk20a.h"
#include "gk20a/regops_gk20a.h"
#include "gk20a/mc_gk20a.h"

#include "gp10b/gr_gp10b.h"
#include "gp10b/fecs_trace_gp10b.h"
#include "gp10b/mc_gp10b.h"
#include "gp10b/ltc_gp10b.h"
#include "gp10b/mm_gp10b.h"
#include "gp10b/ce_gp10b.h"
#include "gp10b/fb_gp10b.h"
#include "gp10b/pmu_gp10b.h"
#include "gp10b/gr_ctx_gp10b.h"
#include "gp10b/fifo_gp10b.h"
#include "gp10b/gp10b_gating_reglist.h"
#include "gp10b/regops_gp10b.h"
#include "gp10b/cde_gp10b.h"
#include "gp10b/therm_gp10b.h"
#include "gp10b/priv_ring_gp10b.h"

#include "gm20b/ltc_gm20b.h"
#include "gm20b/gr_gm20b.h"
#include "gm20b/fifo_gm20b.h"
#include "gm20b/pmu_gm20b.h"
#include "gm20b/clk_gm20b.h"

#include "gp10b.h"
#include "hal_gp10b.h"

#include <nvgpu/debug.h>
#include <nvgpu/bug.h>
#include <nvgpu/enabled.h>
#include <nvgpu/bus.h>

#include <nvgpu/hw/gp10b/hw_proj_gp10b.h>
#include <nvgpu/hw/gp10b/hw_fuse_gp10b.h>

static int gp10b_get_litter_value(struct gk20a *g, int value)
{
	int ret = EINVAL;
	switch (value) {
	case GPU_LIT_NUM_GPCS:
		ret = proj_scal_litter_num_gpcs_v();
		break;
	case GPU_LIT_NUM_PES_PER_GPC:
		ret = proj_scal_litter_num_pes_per_gpc_v();
		break;
	case GPU_LIT_NUM_ZCULL_BANKS:
		ret = proj_scal_litter_num_zcull_banks_v();
		break;
	case GPU_LIT_NUM_TPC_PER_GPC:
		ret = proj_scal_litter_num_tpc_per_gpc_v();
		break;
	case GPU_LIT_NUM_SM_PER_TPC:
		ret = proj_scal_litter_num_sm_per_tpc_v();
		break;
	case GPU_LIT_NUM_FBPS:
		ret = proj_scal_litter_num_fbps_v();
		break;
	case GPU_LIT_GPC_BASE:
		ret = proj_gpc_base_v();
		break;
	case GPU_LIT_GPC_STRIDE:
		ret = proj_gpc_stride_v();
		break;
	case GPU_LIT_GPC_SHARED_BASE:
		ret = proj_gpc_shared_base_v();
		break;
	case GPU_LIT_TPC_IN_GPC_BASE:
		ret = proj_tpc_in_gpc_base_v();
		break;
	case GPU_LIT_TPC_IN_GPC_STRIDE:
		ret = proj_tpc_in_gpc_stride_v();
		break;
	case GPU_LIT_TPC_IN_GPC_SHARED_BASE:
		ret = proj_tpc_in_gpc_shared_base_v();
		break;
	case GPU_LIT_PPC_IN_GPC_BASE:
		ret = proj_ppc_in_gpc_base_v();
		break;
	case GPU_LIT_PPC_IN_GPC_STRIDE:
		ret = proj_ppc_in_gpc_stride_v();
		break;
	case GPU_LIT_PPC_IN_GPC_SHARED_BASE:
		ret = proj_ppc_in_gpc_shared_base_v();
		break;
	case GPU_LIT_ROP_BASE:
		ret = proj_rop_base_v();
		break;
	case GPU_LIT_ROP_STRIDE:
		ret = proj_rop_stride_v();
		break;
	case GPU_LIT_ROP_SHARED_BASE:
		ret = proj_rop_shared_base_v();
		break;
	case GPU_LIT_HOST_NUM_ENGINES:
		ret = proj_host_num_engines_v();
		break;
	case GPU_LIT_HOST_NUM_PBDMA:
		ret = proj_host_num_pbdma_v();
		break;
	case GPU_LIT_LTC_STRIDE:
		ret = proj_ltc_stride_v();
		break;
	case GPU_LIT_LTS_STRIDE:
		ret = proj_lts_stride_v();
		break;
	/* GP10B does not have a FBPA unit, despite what's listed in the
	 * hw headers or read back through NV_PTOP_SCAL_NUM_FBPAS,
	 * so hardcode all values to 0.
	 */
	case GPU_LIT_NUM_FBPAS:
	case GPU_LIT_FBPA_STRIDE:
	case GPU_LIT_FBPA_BASE:
	case GPU_LIT_FBPA_SHARED_BASE:
		ret = 0;
		break;
	default:
		nvgpu_err(g, "Missing definition %d", value);
		BUG();
		break;
	}

	return ret;
}

static const struct gpu_ops gp10b_ops = {
	.ltc = {
		.determine_L2_size_bytes = gp10b_determine_L2_size_bytes,
		.set_zbc_color_entry = gm20b_ltc_set_zbc_color_entry,
		.set_zbc_depth_entry = gm20b_ltc_set_zbc_depth_entry,
		.init_cbc = gm20b_ltc_init_cbc,
		.init_fs_state = gp10b_ltc_init_fs_state,
		.init_comptags = gp10b_ltc_init_comptags,
		.cbc_ctrl = gm20b_ltc_cbc_ctrl,
		.isr = gp10b_ltc_isr,
		.cbc_fix_config = gm20b_ltc_cbc_fix_config,
		.flush = gm20b_flush_ltc,
#ifdef CONFIG_DEBUG_FS
		.sync_debugfs = gp10b_ltc_sync_debugfs,
#endif
	},
	.clock_gating = {
		.slcg_bus_load_gating_prod =
			gp10b_slcg_bus_load_gating_prod,
		.slcg_ce2_load_gating_prod =
			gp10b_slcg_ce2_load_gating_prod,
		.slcg_chiplet_load_gating_prod =
			gp10b_slcg_chiplet_load_gating_prod,
		.slcg_ctxsw_firmware_load_gating_prod =
			gp10b_slcg_ctxsw_firmware_load_gating_prod,
		.slcg_fb_load_gating_prod =
			gp10b_slcg_fb_load_gating_prod,
		.slcg_fifo_load_gating_prod =
			gp10b_slcg_fifo_load_gating_prod,
		.slcg_gr_load_gating_prod =
			gr_gp10b_slcg_gr_load_gating_prod,
		.slcg_ltc_load_gating_prod =
			ltc_gp10b_slcg_ltc_load_gating_prod,
		.slcg_perf_load_gating_prod =
			gp10b_slcg_perf_load_gating_prod,
		.slcg_priring_load_gating_prod =
			gp10b_slcg_priring_load_gating_prod,
		.slcg_pmu_load_gating_prod =
			gp10b_slcg_pmu_load_gating_prod,
		.slcg_therm_load_gating_prod =
			gp10b_slcg_therm_load_gating_prod,
		.slcg_xbar_load_gating_prod =
			gp10b_slcg_xbar_load_gating_prod,
		.blcg_bus_load_gating_prod =
			gp10b_blcg_bus_load_gating_prod,
		.blcg_ce_load_gating_prod =
			gp10b_blcg_ce_load_gating_prod,
		.blcg_ctxsw_firmware_load_gating_prod =
			gp10b_blcg_ctxsw_firmware_load_gating_prod,
		.blcg_fb_load_gating_prod =
			gp10b_blcg_fb_load_gating_prod,
		.blcg_fifo_load_gating_prod =
			gp10b_blcg_fifo_load_gating_prod,
		.blcg_gr_load_gating_prod =
			gp10b_blcg_gr_load_gating_prod,
		.blcg_ltc_load_gating_prod =
			gp10b_blcg_ltc_load_gating_prod,
		.blcg_pwr_csb_load_gating_prod =
			gp10b_blcg_pwr_csb_load_gating_prod,
		.blcg_pmu_load_gating_prod =
			gp10b_blcg_pmu_load_gating_prod,
		.blcg_xbar_load_gating_prod =
			gp10b_blcg_xbar_load_gating_prod,
		.pg_gr_load_gating_prod =
			gr_gp10b_pg_gr_load_gating_prod,
	},
	.mc = {
		.intr_enable = mc_gp10b_intr_enable,
		.intr_unit_config = mc_gp10b_intr_unit_config,
		.isr_stall = mc_gp10b_isr_stall,
		.intr_stall = mc_gp10b_intr_stall,
		.intr_stall_pause = mc_gp10b_intr_stall_pause,
		.intr_stall_resume = mc_gp10b_intr_stall_resume,
		.intr_nonstall = mc_gp10b_intr_nonstall,
		.intr_nonstall_pause = mc_gp10b_intr_nonstall_pause,
		.intr_nonstall_resume = mc_gp10b_intr_nonstall_resume,
		.enable = gk20a_mc_enable,
		.disable = gk20a_mc_disable,
		.reset = gk20a_mc_reset,
		.boot_0 = gk20a_mc_boot_0,
		.is_intr1_pending = mc_gp10b_is_intr1_pending,
	},
	.debug = {
		.show_dump = gk20a_debug_show_dump,
	},
	.dbg_session_ops = {
		.exec_reg_ops = exec_regops_gk20a,
		.dbg_set_powergate = dbg_set_powergate,
		.check_and_set_global_reservation =
			nvgpu_check_and_set_global_reservation,
		.check_and_set_context_reservation =
			nvgpu_check_and_set_context_reservation,
		.release_profiler_reservation =
			nvgpu_release_profiler_reservation,
		.perfbuffer_enable = gk20a_perfbuf_enable_locked,
		.perfbuffer_disable = gk20a_perfbuf_disable_locked,
	},
	.cde = {
		.get_program_numbers = gp10b_cde_get_program_numbers,
		.need_scatter_buffer = gp10b_need_scatter_buffer,
		.populate_scatter_buffer = gp10b_populate_scatter_buffer,
	},
	.bus = {
		.init_hw = gk20a_bus_init_hw,
		.isr = gk20a_bus_isr,
		.read_ptimer = gk20a_read_ptimer,
		.get_timestamps_zipper = nvgpu_get_timestamps_zipper,
		.bar1_bind = gk20a_bus_bar1_bind,
	},
#if defined(CONFIG_GK20A_CYCLE_STATS)
	.css = {
		.enable_snapshot = css_hw_enable_snapshot,
		.disable_snapshot = css_hw_disable_snapshot,
		.check_data_available = css_hw_check_data_available,
		.set_handled_snapshots = css_hw_set_handled_snapshots,
		.allocate_perfmon_ids = css_gr_allocate_perfmon_ids,
		.release_perfmon_ids = css_gr_release_perfmon_ids,
	},
#endif
	.falcon = {
		.falcon_hal_sw_init = gk20a_falcon_hal_sw_init,
	},
	.chip_init_gpu_characteristics = gp10b_init_gpu_characteristics,
	.get_litter_value = gp10b_get_litter_value,
};

int gp10b_init_hal(struct gk20a *g)
{
	struct gpu_ops *gops = &g->ops;
	struct nvgpu_gpu_characteristics *c = &g->gpu_characteristics;
	u32 val;

	gops->ltc = gp10b_ops.ltc;
	gops->clock_gating = gp10b_ops.clock_gating;
	gops->mc = gp10b_ops.mc;
	gops->debug = gp10b_ops.debug;
	gops->dbg_session_ops = gp10b_ops.dbg_session_ops;
	gops->cde = gp10b_ops.cde;
	gops->bus = gp10b_ops.bus;
#if defined(CONFIG_GK20A_CYCLE_STATS)
	gops->css = gp10b_ops.css;
#endif
	gops->falcon = gp10b_ops.falcon;

	/* Lone Functions */
	gops->chip_init_gpu_characteristics =
		gp10b_ops.chip_init_gpu_characteristics;
	gops->get_litter_value = gp10b_ops.get_litter_value;

	gops->pmupstate = false;
#ifdef CONFIG_TEGRA_ACR
	if (nvgpu_is_enabled(g, NVGPU_IS_FMODEL)) {
		gops->privsecurity = 0;
		gops->securegpccs = 0;
	} else if (g->is_virtual) {
		gops->privsecurity = 1;
		gops->securegpccs = 1;
	} else {
		val = gk20a_readl(g, fuse_opt_priv_sec_en_r());
		if (val) {
			gops->privsecurity = 1;
			gops->securegpccs =1;
		} else {
			gk20a_dbg_info("priv security is disabled in HW");
			gops->privsecurity = 0;
			gops->securegpccs = 0;
		}
	}
#else
	if (nvgpu_is_enabled(g, NVGPU_IS_FMODEL)) {
		gk20a_dbg_info("running simulator with PRIV security disabled");
		gops->privsecurity = 0;
		gops->securegpccs = 0;
	} else {
		val = gk20a_readl(g, fuse_opt_priv_sec_en_r());
		if (val) {
			gk20a_dbg_info("priv security is not supported but enabled");
			gops->privsecurity = 1;
			gops->securegpccs =1;
			return -EPERM;
		} else {
			gops->privsecurity = 0;
			gops->securegpccs = 0;
		}
	}
#endif

	g->bootstrap_owner = LSF_BOOTSTRAP_OWNER_DEFAULT;
	gp10b_init_priv_ring(gops);
	gp10b_init_gr(gops);
	gp10b_init_fecs_trace_ops(gops);
	gp10b_init_fb(gops);
	gp10b_init_fifo(gops);
	gp10b_init_ce(gops);
	gp10b_init_gr_ctx(gops);
	gp10b_init_mm(gops);
	gp10b_init_pmu_ops(gops);
	gp10b_init_regops(gops);
	gp10b_init_therm_ops(gops);
	gk20a_init_tsg_ops(gops);
	gk20a_init_pramin_ops(gops);

	g->name = "gp10b";

	c->twod_class = FERMI_TWOD_A;
	c->threed_class = PASCAL_A;
	c->compute_class = PASCAL_COMPUTE_A;
	c->gpfifo_class = PASCAL_CHANNEL_GPFIFO_A;
	c->inline_to_memory_class = KEPLER_INLINE_TO_MEMORY_B;
	c->dma_copy_class = PASCAL_DMA_COPY_A;

	return 0;
}
