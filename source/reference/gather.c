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

int csi_ref_gather_f32(struct csi_tensor *input,
                       struct csi_tensor *indices,
                       struct csi_tensor *output,
                       struct gather_params *params)
{
    float *input_data = (float *)input->data;
    float *output_data = (float *)output->data;
    int32_t *indices_data = (int32_t *)indices->data;

    int inner_size = 1;
    for (int i = params->axis + 1; i < input->dim_count; i++) {
        inner_size *= input->dim[i];
    }
    int outer_size = 1;
    for (int i = 0; i < params->axis; i++) {
        outer_size *= input->dim[i];
    }
    int indices_size = 1;
    for (int i = 0; i < indices->dim_count; i++) {
        indices_size *= indices->dim[i];
    }

    for (int i = 0; i < outer_size; i++) {

        for (int j = 0; j < indices_size; j++) {
            if (indices_data[j] < input->dim[params->axis]) {
                memcpy(output_data, input_data + indices_data[j] * inner_size, inner_size * sizeof(float));
            } else {
                memset(output_data, 0, inner_size * sizeof(float));
            }
            output_data += inner_size;
        }
        input_data += inner_size * input->dim[params->axis];
    }
    return CSINN_TRUE;
}

int csi_ref_gather_quant(struct csi_tensor *input,
                         struct csi_tensor *indices,
                         struct csi_tensor *output,
                         struct gather_params *params)
{
    int ret;
    struct csi_tensor *finput = csi_ref_tensor_transform_f32(input);
    struct csi_tensor *foutput = csi_ref_tensor_transform_f32(output);
    ret = csi_ref_gather_f32(finput, indices, foutput, params);
    csi_tensor_data_convert(output, foutput);
    csi_ref_tensor_transform_free_f32(finput);
    csi_ref_tensor_transform_free_f32(foutput);
}
