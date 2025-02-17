/*
 * Copyright (C) 2016-2021 C-SKY Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* CSI-NN2 version 1.10.x */

#include "csi_ref.h"
#include "csi_utils.h"
#include <time.h>

int32_t csi_ref_max_internal_s32(int32_t a, int32_t b)
{
    if (a >= b) {
        return a;
    } else {
        return b;
    }
}

int32_t csi_ref_min_internal_s32(int32_t a, int32_t b)
{
    if (a <= b) {
        return a;
    } else {
        return b;
    }
}

int32_t csi_ref_get_index(int32_t *dim, int32_t index0, int32_t index1, int32_t index2, int32_t index3)
{
    return ((index0 * dim[1] + index1) * dim[2] + index2) * dim[3] + index3;
}

int32_t csi_ref_get_index_5(int32_t *dim, int32_t index0, int32_t index1, int32_t index2, int32_t index3, int32_t index4)
{
    return dim[4] * (dim[3] * (dim[2] * (dim[1] * index0 + index1) + index2) + index3) + index4;
}

/* iteration to calculate index */
int32_t csi_ref_get_index_iter(int32_t *dim, int dim_idx, int32_t *index)
{
    int32_t ret;
    if (dim_idx > 0) {
        ret = csi_ref_get_index_iter(dim, dim_idx - 1, index) * dim[dim_idx] + index[dim_idx];
    } else {
        ret = index[dim_idx];
    }

    return ret;
}

int32_t *csi_ref_get_input_dim(struct csi_tensor *input, int dim_count, int32_t *axis, int axis_size)
{
    int8_t alloc_size = dim_count * sizeof(int32_t *);
    int32_t *ret = csi_mem_alloc(alloc_size);

    for (int i = 0; i < dim_count; i++) {
        ret[i] = 1;
    }

    for (int i = 0; i < axis_size; i++) {
        ret[axis[i]] = input->dim[axis[i]];
    }

    return ret;
}

int csi_check_rhs_shape(struct csi_tensor *input)
{
    int axis = -1;
    int in_size = csi_tensor_size(input);
    for (int i = 0; i < input->dim_count; i++)
    {
        if (input->dim[i] == in_size){axis = i;}
    }
    return axis;
}

int csi_ref_diso_broadcast_base(struct csi_tensor *input0,
                                struct csi_tensor *input1,
                                struct csi_tensor *output,
                                struct diso_params *params,
                                struct csi_ref_diso_callback *cb)
{
    float *input0_data = input0->data;
    float *input1_data = input1->data;
    float *output_data = output->data;

    cb->output = output;

    int out_size = csi_tensor_size(output);
    float *in0_data_b = csi_mem_alloc(out_size * 4);
    float *in1_data_b = csi_mem_alloc(out_size * 4);

    struct csi_tensor *b_input0 = csi_alloc_tensor(NULL);
    struct csi_tensor *b_input1 = csi_alloc_tensor(NULL);
    csi_tensor_copy(b_input0, output);
    csi_tensor_copy(b_input1, output);
    b_input0->data = in0_data_b;
    b_input1->data = in1_data_b;

    if (csi_ref_broadcast_to_shape(input0, b_input0, output->dim, output->dim_count) == CSINN_FALSE)
    {
        CSI_DEBUG_CALL(csi_debug_info("%s: broadcast input0 failed.", __func__));
        return CSINN_FALSE;
    };
    if (csi_ref_broadcast_to_shape(input1, b_input1, output->dim, output->dim_count) == CSINN_FALSE)
    {
        CSI_DEBUG_CALL(csi_debug_info("%s: broadcast input1 failed.", __func__));
        return CSINN_FALSE;
    };

    int size0 = csi_tensor_size(b_input0);
    int size1 = csi_tensor_size(b_input1);

    if (size0 == size1) {
        for (int i = 0; i < size0; i++) {
            cb->bc(in0_data_b, in1_data_b, output_data, i, i);
        }
    }else{
        return CSINN_FALSE;
    }
    csi_mem_free(in0_data_b);
    csi_mem_free(in1_data_b);
    return CSINN_TRUE;
}

