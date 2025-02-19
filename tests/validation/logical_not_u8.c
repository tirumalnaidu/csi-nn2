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

int main(int argc, char** argv)
{
    init_testsuite("Testing function of logical not u8.\n");

    struct csi_tensor *input0 = csi_alloc_tensor(NULL);
    struct csi_tensor *output = csi_alloc_tensor(NULL);
    struct csi_tensor *reference = csi_alloc_tensor(NULL);
    struct siso_params params;
    int in_size0;
    int zp, quantized_multiplier, shift;
    float scale, min_value, max_value;
    float max_error = 0.0f;

    int *buffer = read_input_data_f32(argv[1]);

    input0->dim[0] = buffer[0];          
    input0->dim[1] = buffer[1];          
    input0->dim[2] = buffer[2];          
    input0->dim[3] = buffer[3];          

    output->dim[0] = input0->dim[0];
    output->dim[1] = input0->dim[1];
    output->dim[2] = input0->dim[2];
    output->dim[3] = input0->dim[3];

    in_size0 = input0->dim[0] * input0->dim[1] * input0->dim[2] * input0->dim[3];
    input0->dim_count = 4;
    output->dim_count = 4;
    input0->dtype = CSINN_DTYPE_UINT8;
    input0->layout = CSINN_LAYOUT_NCHW;
    input0->is_const = 0;
    input0->quant_channel = 1;

    output->dtype = CSINN_DTYPE_UINT8;
    output->layout = CSINN_LAYOUT_NCHW;
    output->is_const = 0;
    output->quant_channel = 1;

    params.base.api = CSINN_API;
    params.base.run_mode = CSINN_RM_LAYER;
    params.base.layout = CSINN_LAYOUT_NCHW;

    float *src_in   = (float *)(buffer + 4);
    float *ref      = (float *)(buffer + 4 + in_size0);
    uint8_t *src_tmp = malloc(in_size0 * sizeof(char));

    input0->data = src_in;
    get_quant_info(input0);

    for(int i = 0; i < in_size0; i++) {
        src_tmp[i] = csi_ref_quantize_f32_to_u8(src_in[i], input0->qinfo);
    }

    /* compute the max quantize error */
    for(int i = 0; i < in_size0; i++) {
        float error1;
        float output_tmp  = csi_ref_dequantize_u8_to_f32(src_tmp[i], input0->qinfo);
        if(isinf(src_in[i]) || isnan(src_in[i])){
            continue;
        } else {
            error1 = fabs(src_in[i] -output_tmp);
            if(error1 > 1e-6) {
                error1 = fabs(src_in[i] - output_tmp)/fabs(src_in[i] + 1e-9);
            }
        }
        if(error1 > max_error) {
            max_error = error1;
        }
    }

    output->data = ref;
    get_quant_info(output);

    input0->data     = src_tmp;
    reference->data = ref;
    output->data    = malloc(in_size0 * sizeof(char));

    float difference = argc > 2 ? atof(argv[2]) : 0.9;

    if (csi_logical_not_init(input0, output, &params) == CSINN_TRUE) {
        csi_logical_not(input0, output, &params);
    }

    result_verify_8(reference->data, output, input0->data, difference, in_size0, false);

    free(buffer);
    free(src_tmp);
    free(output->data);
    return done_testing();
}
