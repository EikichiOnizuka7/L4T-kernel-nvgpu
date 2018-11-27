/*
 * Copyright (c) 2016-2019, NVIDIA CORPORATION.  All rights reserved.
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
#include <nvgpu/gk20a.h>
#include <nvgpu/boardobjgrp.h>
#include <nvgpu/pmuif/ctrlboardobj.h>

/*
* Assures that unused bits (size .. (maskDataCount * 32 - 1)) are always zero.
*/
#define BOARDOBJGRPMASK_NORMALIZE(_pmask)                                      \
	((_pmask)->data[(_pmask)->maskdatacount-1U] &= (_pmask)->lastmaskfilter)

int boardobjgrpmask_init(struct boardobjgrpmask *mask, u8 bitsize,
		struct ctrl_boardobjgrp_mask *extmask)
{
	if (mask == NULL) {
		return -EINVAL;
	}
	if ((bitsize != CTRL_BOARDOBJGRP_E32_MAX_OBJECTS) &&
		(bitsize != CTRL_BOARDOBJGRP_E255_MAX_OBJECTS)) {
		return -EINVAL;
	}

	mask->bitcount = bitsize;
	mask->maskdatacount = CTRL_BOARDOBJGRP_MASK_DATA_SIZE(bitsize);
	mask->lastmaskfilter = U32(bitsize) %
		CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_BIT_SIZE;

	mask->lastmaskfilter = (mask->lastmaskfilter == 0U) ?
		0xFFFFFFFFU : (BIT32(mask->lastmaskfilter) - 1U);

	return (extmask == NULL) ?
		boardobjgrpmask_clr(mask) :
		boardobjgrpmask_import(mask, bitsize, extmask);
}

int boardobjgrpmask_import(struct boardobjgrpmask *mask, u8 bitsize,
		struct ctrl_boardobjgrp_mask *extmask)
{
	u8 index;

	if (mask == NULL) {
		return -EINVAL;
	}
	if (extmask == NULL) {
		return -EINVAL;
	}
	if (mask->bitcount != bitsize) {
		return -EINVAL;
	}

	for (index = 0; index < mask->maskdatacount; index++) {
		mask->data[index] = extmask->data[index];
	}

	BOARDOBJGRPMASK_NORMALIZE(mask);

	return 0;
}

int boardobjgrpmask_export(struct boardobjgrpmask *mask, u8 bitsize,
		struct ctrl_boardobjgrp_mask *extmask)
{
	u8 index;

	if (mask == NULL) {
		return -EINVAL;
	}
	if (extmask == NULL) {
		return -EINVAL;
	}
	if (mask->bitcount != bitsize) {
		return -EINVAL;
	}

	for (index = 0; index < mask->maskdatacount; index++) {
		extmask->data[index] = mask->data[index];
	}

	return 0;
}

int boardobjgrpmask_clr(struct boardobjgrpmask *mask)
{
	u8 index;

	if (mask == NULL) {
		return -EINVAL;
	}
	for (index = 0; index < mask->maskdatacount; index++) {
		mask->data[index] = 0;
	}

	return 0;
}

int boardobjgrpmask_set(struct boardobjgrpmask *mask)
{
	u8 index;

	if (mask == NULL) {
		return -EINVAL;
	}
	for (index = 0; index < mask->maskdatacount; index++) {
		mask->data[index] = 0xFFFFFFFFU;
	}
	BOARDOBJGRPMASK_NORMALIZE(mask);
	return 0;
}

int boardobjgrpmask_inv(struct boardobjgrpmask *mask)
{
	u8 index;

	if (mask == NULL) {
		return -EINVAL;
	}
	for (index = 0; index < mask->maskdatacount; index++) {
		mask->data[index] = ~mask->data[index];
	}
	BOARDOBJGRPMASK_NORMALIZE(mask);
	return 0;
}

bool boardobjgrpmask_iszero(struct boardobjgrpmask *mask)
{
	u8 index;

	if (mask == NULL) {
		return true;
	}
	for (index = 0; index < mask->maskdatacount; index++) {
		if (mask->data[index] != 0U) {
			return false;
		}
	}
	return true;
}

u8 boardobjgrpmask_bitsetcount(struct boardobjgrpmask *mask)
{
	u8 index;
	u8 result = 0;

	if (mask == NULL) {
		return result;
	}

	for (index = 0; index < mask->maskdatacount; index++) {
		u32 m = mask->data[index];

		NUMSETBITS_32(m);
		result += (u8)m;
	}

	return result;
}

u8 boardobjgrpmask_bitidxlowest(struct boardobjgrpmask *mask)
{
	u8 index;
	u8 result = CTRL_BOARDOBJ_IDX_INVALID;

	if (mask == NULL) {
		return result;
	}

	for (index = 0; index < mask->maskdatacount; index++) {
		u32 m = mask->data[index];

		if (m != 0U) {
			LOWESTBITIDX_32(m);
			result = (u8)m + index *
			CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_BIT_SIZE;
			break;
		}
	}

	return result;
}