float csi_ref_get_scale(int32_t multiplier, int32_t shift)
{
    float scale = multiplier / pow(2, 31) * pow(2, shift);

    return scale;
}

static int32_t mask_non_zero(int32_t a)
{
    int32_t zero = 0;
    return a ? (~zero) : zero;
}

static int32_t round_div_pot(int32_t x, int32_t exponent)
{
    assert(exponent >= 0);
    assert(exponent <= 31);
    int32_t mask = (1ll << exponent) - 1;
    int32_t zero = 0;
    int32_t one = 1;
    int32_t remainder = x & mask;
    int32_t threshold = (mask >> 1) + (mask_non_zero(x < zero) & one);
    return (x >> exponent) + (mask_non_zero(remainder > threshold) & one);
}

static int32_t high_mul_sat_round_double(int32_t a, int32_t b)
{
    int overflow = a == b && a == INT32_MIN;
    int64_t a_64 = a;
    int64_t b_64 = b;
    int64_t ab_64 = a_64 * b_64;
    int32_t nudge = ab_64 >= 0 ? (1 << 30) : (1 - (1 << 30));
    int32_t ab_x2_high32 = (int32_t)((ab_64 + nudge) / (1ll << 31));
    return overflow ? INT32_MAX : ab_x2_high32;
}

uint8_t csi_ref_quantize_channel_u8(int32_t data, struct csi_tensor* input, struct csi_tensor* output, float wscale)
{
    float out = data * input->qinfo->scale * wscale;
    return csi_ref_quantize_f32_to_u8(out, output->qinfo);
}

int8_t csi_ref_quantize_channel_i8(int32_t data, struct csi_tensor* input, struct csi_tensor* output, float wscale)
{
    float out = data * input->qinfo->scale * wscale;
    return csi_ref_quantize_f32_to_i8(out, output->qinfo);
}

float csi_ref_dequantize_u8_to_f32(uint8_t input, struct csi_quant_info *qinfo)
{
    float x = input;
    x -= qinfo->zero_point;
    float scale = csi_ref_get_scale(qinfo->multiplier, qinfo->shift);
    return x * scale;
}

float csi_ref_dequantize_i8_to_f32(int8_t input, struct csi_quant_info *qinfo)
{
    float x = input;
    x -= qinfo->zero_point;
    float scale = csi_ref_get_scale(qinfo->multiplier, qinfo->shift);
    return x * scale;
}

uint8_t csi_ref_quantize_f32_to_u8(float input, struct csi_quant_info *qinfo)
{
    float scale = csi_ref_get_scale(qinfo->multiplier, qinfo->shift);
    float output = round(input / scale + qinfo->zero_point);
    return fmin(255, fmax(0, output));
}

int8_t csi_ref_quantize_f32_to_i8(float input, struct csi_quant_info *qinfo)
{
    float scale = csi_ref_get_scale(qinfo->multiplier, qinfo->shift);
    float output = round(input / scale + qinfo->zero_point);
    return fmin(127, fmax(-127, output));
}

struct csi_tensor *csi_ref_deconv_kernel_nchw_to_nhwc_f32(struct csi_tensor *t, int32_t *permute)
{
    struct csi_tensor *nt = csi_alloc_tensor(NULL);

    assert(t->dim_count < 5);

    int size = csi_tensor_byte_size(t);

    for (int i = t->dim_count; i < 4; i++) {
        t->dim[i] = 1;
    }

    int t_dim = t->dim_count;
    t->dim_count = 4;
    t->quant_channel = 0;
    csi_tensor_copy(nt, t);
    nt->dim[0] = t->dim[permute[0]];
    nt->dim[1] = t->dim[permute[1]];
    nt->dim[2] = t->dim[permute[2]];
    nt->dim[3] = t->dim[permute[3]];

    nt->data = csi_mem_alloc(size);

