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
#include "./valid_data/q7_conv_basic.dat"


extern void verify_conv2d_q7(void *input_data,
                             void *kernel_data,
                             void *bias_data,
                             void *ref_data,
                             uint16_t batch,
                             uint16_t in_h,
                             uint16_t in_w,
                             uint16_t in_c,
                             uint16_t out_h,
                             uint16_t out_w,
                             uint16_t out_c,
                             uint16_t kernel_h,
                             uint16_t kernel_w,
                             uint16_t stride_h,
                             uint16_t stride_w,
                             uint16_t pad_x,
                             uint16_t pad_y,
                             uint16_t bias_shift,
                             uint16_t out_shift,
                             float difference);


int main(int argc, char** argv)
{
    init_testsuite("First testing function of convolution nonsquare q7 for xt800.\n");

    verify_conv2d_q7(q7_conv_input_0, q7_conv_weight_0, q7_conv_bias_0, q7_conv_result_0,
                     1, 32, 32, 16, 30, 30, 32, 3, 3, 1, 1, 0, 0, 0, 11, 0.0f);

    verify_conv2d_q7(q7_conv_input_0, q7_conv_weight_0, q7_conv_bias_0, q7_conv_result_1,
                     1, 32, 32, 16, 32, 32, 32, 3, 3, 1, 1, 1, 1, 0, 12, 0.0f);

    verify_conv2d_q7(q7_conv_input_0, q7_conv_weight_0, q7_conv_bias_0, q7_conv_result_16,
                     1, 31, 31, 12, 29, 29, 30, 3, 3, 1, 1, 0, 0, 0, 11, 0.0f);

    verify_conv2d_q7(q7_conv_input_0, q7_conv_weight_0, q7_conv_bias_0, q7_conv_result_17,
                     1, 31, 31, 12, 31, 31, 30, 3, 3, 1, 1, 1, 1, 0, 12, 0.0f);
}

