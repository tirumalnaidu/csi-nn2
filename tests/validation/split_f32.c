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
    init_testsuite("Testing function of split f32.\n");

    int *buffer = read_input_data_f32(argv[1]);
    int axis = buffer[4];
    int output_cnt = buffer[5];
    int32_t *split_index = (int32_t *)malloc(output_cnt * sizeof(int32_t));
    for(int i = 0; i < output_cnt; i++) {
        split_index[i] = buffer[axis] / output_cnt;
    }

    struct csi_tensor *reference[output_cnt];
    for(int i = 0; i < output_cnt; i++) {
        reference[i] = csi_alloc_tensor(NULL);
    }
    int in_size = 0;
    int out_size[output_cnt];
    int acc_out_size = 0;


    struct csi_tensor *input = csi_alloc_tensor(NULL);
    input->dim[0] = buffer[0];          // batch
    input->dim[1] = buffer[1];          // channel
    input->dim[2] = buffer[2];          // height
    input->dim[3] = buffer[3];          // width
    input->dim_count = 4;
    in_size = input->dim[0] * input->dim[1] * input->dim[2] * input->dim[3];

    input->data  = (float *)(buffer + 6);
    input->dtype = CSINN_DTYPE_FLOAT32;

    struct csi_tensor *output[output_cnt];
    for(int i = 0; i < output_cnt; i++) {
        output[i]  = csi_alloc_tensor(NULL);
        for(int j = 0; j < 4; j++) {
            if(j == axis) {
                output[i]->dim[j] = split_index[i];
            } else {
                output[i]->dim[j] = input->dim[j];
            }
        }
        output[i]->dim_count = 4;
        out_size[i] = output[i]->dim[0] * output[i]->dim[1] * output[i]->dim[2] * output[i]->dim[3];

        reference[i]->data = (float *)(buffer + 6 + in_size + acc_out_size);
        output[i]->data     = malloc(out_size[i] * sizeof(float));
        acc_out_size += out_size[i];
        output[i]->is_const = 0;
    }

    struct split_params params;
    params.base.api = CSINN_API;
    params.base.layout = CSINN_LAYOUT_NCHW;
    params.base.run_mode = CSINN_RM_LAYER;
    params.axis = axis;
    params.output_num = output_cnt;

    int temp = 0;
    for(int i = 0; i < output_cnt; i++) {
        temp += split_index[i];
        split_index[i] = temp;
        printf("%d\n", split_index[i]);
    }
    params.split_index = split_index;


    if (csi_split_init(input, (struct csi_tensor **)&output, &params) == CSINN_TRUE) {
        csi_split(input, (struct csi_tensor **)&output, &params);
    }

    /* verify result */
    float difference = argc > 2 ? atof(argv[2]) : 1e-4;
    for(int i = 0; i < output_cnt; i++) {
        result_verify_f32(reference[i]->data, output[i]->data, input->data, difference, out_size[i], false);
    }


    /* free alloced memory */
    free(buffer);
    free(split_index);
    for(int i = 0; i < output_cnt; i++) {
        free(output[i]->data);
    }
    return done_testing();
}