    struct transpose_params tparams;
    tparams.permute = permute;
    tparams.base.api = CSINN_REF;
    tparams.base.name = "internal_transpose";
    csi_ref_transpose(t, nt, &tparams);
    t->dim_count = t_dim;
    return nt;
}

struct csi_tensor *csi_ref_nchw_to_nhwc_8(struct csi_tensor *t)
{
    struct csi_tensor *nt = csi_alloc_tensor(NULL);

    assert(t->dim_count < 5);

    int size = 1;
    for (int i = 0; i < t->dim_count; i++) {
        size = size * t->dim[i];
    }

    for (int i = t->dim_count; i < 4; i++) {
        t->dim[i] = 1;
    }

    int t_dim = t->dim_count;
    t->dim_count = 4;
    csi_tensor_copy(nt, t);
    nt->dim[1] = t->dim[2];
    nt->dim[2] = t->dim[3];
    nt->dim[3] = t->dim[1];

    nt->data = csi_mem_alloc(size);
    int32_t permute[4] = {0, 2, 3, 1};

    struct transpose_params tparams;
    tparams.permute = permute;
    tparams.base.api = CSINN_REF;
    tparams.base.name = "internal_transpose";
    csi_ref_transpose(t, nt, &tparams);
    t->dim_count = t_dim;
    return nt;
}

void csi_ref_nhwc_to_nchw_8(struct csi_tensor *nt, struct csi_tensor *t)
{
    nt->dim[1] = t->dim[3];
    nt->dim[2] = t->dim[1];
    nt->dim[3] = t->dim[2];

    int nt_dim = nt->dim_count;
    nt->dim_count = 4;

    int32_t permute[4] = {0, 3, 1, 2};

    struct transpose_params tparams;
    tparams.permute = permute;
    tparams.base.api = CSINN_REF;
    tparams.base.name = "internal_transpose";
    csi_ref_transpose(t, nt, &tparams);

    nt->dim_count = nt_dim;

    csi_mem_free(t->data);
    csi_mem_free(t);
}

struct csi_tensor *csi_ref_nchw_to_nhwc_f32(struct csi_tensor *t)
{
    struct csi_tensor *nt = csi_alloc_tensor(NULL);

    assert(t->dim_count < 5);

    int size = 1;
    for (int i = 0; i < t->dim_count; i++) {
        size = size * t->dim[i];
    }

    for (int i = t->dim_count; i < 4; i++) {
        t->dim[i] = 1;
    }

    int t_dim = t->dim_count;
    t->dim_count = 4;
    t->quant_channel = 0;
    csi_tensor_copy(nt, t);
    nt->dim[1] = t->dim[2];
    nt->dim[2] = t->dim[3];
    nt->dim[3] = t->dim[1];

    nt->data = csi_mem_alloc(size * 4);
    int32_t permute[4] = {0, 2, 3, 1};

    struct transpose_params tparams;
    tparams.permute = permute;
    tparams.permute_num = 4;
    tparams.base.api = CSINN_REF;
    tparams.base.name = "internal_transpose";
    csi_ref_transpose(t, nt, &tparams);
    t->dim_count = t_dim;
    return nt;
}

void csi_ref_nhwc_to_nchw_f32(struct csi_tensor *nt, struct csi_tensor *t)
{
    nt->dim[1] = t->dim[3];
    nt->dim[2] = t->dim[1];
    nt->dim[3] = t->dim[2];

    int nt_dim = nt->dim_count;
    nt->dim_count = 4;

    int32_t permute[4] = {0, 3, 1, 2};

    struct transpose_params tparams;
    tparams.permute = permute;
    tparams.permute_num = 4;
    tparams.base.api = CSINN_REF;
    tparams.base.name = "internal_transpose";
    csi_ref_transpose(t, nt, &tparams);

    nt->dim_count = nt_dim;

    if (t->qinfo != NULL) {
        csi_mem_free(t->qinfo);
        t->qinfo = NULL;
    }
    csi_mem_free(t->data);
    csi_mem_free(t);
}

