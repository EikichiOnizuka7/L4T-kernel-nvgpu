/*
 * general clock structures & definitions
 *
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef NVGPU_PMU_CLK_FREQ_DOMAIN_H
#define NVGPU_PMU_CLK_FREQ_DOMAIN_H

#include <nvgpu/types.h>
#include <nvgpu/boardobj.h>

struct gk20a;
struct boardobjgrp_e32;

struct nvgpu_clk_freq_domain_grp {
	struct boardobjgrp_e32 super;
	u32 init_flags;
};

struct nvgpu_clk_freq_domain {
	struct boardobj super;
	u32 clk_domain;
};

int nvgpu_clk_freq_domain_init_pmupstate(struct gk20a *g);
void nvgpu_clk_freq_domain_free_pmupstate(struct gk20a *g);
int nvgpu_clk_freq_domain_sw_setup(struct gk20a *g);
int nvgpu_clk_freq_domain_pmu_setup(struct gk20a *g);

#endif /* NVGPU_PMU_CLK_FREQ_DOMAIN_H */
