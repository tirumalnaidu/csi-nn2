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
    init_testsuite("Testing function of roialign f32.\n");

    struct csi_tensor *reference = csi_alloc_tensor(NULL);
    struct csi_tensor *input0  = csi_alloc_tensor(NULL);
    struct csi_tensor *input1  = csi_alloc_tensor(NULL);
    struct csi_tensor *output = csi_alloc_tensor(NULL);
    struct roi_align_params params;
    int in0_size = 0, in1_size = 0, out_size = 0;

    int *buffer = read_input_data_f32(argv[1]);

    input0->dim[0] = buffer[0];          // batch
    input0->dim[1] = buffer[1];          // channel
    input0->dim[2] = buffer[2];          // height
    input0->dim[3] = buffer[3];          // width
    input0->dim_count = 4;
    in0_size = input0->dim[0] * input0->dim[1] * input0->dim[2] * input0->dim[3];
    input0->dtype = CSINN_DTYPE_FLOAT32;
    input0->name = "input0";
    input0->data = (float *)(buffer + 11);


    input1->dim[0] = buffer[6];
    input1->dim[1] = 5;
    input1->dim_count = 2;
    in1_size = input1->dim[0] * input1->dim[1];
    input1->dtype = CSINN_DTYPE_FLOAT32;
    input1->name = "input1";
    input1->data = (float *)(buffer + 11 + in0_size);


    output->dim[0] = input1->dim[0];    // num_rois
    output->dim[1] = input0->dim[1];    // channel
    output->dim[2] = buffer[4];
    output->dim[3] = buffer[5];
    output->dim_count = 4;
    out_size = output->dim[0] * output->dim[1] * output->dim[2] * output->dim[3];
    reference->data = (float *)(buffer + 11 + in0_size + in1_size);
    output->data = (float *)malloc(out_size * sizeof(float));
    output->name = "output";
    output->dtype = CSINN_DTYPE_FLOAT32;
    float difference = argc > 2 ? atof(argv[2]) : 0.9;

    params.spatial_scale = *((float *)buffer + 9);
    params.sample_ratio = *((int32_t *)buffer + 10);
    params.pooled_size_h = buffer[7];
    params.pooled_size_w = buffer[8];
    params.base.api = CSINN_API;
    params.base.name = "params";
    params.base.layout = CSINN_LAYOUT_NCHW;
    params.base.run_mode = CSINN_RM_LAYER;


    if (csi_roi_align_init(input0, input1, output, &params) == CSINN_TRUE) {
        csi_roi_align(input0, input1, output, &params);
    }

    result_verify_f32(reference->data, output->data, input0->data, difference, out_size, false);

    free(buffer);
    free(output->data);
    return done_testing();
}