int32_t csi_ref_get_reduction_index(int32_t k, const int32_t *strides,
                            const int32_t *extents, int32_t n)
{
    int32_t index = 0;
    for (int32_t i = 0; i < n; i++)
    {
        int32_t div = 1;
        for (int32_t j = i + 1; j < n; j++)
        {
            div *= extents[j];
        }
        int32_t mod = div * extents[i];

        index += ((k % mod) / div * strides[i]);
    }

    return index;
}

float csi_ref_uint8_to_float(uint8_t i, struct csi_tensor *t)
{
    return ((float)i - t->qinfo->zero_point) * t->qinfo->scale;
}

float csi_ref_int8_to_float(int8_t i, struct csi_tensor *t)
{
    return ((float)i - t->qinfo->zero_point) * t->qinfo->scale;
}

int16_t csi_ref_float32_to_float16(float value)
{
    int16_t ret;
    if (value > -6.1e-5 && value < 6.1e-5) {
        /* to small for f16, ignore to 0 */
        return 0;
    }
    if (value > 65504) {
        csi_debug_error("too large f32 to f16\n");
        /* saturate to f16 max value: 65504 */
        value = 65504;
    }
    int32_t org_format = *(int32_t *)&value;
    int16_t sign = (org_format & 0x80000000) >> 16;
    int16_t frac = (org_format & 0x7fffff) >> 13;
    int16_t exp = (((((org_format >> 23) & 0xff) - 128) + 16) & 0x1f) << 10;
    ret = sign | frac | exp;
    return ret;
}

float csi_ref_float16_to_float32(int16_t value)
{
    float ret;
    if (value == 0 || value == 0x8000) {
      return 0;
    }
    int32_t ret_format = 0;
    int32_t sign = (value & 0x8000) << 16;
    int32_t frac = (value & 0x3ff) << 13;
    int32_t exp = (((((value >> 10) & 0x1f) - 16) + 128) & 0xff) << 23;
    ret_format = sign | frac | exp;
    ret = *(float *)&ret_format;
    return ret;
}

struct csi_tensor *csi_ref_alloc_float_tensor(struct csi_tensor *src)
{
    struct csi_tensor *ret = csi_alloc_tensor(NULL);
    csi_tensor_copy(ret, src);
    ret->dtype = CSINN_DTYPE_FLOAT32;
    int size = csi_tensor_byte_size(ret);
    float *data = csi_mem_alloc(size);
    ret->data = data;
    return ret;
}

void csi_ref_free_float_tensor(struct csi_tensor *src)
{
    csi_mem_free(src->data);
    csi_free_tensor(src);
}

struct csi_tensor *csi_ref_convert_float_tensor(struct csi_tensor *src)
{
    struct csi_tensor *ret = csi_ref_alloc_float_tensor(src);
    int size = csi_tensor_size(src);
    float *float_data = ret->data;

    if (src->dtype == CSINN_DTYPE_UINT8) {
        uint8_t *input_data = src->data;
        for (int i = 0; i < size; i++) {
            float_data[i] = csi_ref_uint8_to_float(input_data[i], src);
        }
    } else if (src->dtype == CSINN_DTYPE_INT8) {
        int8_t *input_data = src->data;
        for (int i = 0; i < size; i++) {
            float_data[i] = csi_ref_int8_to_float(input_data[i], src);
        }
    } else {
        return NULL;
    }

    return ret;
}

void csi_ref_conv_free_float_tensor(struct csi_tensor *input,
    struct csi_tensor *output, struct csi_tensor *kernel,
    struct csi_tensor *bias)
{
    csi_ref_free_float_tensor(input);
    csi_ref_free_float_tensor(output);
    csi_ref_free_float_tensor(kernel);
    csi_ref_free_float_tensor(bias);
}

