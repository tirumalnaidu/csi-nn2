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

#include "test_utils.h"
#include "csi_nn.h"
#include "math_snr.h"

void op_test_run_light(struct csi_tensor *input, struct csi_tensor *output,
                       struct softmax_params *params, struct csi_session *sess,
                       struct csi_tensor *real_input, float *output_data, float diff)
{
    csi_session_init(sess);
    csi_set_input_number(2, sess);
    csi_set_output_number(1, sess);

    struct csi_tensor *zero = csi_alloc_tensor(sess);
    struct csi_tensor *add_output = csi_alloc_tensor(sess);
    struct diso_params *add_params = csi_alloc_params(sizeof(struct diso_params), sess);
    add_params->base.name = "add_params";
    add_params->base.layout = CSINN_LAYOUT_NCHW;
    add_params->base.run_mode = CSINN_RM_NPU_GRAPH;
    add_params->base.api = CSINN_LIGHT;

    csi_tensor_copy(zero, input);
    zero->qinfo->scale = 1;
    zero->qinfo->zero_point = 0;

    csi_tensor_copy(add_output, input);

    csi_add_init(input, zero, add_output, add_params);
    csi_softmax_init(add_output, output, params);

    csi_set_tensor_entry(input, sess);
    csi_set_input(0, input, sess);
    csi_set_tensor_entry(zero, sess);
    csi_set_input(1, zero, sess);

    csi_add(input, zero, add_output, add_params);
    csi_softmax(add_output, output, params);

    csi_set_output(0, output, sess);
    csi_session_setup(sess);

    csi_update_input(0, real_input, sess);
    zero->data = calloc(1, csi_tensor_byte_size(zero));
    csi_update_input(1, zero, sess);
    csi_session_run(sess);
    csi_get_output(0, output, sess);

    struct csi_tensor *foutput = csi_ref_tensor_transform_f32(output);
    result_verify_f32(output_data, foutput->data, input->data, diff, csi_tensor_size(output),
                      false);

    free_input(real_input);
    csi_ref_tensor_transform_free_f32(foutput);
    csi_session_deinit(sess);
    csi_free_session(sess);
}

void test_i8_sym(struct csi_tensor *input, struct csi_tensor *output, struct softmax_params *params,
                 float difference)
{
    printf("test softmax i8 sym\n");
    struct csi_session *sess = csi_alloc_session();
    sess->base_api = CSINN_LIGHT;
    sess->base_quant_type = CSINN_QUANT_INT8_SYM;
    // sess->debug_level = CSI_DEBUG_LEVEL_INFO;
    enum csinn_dtype_enum test_dtype = CSINN_DTYPE_FLOAT32;

    struct csi_tensor *qinput = convert_f32_input(input, test_dtype, sess);
    struct csi_tensor *qoutput = convert_f32_input(output, test_dtype, sess);
    struct csi_tensor *real_input = convert_f32_input(input, CSINN_DTYPE_INT8, sess);

    op_test_run_light(qinput, qoutput, params, sess, real_input, output->data, difference);
}

void test_i16_sym(struct csi_tensor *input, struct csi_tensor *output, struct softmax_params *params,
                 float difference)
{
    printf("test softmax i16 sym\n");
    struct csi_session *sess = csi_alloc_session();
    sess->base_api = CSINN_LIGHT;
    sess->base_quant_type = CSINN_QUANT_INT16_SYM;
    // sess->debug_level = CSI_DEBUG_LEVEL_INFO;
    enum csinn_dtype_enum test_dtype = CSINN_DTYPE_FLOAT32;

    struct csi_tensor *qinput = convert_f32_input(input, test_dtype, sess);
    struct csi_tensor *qoutput = convert_f32_input(output, test_dtype, sess);
    struct csi_tensor *real_input = convert_f32_input(input, CSINN_DTYPE_INT16, sess);

    op_test_run_light(qinput, qoutput, params, sess, real_input, output->data, difference);
}

void test_i8_asym(struct csi_tensor *input, struct csi_tensor *output, struct softmax_params *params,
                 float difference)
{
    printf("test softmax i8 asym\n");
    struct csi_session *sess = csi_alloc_session();
    sess->base_api = CSINN_LIGHT;
    sess->base_quant_type = CSINN_QUANT_INT8_ASYM;
    // sess->debug_level = CSI_DEBUG_LEVEL_INFO;
    enum csinn_dtype_enum test_dtype = CSINN_DTYPE_INT8;

    struct csi_tensor *qinput = convert_f32_input(input, test_dtype, sess);
    struct csi_tensor *qoutput = convert_f32_input(output, test_dtype, sess);
    struct csi_tensor *real_input = convert_f32_input(input, test_dtype, sess);

    op_test_run_light(qinput, qoutput, params, sess, real_input, output->data, difference);
}

void test_u8_asym(struct csi_tensor *input, struct csi_tensor *output, struct softmax_params *params,
                 float difference)
{
    printf("test softmax u8 asym\n");
    struct csi_session *sess = csi_alloc_session();
    sess->base_api = CSINN_LIGHT;
    sess->base_quant_type = CSINN_QUANT_UINT8_ASYM;
    // sess->debug_level = CSI_DEBUG_LEVEL_INFO;
    enum csinn_dtype_enum test_dtype = CSINN_DTYPE_UINT8;

    struct csi_tensor *qinput = convert_f32_input(input, test_dtype, sess);
    struct csi_tensor *qoutput = convert_f32_input(output, test_dtype, sess);
    struct csi_tensor *real_input = convert_f32_input(input, test_dtype, sess);

    op_test_run_light(qinput, qoutput, params, sess, real_input, output->data, difference);
}

void test_softmax(struct csi_tensor *input, struct csi_tensor *output,
                  struct softmax_params *params, float difference)
{
    params->base.api = CSINN_LIGHT;

    test_i8_sym(input, output, params, difference);
    test_i16_sym(input, output, params, difference);
    test_i8_asym(input, output, params, difference);
    test_u8_asym(input, output, params, difference);
}