u8 boardobjgrpmask_bitidxhighest(struct boardobjgrpmask *mask)
{
	u8 index;
	u8 result = CTRL_BOARDOBJ_IDX_INVALID;

	if (mask == NULL) {
		return result;
	}

	for (index = 0; index < mask->maskdatacount; index++) {
		u32 m = mask->data[index];

		if (m != 0U) {
			HIGHESTBITIDX_32(m);
			result = (u8)m + index *
			CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_BIT_SIZE;
			break;
		}
	}

	return result;
}

int boardobjgrpmask_bitclr(struct boardobjgrpmask *mask, u8 bitidx)
{
	u8 index;
	u8 offset;

	if (mask == NULL) {
		return -EINVAL;
	}
	if (bitidx >= mask->bitcount) {
		return -EINVAL;
	}

	index = CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_INDEX(bitidx);
	offset = CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_OFFSET(bitidx);

	mask->data[index] &= ~BIT(offset);

	return 0;
}

int boardobjgrpmask_bitset(struct boardobjgrpmask *mask, u8 bitidx)
{
	u8 index;
	u8 offset;

	if (mask == NULL) {
		return -EINVAL;
	}
	if (bitidx >= mask->bitcount) {
		return -EINVAL;
	}

	index = CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_INDEX(bitidx);
	offset = CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_OFFSET(bitidx);

	mask->data[index] |= BIT(offset);

	return 0;
}

int boardobjgrpmask_bitinv(struct boardobjgrpmask *mask, u8 bitidx)
{
	u8 index;
	u8 offset;

	if (mask == NULL) {
		return -EINVAL;
	}
	if (bitidx >= mask->bitcount) {
		return -EINVAL;
	}

	index = CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_INDEX(bitidx);
	offset = CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_OFFSET(bitidx);

	mask->data[index] ^= ~BIT(offset);

	return 0;
}

bool boardobjgrpmask_bitget(struct boardobjgrpmask *mask, u8 bitidx)
{
	u8 index;
	u8 offset;

	if (mask == NULL) {
		return false;
	}
	if (bitidx >= mask->bitcount) {
		return false;
	}

	index = CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_INDEX(bitidx);
	offset = CTRL_BOARDOBJGRP_MASK_MASK_ELEMENT_OFFSET(bitidx);

	return (mask->data[index] & BIT(offset)) != 0U;
}

int boardobjgrpmask_and(struct boardobjgrpmask *dst,
			struct boardobjgrpmask *op1,
			struct boardobjgrpmask *op2)
{
	u8 index;

	if (!boardobjgrpmask_sizeeq(dst, op1)) {
		return -EINVAL;
	}
	if (!boardobjgrpmask_sizeeq(dst, op2)) {
		return -EINVAL;
	}

	for (index = 0; index < dst->maskdatacount; index++) {
		dst->data[index] = op1->data[index] & op2->data[index];
	}

	return 0;
}

int boardobjgrpmask_or(struct boardobjgrpmask *dst,
		       struct boardobjgrpmask *op1,
		       struct boardobjgrpmask *op2)
{
	u8 index;

	if (!boardobjgrpmask_sizeeq(dst, op1)) {
		return -EINVAL;
	}
	if (!boardobjgrpmask_sizeeq(dst, op2)) {
		return -EINVAL;
	}

	for (index = 0; index < dst->maskdatacount; index++) {
		dst->data[index] = op1->data[index] | op2->data[index];
	}

	return 0;
}

int boardobjgrpmask_xor(struct boardobjgrpmask *dst,
			struct boardobjgrpmask *op1,
			struct boardobjgrpmask *op2)
{
	u8 index;

	if (!boardobjgrpmask_sizeeq(dst, op1)) {
		return -EINVAL;
	}
	if (!boardobjgrpmask_sizeeq(dst, op2)) {
		return -EINVAL;
	}

	for (index = 0; index < dst->maskdatacount; index++) {
		dst->data[index] = op1->data[index] ^ op2->data[index];
	}

	return 0;
}

int boardobjgrpmask_copy(struct boardobjgrpmask *dst,
		struct boardobjgrpmask *src)
{
	u8 index;

	if (!boardobjgrpmask_sizeeq(dst, src)) {
		return -EINVAL;
	}

	for (index = 0; index < dst->maskdatacount; index++) {
		dst->data[index] = src->data[index];
	}

	return 0;
}

bool boardobjgrpmask_sizeeq(struct boardobjgrpmask *op1,
		struct boardobjgrpmask *op2)
{
	if (op1 == NULL) {
		return false;
	}
	if (op2 == NULL) {
		return false;
	}

	return op1->bitcount == op2->bitcount;
}

bool boardobjgrpmask_issubset(struct boardobjgrpmask *op1,
		struct boardobjgrpmask *op2)
{
	u8 index;

	if (!boardobjgrpmask_sizeeq(op2, op1)) {
		return false;
	}

	for (index = 0; index < op1->maskdatacount; index++) {
		u32 op_1 = op1->data[index];
		u32 op_2 = op2->data[index];

		if ((op_1 & op_2) != op_1) {
			return false;
		}
	}

	return true;
}