struct csi_tensor *csi_ref_tensor_transform_f32(struct csi_tensor *input)
{
    struct csi_tensor *ret = csi_alloc_tensor(NULL);
    csi_tensor_copy(ret, input);
    if (ret->qinfo != NULL) {
        csi_mem_free(ret->qinfo);
        ret->qinfo = NULL;
    }
    ret->quant_channel = 0;
    ret->dtype = CSINN_DTYPE_FLOAT32;
    if (ret->dim_count == 0) {
        return ret;
    }
    ret->data = csi_mem_alloc(csi_tensor_size(input) * 4);
    if (csi_tensor_data_convert(ret, input) == CSINN_TRUE) {
        return ret;
    }
    return NULL;
}

int csi_ref_tensor_transform_free_f32(struct csi_tensor *input)
{
    csi_mem_free(input->data);
    csi_free_tensor(input);
    return CSINN_TRUE;
}

int csi_ref_siso_callback_base(struct csi_tensor *input,
                               struct csi_tensor *output,
                               void *params,
                               void *cb)
{
    int (*callback)() = cb;
    int ret;
    struct csi_tensor *finput = csi_ref_tensor_transform_f32(input);
    struct csi_tensor *foutput = csi_ref_tensor_transform_f32(output);
    ret = callback(finput, foutput, params);
    csi_tensor_data_convert(output, foutput);
    csi_ref_tensor_transform_free_f32(finput);
    csi_ref_tensor_transform_free_f32(foutput);
    return ret;
}

int csi_ref_diso_callback_base(struct csi_tensor *input0,
                               struct csi_tensor *input1,
                               struct csi_tensor *output,
                               void *params,
                               void *cb)
{
    int (*callback)() = cb;
    int ret;
    struct csi_tensor *finput0 = csi_ref_tensor_transform_f32(input0);
    struct csi_tensor *finput1 = csi_ref_tensor_transform_f32(input1);
    struct csi_tensor *foutput = csi_ref_tensor_transform_f32(output);
    ret = callback(finput0, finput1, foutput, params);
    csi_tensor_data_convert(output, foutput);
    csi_ref_tensor_transform_free_f32(finput0);
    csi_ref_tensor_transform_free_f32(finput1);
    csi_ref_tensor_transform_free_f32(foutput);
    return ret;
}

int csi_ref_conv_callback_base(struct csi_tensor *input,
                               struct csi_tensor *output,
                               struct csi_tensor *kernel,
                               struct csi_tensor *bias,
                               void *params,
                               void *cb)
{
    int (*callback)() = cb;
    struct csi_tensor *float_input = csi_ref_tensor_transform_f32(input);
    struct csi_tensor *float_kernel = csi_ref_tensor_transform_f32(kernel);
    struct csi_tensor *float_bias = csi_ref_tensor_transform_f32(bias);
    struct csi_tensor *float_output = csi_ref_tensor_transform_f32(output);
    int ret = callback(float_input, float_output, float_kernel, float_bias, params);
    csi_tensor_data_convert(output, float_output);
    csi_ref_tensor_transform_free_f32(float_input);
    csi_ref_tensor_transform_free_f32(float_output);
    csi_ref_tensor_transform_free_f32(float_kernel);
    csi_ref_tensor_transform_free_f32(float_bias);
    return ret;
}

uint8_t *csi_ref_f32_to_input_dtype(uint32_t index, float *data, struct csi_session *sess)
{
    struct csi_tensor *ftmp = csi_alloc_tensor(NULL);
    csi_tensor_copy(ftmp, sess->input[index]);
    ftmp->data = data;
    ftmp->dtype = CSINN_DTYPE_FLOAT32;
    struct csi_tensor *ret = csi_alloc_tensor(NULL);
    csi_tensor_copy(ret, sess->input[index]);
    ret->data = csi_mem_alloc(csi_tensor_byte_size(ret));
    csi_tensor_data_convert(ret, ftmp);
    uint8_t *ret_data = ret->data;
    csi_free_tensor(ret);
    csi_free_tensor(ftmp);
    return ret_data;
}

int csi_ref_broadcast_to_shape(struct csi_tensor *input,
                               struct csi_tensor *output,
                               int32_t *shape,
                               int32_t shape_count)
{
    int ret;
    if (input->dtype < CSINN_DTYPE_FLOAT16){
        ret = csi_ref_broadcast_to_shape_quant(input, output, shape, shape_count);
    } else {
        ret = csi_ref_broadcast_to_shape_f32(input, output, shape, shape_count);
    }
    return ret;
}


int csi_ref_broadcast_to_shape_f32(struct csi_tensor *input,
                                   struct csi_tensor *output,
                                   int32_t *shape,
                                   int32_t shape_count)
{
    float *input_data = (float *)input->data;
    float *output_data = (float *)output->data;
    int32_t *target_shape = shape;
    int32_t *in_shape = input->dim;
    int32_t in_shape_rank = input->dim_count;
    int32_t target_shape_rank = shape_count;

    // check for broadcast rule
    if (target_shape_rank < in_shape_rank){return CSINN_FALSE;}
    for (int i = 0; i < in_shape_rank; i++){
        if ((in_shape[in_shape_rank - i -1] != target_shape[target_shape_rank - i - 1]) &&
           (in_shape[in_shape_rank - i - 1] != 1)){
               return CSINN_FALSE;
           }
    }

    // full in_shape
    int32_t new_shape[target_shape_rank];
    memcpy(new_shape, in_shape, in_shape_rank*4);
    if (target_shape_rank > in_shape_rank){
        for (int i = 0; i < target_shape_rank - in_shape_rank; i++){
            new_shape[i] = 1;
        }
        for (int i = 0; i < in_shape_rank; i++){
            int index = target_shape_rank - in_shape_rank + i;
            new_shape[index] = in_shape[i];
        }
    }
    in_shape = new_shape;

    int data_size = csi_tensor_size(input);
    int out_size = csi_tensor_size(output);
    float *output_data_t = csi_mem_alloc(out_size * 4);
    memcpy(output_data_t, input_data, data_size * 4);
    memcpy(output_data, input_data, data_size * 4);

    for(int i=0; i< target_shape_rank; i++){

        int origin_dim = in_shape[target_shape_rank - i -1];
        int target_dim = target_shape[target_shape_rank - i -1];

        if (origin_dim != target_dim){
            data_size = 1;
            for (int i=0; i< target_shape_rank; i++){
                data_size *= in_shape[i];
            }
            int inner_size = 1;
            for (int j = target_shape_rank - i - 1; j < target_shape_rank; j++){
                inner_size *= in_shape[j];
            }
            int target_inner_size = 1;
            for (int j = target_shape_rank - i - 1; j < target_shape_rank; j++){
                target_inner_size *= target_shape[j];
            }

            float tmp_arr[inner_size];
            for (int idx = 0; idx < data_size; idx++){
                // at first output equal to input, then tmp data be saved in output
                tmp_arr[idx % inner_size] = output_data_t[idx];
                if ((idx + 1) % inner_size == 0){
                    int out_index = ((idx + 1) / inner_size - 1) * target_inner_size;
                    for (int cp_num = 0; cp_num < target_dim; cp_num++){
                        for (int elem_id =0; elem_id < inner_size; elem_id++){
                            output_data[out_index + cp_num * inner_size + elem_id] = tmp_arr[elem_id];
                        }
                    }
                }
            }
            in_shape[target_shape_rank - i - 1] = target_shape[target_shape_rank - i - 1];
            memcpy(output_data_t, output_data, out_size * 4);
        }
    }
    csi_mem_free(output_data_t);
    return CSINN_TRUE;
}

int csi_ref_broadcast_to_shape_quant(struct csi_tensor *input,
                                     struct csi_tensor *output,
                                     int32_t *shape,
                                     int32_t shape_count)
{
    struct csi_tensor *finput = csi_ref_tensor_transform_f32(input);
    struct csi_tensor *foutput = csi_ref_tensor_transform_f32(output);
    int ret = csi_ref_broadcast_to_shape_f32(finput, foutput, shape, shape_count);
    csi_tensor_data_convert(output, foutput);
    csi_ref_tensor_transform_free_f32(finput);
    csi_ref_tensor_transform_free_f32(foutput);
    return ret;
}