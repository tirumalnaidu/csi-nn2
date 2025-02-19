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

#include "csi_c906.h"

/*
    pad_left = pad_top = 0
    pad_right = 0 or 1
    pad_down = 0 or 1
*/
static int avgpool2x2s2(struct csi_tensor *input,
                        struct csi_tensor *output,
                        struct pool_params *params)
{
    float *input_data  = (float *)input->data;
    float *output_data = (float *)output->data;

    int batch = input->dim[0];
    int in_c  = input->dim[1];
    int in_h  = input->dim[2];
    int in_w  = input->dim[3];
    int input_size = in_c * in_h * in_w;

    int out_h = output->dim[2];
    int out_w = output->dim[3];
    int out_hw = out_h * out_w;
    int output_size = in_c * out_h * out_w;

    int extend_h = 0;
    int extend_w = 0;

    if (in_h % 2 == 1 && params->pad_down == 1) {
        extend_h = 1;
        out_h--;
    }
    if (in_w % 2 == 1 && params->pad_right == 1) {
        extend_w = 1;
        out_w--;
    }

    const int tailstep = in_w - 2 * out_w + in_w;
    int out_w4 = out_w >> 2;
    int remain_w = in_w - 2 * out_w;
    float ratio = 0.25f;

    for(int b = 0; b < batch; b++) {

        for(int c = 0; c < in_c; c++) {

            const float *line0 = input_data + c * in_h * in_w;
            const float *line1 = line0 + in_w;
            float *outptr = output_data + c * out_hw;

            for(int h = 0; h < out_h; h++) {
                ratio = 0.25f;
#if __riscv_vector == 128
                if(out_w4 > 0) {
                    // execution delay cycles for vlseg2e: >= 2 + 2
                    asm volatile(
                        "vsetvli        zero, zero, e32, m1\n\t"
                        "mv             t0, %3\n\t"
                    "1:\n\t"
                        "vlseg2e.v      v0, (%0)\n\t"   // v0[0..3] = line0[0,2.4.6]   v1[0..3] = line0[1,3,5,7]
                        "vfadd.vv       v4, v0, v1\n\t"
                        "addi           %0, %0, 32\n\t" // line0 += 8

                        "vlseg2e.v      v2, (%1)\n\t"
                        "vfadd.vv       v5, v2, v3\n\t"
                        "addi           %1, %1, 32\n\t" // line1 += 8

                        "vfadd.vv       v6, v4, v5\n\t"
                        "vfmul.vf       v7, v6, %8\n\t"
                        "vsw.v          v7, (%2)\n\t"
                        "addi           %2, %2, 16\n\t" // outptr += 4

                        "addi           t0, t0, -1\n\t"
                        "bnez           t0, 1b\n\t"

                        :"=r"(line0),   // %0
                        "=r"(line1),    // %1
                        "=r"(outptr),   // %2
                        "=r"(out_w4)    // %3
                        :"0"(line0),
                        "1"(line1),
                        "2"(outptr),
                        "3"(out_w4),
                        "f"(ratio)      // %8
                        :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "t0"
                    );
                }
#else
                for(int w4 = 0; w4 < out_w4; w4++) {
                    float sum00 = line0[0] + line0[1];
                    float sum01 = line0[2] + line0[3];
                    float sum02 = line0[4] + line0[5];
                    float sum03 = line0[6] + line0[7];

                    float sum10 = line1[0] + line1[1];
                    float sum11 = line1[2] + line1[3];
                    float sum12 = line1[4] + line1[5];
                    float sum13 = line1[6] + line1[7];

                    outptr[0] = (sum00 + sum10) * ratio;
                    outptr[1] = (sum01 + sum11) * ratio;
                    outptr[2] = (sum02 + sum12) * ratio;
                    outptr[3] = (sum03 + sum13) * ratio;

                    line0 += 8;
                    line1 += 8;
                    outptr += 4;
                }
#endif  // __riscv_vector
                for(int i = out_w4 * 4; i < out_w; i++) {

                    outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;

                    line0 += 2;
                    line1 += 2;
                    outptr++;
                }
                if(extend_w) {
                    ratio = (params->count_include_pad) ? 0.25f : 0.5f;
                    outptr[0] = (line0[0] + line1[0]) * ratio;
                    outptr++;
                }
                line0 += remain_w + in_w;
                line1 += remain_w + in_w;
            }
            if(extend_h) {
                ratio = (params->count_include_pad) ? 0.25f : 0.5f;
#if __riscv_vector == 128
                if(out_w4 > 0) {
                    asm volatile(
                        "vsetvli        zero, zero, e32, m1\n\t"
                        "mv             t0, %2\n\t"
                    "1:\n\t"
                        "vlseg2e.v      v0, (%0)\n\t"
                        "vfadd.vv       v2, v0, v1\n\t"
                        "addi           %0, %0, 32\n\t"

                        "vfmul.vf       v3, v2, %6\n\t"
                        "vsw.v          v3, (%1)\n\t"
                        "addi           %1, %1, 16\n\t"

                        "addi           t0, t0, -1\n\t"
                        "bnez           t0, 1b\n\t"

                        :"=r"(line0),   // %0
                        "=r"(outptr),   // %1
                        "=r"(out_w4)    // %2
                        :"0"(line0),
                        "1"(outptr),
                        "2"(out_w4),
                        "f"(ratio)      // %6
                        :"cc", "memory", "v0", "v1", "v2", "v3", "t0"
                    );
                }
#else
                for(int w4 = 0; w4 < out_w4; w4++) {
                    outptr[0] = (line0[0] + line0[1]) * ratio;
                    outptr[1] = (line0[2] + line0[3]) * ratio;
                    outptr[2] = (line0[4] + line0[5]) * ratio;
                    outptr[3] = (line0[6] + line0[7]) * ratio;

                    line0 += 8;
                    outptr += 4;
                }
#endif  //__riscv_vector
                for(int i = out_w4 * 4; i < out_w; i++) {
                    outptr[0] = (line0[0] + line0[1]) * ratio;
                    line0 += 2;
                    outptr++;
                }
                if(extend_w) {
                    ratio = (params->count_include_pad) ? 0.25f : 1.0f;
                    outptr[0] = line0[0] * ratio;
                    outptr++;
                }
            }
        }
        input_data  += input_size;
        output_data += output_size;
    }
    return CSINN_TRUE;
}


static int avgpool2x2s2_fp16(struct csi_tensor *input,
                             struct csi_tensor *output,
                             struct pool_params *params)
{
    __fp16 *input_data  = (__fp16 *)input->data;
    __fp16 *output_data = (__fp16 *)output->data;

    int batch = input->dim[0];
    int in_c  = input->dim[1];
    int in_h  = input->dim[2];
    int in_w  = input->dim[3];
    int input_size = in_c * in_h * in_w;

    int out_h = output->dim[2];
    int out_w = output->dim[3];
    int out_hw = out_h * out_w;
    int output_size = in_c * out_h * out_w;

    int extend_h = 0;
    int extend_w = 0;

    if (in_h % 2 == 1 && params->pad_down == 1) {
        extend_h = 1;
        out_h--;
    }
    if (in_w % 2 == 1 && params->pad_right == 1) {
        extend_w = 1;
        out_w--;
    }

    const int tailstep = in_w - 2 * out_w + in_w;
    int out_w8 = out_w >> 3;
    int remain_w = in_w - 2 * out_w;
    __fp16 ratio = 0.25f;

    for(int b = 0; b < batch; b++) {

        for(int c = 0; c < in_c; c++) {

            const __fp16 *line0 = input_data + c * in_h * in_w;
            const __fp16 *line1 = line0 + in_w;
            __fp16 *outptr = output_data + c * out_hw;

            for(int h = 0; h < out_h; h++) {
                ratio = 0.25f;

                asm volatile(
                    "srai           t0, %3, 3\n\t"  // t0 = out_w >> 3 (out_w8)
                    "beqz           t0, 2f\n\t"
                    "vsetvli        zero, zero, e16, m1\n\t"

                "1:\n\t"
                    "vlseg2e.v      v0, (%0)\n\t"   // v0[0..7] = line0[0,2,4,6,8,10,12,14]   v1[0..7] = line0[1,3,5,7,9,11,13,15]
                    "vfadd.vv       v4, v0, v1\n\t"
                    "addi           %0, %0, 32\n\t" // line0 += 16

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v5, v2, v3\n\t"
                    "addi           %1, %1, 32\n\t" // line1 += 16

                    "vfadd.vv       v6, v4, v5\n\t"
                    "vfmul.vf       v7, v6, %8\n\t"
                    "vse.v          v7, (%2)\n\t"
                    "addi           %2, %2, 16\n\t" // outptr += 8

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                "2:\n\t"
                    "andi           t0, %3, 7\n\t"
                    "beqz           t0, 3f\n\t"

                    // out_w_tail
                    "vsetvli        zero, t0, e16, m1\n\t"
                    "slli           t1, t0, 1\n\t"
                    "slli           t2, t0, 2\n\t"

                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v4, v0, v1\n\t"
                    "add            %0, %0, t2\n\t"

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v5, v2, v3\n\t"
                    "add            %1, %1, t2\n\t"

                    "vfadd.vv       v6, v4, v5\n\t"
                    "vfmul.vf       v7, v6, %8\n\t"
                    "vse.v          v7, (%2)\n\t"
                    "add            %2, %2, t1\n\t"

                "3:\n\t"

                    :"=r"(line0),   // %0
                    "=r"(line1),    // %1
                    "=r"(outptr),   // %2
                    "=r"(out_w)     // %3
                    :"0"(line0),
                    "1"(line1),
                    "2"(outptr),
                    "3"(out_w),
                    "f"(ratio)      // %8
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "t0", "t1", "t2"
                );

                if(extend_w) {
                    ratio = (params->count_include_pad) ? 0.25f : 0.5f;
                    outptr[0] = (line0[0] + line1[0]) * ratio;
                    outptr++;
                }
                line0 += remain_w + in_w;
                line1 += remain_w + in_w;
            }

            if(extend_h) {
                ratio = (params->count_include_pad) ? 0.25f : 0.5f;

                asm volatile(
                    "srai           t0, %2, 3\n\t"  // t0 = out_w >> 3 (out_w8)
                    "beqz           t0, 2f\n\t"
                    "vsetvli        zero, zero, e16, m1\n\t"

                "1:\n\t"
                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v2, v0, v1\n\t"
                    "addi           %0, %0, 32\n\t"

                    "vfmul.vf       v3, v2, %6\n\t"
                    "vse.v          v3, (%1)\n\t"
                    "addi           %1, %1, 16\n\t"

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                "2:\n\t"
                    "andi           t0, %2, 7\n\t"
                    "beqz           t0, 3f\n\t"

                    // out_w_tail
                    "vsetvli        zero, t0, e16, m1\n\t"
                    "slli           t1, t0, 1\n\t"
                    "slli           t2, t0, 2\n\t"

                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v2, v0, v1\n\t"
                    "add            %0, %0, t2\n\t"

                    "vfmul.vf       v3, v2, %6\n\t"
                    "vse.v          v3, (%1)\n\t"
                    "add            %1, %1, t1\n\t"

                "3:\n\t"

                    :"=r"(line0),   // %0
                    "=r"(outptr),   // %1
                    "=r"(out_w)     // %2
                    :"0"(line0),
                    "1"(outptr),
                    "2"(out_w),
                    "f"(ratio)      // %6
                    :"cc", "memory", "v0", "v1", "v2", "v3", "t0", "t1", "t2"
                );

                if(extend_w) {
                    ratio = (params->count_include_pad) ? 0.25f : 1.0f;
                    outptr[0] = line0[0] * ratio;
                    outptr++;
                }
            }
        }
        input_data  += input_size;
        output_data += output_size;
    }
    return CSINN_TRUE;
}

/*
    pad_left = pad_top = 1
    pad_right = 0 or 1
    pad_down = 0 or 1
*/
static int avgpool2x2s2_p1(struct csi_tensor *input,
                           struct csi_tensor *output,
                           struct pool_params *params)
{
    float *input_data  = (float *)input->data;
    float *output_data = (float *)output->data;

    int batch = input->dim[0];
    int in_c  = input->dim[1];
    int in_h  = input->dim[2];
    int in_w  = input->dim[3];
    int input_size = in_c * in_h * in_w;

    int out_h = output->dim[2];
    int out_w = output->dim[3];
    int out_hw = out_h * out_w;
    int output_size = in_c * out_h * out_w;

    int extend_h = 0;
    int extend_w = 0;

    if (in_h % 2 == 0 && params->pad_down == 1) {
        extend_h = 1;
        out_h--;
    }
    if (in_w % 2 == 0 && params->pad_right == 1) {
        extend_w = 1;
        out_w--;
    }

    int out_w4 = (out_w - 1) >> 2;
    int remain_w = in_w - 2 * out_w + 1;
    float ratio = 0.25f;

    for(int b = 0; b < batch; b++) {

        for(int c = 0; c < in_c; c++) {

            const float *line00 = input_data + c * in_h * in_w;
            float *outptr = output_data + c * out_hw;

            // h top ---- w left
            ratio = (params->count_include_pad) ? 0.25f : 1.0f;
            outptr[0] = line00[0] * ratio;
            outptr++;
            line00++;
            // h top ---- w mid
            ratio = (params->count_include_pad) ? 0.25f : 0.5f;
#if __riscv_vector == 128
            if(out_w4 > 0) {
                asm volatile(
                    "vsetvli        zero, zero, e32, m1\n\t"
                    "mv             t0, %2\n\t"
                "1:\n\t"
                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v2, v0, v1\n\t"
                    "addi           %0, %0, 32\n\t"

                    "vfmul.vf       v3, v2, %6\n\t"
                    "vsw.v          v3, (%1)\n\t"
                    "addi           %1, %1, 16\n\t"

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                    :"=r"(line00),  // %0
                    "=r"(outptr),   // %1
                    "=r"(out_w4)    // %2
                    :"0"(line00),
                    "1"(outptr),
                    "2"(out_w4),
                    "f"(ratio)      // %6
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "t0"
                );
            }
#else
            for(int w4 = 0; w4 < out_w4; w4++) {
                outptr[0] = (line00[0] + line00[1]) * ratio;
                outptr[1] = (line00[2] + line00[3]) * ratio;
                outptr[2] = (line00[4] + line00[5]) * ratio;
                outptr[3] = (line00[6] + line00[7]) * ratio;

                line00 += 8;
                outptr += 4;
            }
#endif  // __riscv_vector
            for(int j = out_w4 * 4 + 1; j < out_w; j++) {
                outptr[0] = (line00[0] + line00[1]) * ratio;
                outptr++;
                line00 += 2;
            }
            // h top ---- w right
            ratio = (params->count_include_pad) ? 0.25f : 1.0f;
            if(extend_w) {
                outptr[0] = line00[0] * ratio;
                outptr++;
            }
            line00 += remain_w;

            // h mid
            const float *line0 = line00;
            const float *line1 = line0 + in_w;
            for(int h = 0; h < out_h - 1; h++) {
                // h mid ---- w left
                ratio = (params->count_include_pad) ? 0.25f : 0.5f;
                outptr[0] = (line0[0] + line1[0]) * ratio;
                outptr++;
                line0++;
                line1++;
                // h mid ---- w mid
                float ratio = 0.25f;
#if __riscv_vector == 128
                if(out_w4 > 0) {
                    // execution delay cycles for vlseg2e: >= 2 + 2
                    asm volatile(
                        "vsetvli        zero, zero, e32, m1\n\t"
                        "mv             t0, %3\n\t"
                    "1:\n\t"
                        "vlseg2e.v      v0, (%0)\n\t"
                        "vfadd.vv       v4, v0, v1\n\t"
                        "addi           %0, %0, 32\n\t"

                        "vlseg2e.v      v2, (%1)\n\t"
                        "vfadd.vv       v5, v2, v3\n\t"
                        "addi           %1, %1, 32\n\t"

                        "vfadd.vv       v6, v4, v5\n\t"
                        "vfmul.vf       v7, v6, %8\n\t"
                        "vsw.v          v7, (%2)\n\t"
                        "addi           %2, %2, 16\n\t"

                        "addi           t0, t0, -1\n\t"
                        "bnez           t0, 1b\n\t"

                        :"=r"(line0),   // %0
                        "=r"(line1),    // %1
                        "=r"(outptr),   // %2
                        "=r"(out_w4)    // %3
                        :"0"(line0),
                        "1"(line1),
                        "2"(outptr),
                        "3"(out_w4),
                        "f"(ratio)      // %8
                        :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "t0"
                    );
                }
#else
                for(int w5 = 0; w5 < out_w4; w5++) {
                    float sum00 = line0[0] + line0[1];
                    float sum01 = line0[2] + line0[3];
                    float sum02 = line0[4] + line0[5];
                    float sum03 = line0[6] + line0[7];

                    float sum10 = line1[0] + line1[1];
                    float sum11 = line1[2] + line1[3];
                    float sum12 = line1[4] + line1[5];
                    float sum13 = line1[6] + line1[7];

                    outptr[0] = (sum00 + sum10) * ratio;
                    outptr[1] = (sum01 + sum11) * ratio;
                    outptr[2] = (sum02 + sum12) * ratio;
                    outptr[3] = (sum03 + sum13) * ratio;

                    line0 += 8;
                    line1 += 8;
                    outptr += 4;
                }
#endif  // __riscv_vector
                for(int i = out_w4 * 4 + 1; i < out_w; i++) {
                    outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;

                    line0 += 2;
                    line1 += 2;
                    outptr++;
                }
                // h mid ---- w right
                ratio = (params->count_include_pad) ? 0.25f : 0.5f;
                if(extend_w) {
                    outptr[0] = (line0[0] + line1[0]) * ratio;
                    outptr++;
                }
                line0 += remain_w + in_w;
                line1 += remain_w + in_w;
            }
            // h bottom
            if(extend_h) {
                // h bottom ---- w left
                ratio = (params->count_include_pad) ? 0.25f : 1.0f;
                outptr[0] = line0[0] * ratio;
                outptr++;
                line0++;
                // h bottom ---- w mid
                ratio = (params->count_include_pad) ? 0.25f : 0.5f;
#if __riscv_vector == 128
                if(out_w4 > 0) {
                    asm volatile(
                        "vsetvli        zero, zero, e32, m1\n\t"
                        "mv             t0, %2\n\t"
                    "1:\n\t"
                        "vlseg2e.v      v0, (%0)\n\t"
                        "vfadd.vv       v2, v0, v1\n\t"
                        "addi           %0, %0, 32\n\t"

                        "vfmul.vf       v3, v2, %6\n\t"
                        "vsw.v          v3, (%1)\n\t"
                        "addi           %1, %1, 16\n\t"

                        "addi           t0, t0, -1\n\t"
                        "bnez           t0, 1b\n\t"

                        :"=r"(line0),   // %0
                        "=r"(outptr),   // %1
                        "=r"(out_w4)    // %2
                        :"0"(line0),
                        "1"(outptr),
                        "2"(out_w4),
                        "f"(ratio)      // %6
                        :"cc", "memory", "v0", "v1", "v2", "v3", "t0"
                    );
                }
#else
                for(int w4 = 0; w4 < out_w4; w4++) {
                    outptr[0] = (line0[0] + line0[1]) * ratio;
                    outptr[1] = (line0[2] + line0[3]) * ratio;
                    outptr[2] = (line0[4] + line0[5]) * ratio;
                    outptr[3] = (line0[6] + line0[7]) * ratio;

                    line0 += 8;
                    outptr += 4;
                }
#endif  // __riscv_vector
                for(int i = out_w4 * 4 + 1; i < out_w; i++) {
                    outptr[0] = (line0[0] + line0[1]) * ratio;
                    outptr++;
                    line0 += 2;
                }
                // h bottom ---- w right
                ratio = (params->count_include_pad) ? 0.25f : 1.0f;
                if(extend_w) {
                    outptr[0] = line0[0] * ratio;
                }
            }
        }
        input_data  += input_size;
        output_data += output_size;
    }
    return CSINN_TRUE;
}

static int avgpool2x2s2_p1_fp16(struct csi_tensor *input,
                                struct csi_tensor *output,
                                struct pool_params *params)
{
    __fp16 *input_data  = (__fp16 *)input->data;
    __fp16 *output_data = (__fp16 *)output->data;

    int batch = input->dim[0];
    int in_c  = input->dim[1];
    int in_h  = input->dim[2];
    int in_w  = input->dim[3];
    int input_size = in_c * in_h * in_w;

    int out_h = output->dim[2];
    int out_w = output->dim[3];
    int out_hw = out_h * out_w;
    int output_size = in_c * out_h * out_w;

    int extend_h = 0;
    int extend_w = 0;

    if (in_h % 2 == 0 && params->pad_down == 1) {
        extend_h = 1;
        out_h--;
    }
    if (in_w % 2 == 0 && params->pad_right == 1) {
        extend_w = 1;
        out_w--;
    }

    int out_w8 = (out_w - 1) >> 3;
    int remain_w = in_w - 2 * out_w + 1;
    __fp16 ratio = 0.25f;

    for(int b = 0; b < batch; b++) {

        for(int c = 0; c < in_c; c++) {

            const __fp16 *line00 = input_data + c * in_h * in_w;
            __fp16 *outptr = output_data + c * out_hw;

            // h top ---- w left
            ratio = (params->count_include_pad) ? 0.25f : 1.0f;
            outptr[0] = line00[0] * ratio;
            outptr++;
            line00++;
            // h top ---- w mid
            ratio = (params->count_include_pad) ? 0.25f : 0.5f;

            asm volatile(
                "addi           t3, %2, -1\n\t" // out_w --
                "srai           t0, t3, 3\n\t"  // t0 = (out_w - 1) >> 3 (out_w8)
                "beqz           t0, 2f\n\t"
                "vsetvli        zero, zero, e16, m1\n\t"

            "1:\n\t"
                "vlseg2e.v      v0, (%0)\n\t"
                "vfadd.vv       v2, v0, v1\n\t"
                "addi           %0, %0, 32\n\t"

                "vfmul.vf       v3, v2, %6\n\t"
                "vse.v          v3, (%1)\n\t"
                "addi           %1, %1, 16\n\t"

                "addi           t0, t0, -1\n\t"
                "bnez           t0, 1b\n\t"

            "2:\n\t"
                "andi           t0, t3, 7\n\t"
                "beqz           t0, 3f\n\t"

                // out_w_tail
                "vsetvli        zero, t0, e16, m1\n\t"
                "slli           t1, t0, 1\n\t"
                "slli           t2, t0, 2\n\t"

                "vlseg2e.v      v0, (%0)\n\t"
                "vfadd.vv       v2, v0, v1\n\t"
                "add            %0, %0, t2\n\t"

                "vfmul.vf       v3, v2, %6\n\t"
                "vse.v          v3, (%1)\n\t"
                "add            %1, %1, t1\n\t"

            "3:\n\t"

                :"=r"(line00),  // %0
                "=r"(outptr),   // %1
                "=r"(out_w)     // %2
                :"0"(line00),
                "1"(outptr),
                "2"(out_w),
                "f"(ratio)      // %6
                :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "t0", "t1", "t2", "t3"
            );

            // h top ---- w right
            ratio = (params->count_include_pad) ? 0.25f : 1.0f;
            if(extend_w) {
                outptr[0] = line00[0] * ratio;
                outptr++;
            }
            line00 += remain_w;

            // h mid
            const __fp16 *line0 = line00;
            const __fp16 *line1 = line0 + in_w;
            for(int h = 0; h < out_h - 1; h++) {
                // h mid ---- w left
                ratio = (params->count_include_pad) ? 0.25f : 0.5f;
                outptr[0] = (line0[0] + line1[0]) * ratio;
                outptr++;
                line0++;
                line1++;
                // h mid ---- w mid
                ratio = 0.25f;

                asm volatile(
                    "addi           t3, %3, -1\n\t" // out_w --
                    "srai           t0, t3, 3\n\t"  // t0 = out_w - 1 >> 3 (out_w8)
                    "beqz           t0, 2f\n\t"
                    "vsetvli        zero, zero, e16, m1\n\t"

                "1:\n\t"
                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v4, v0, v1\n\t"
                    "addi           %0, %0, 32\n\t"

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v5, v2, v3\n\t"
                    "addi           %1, %1, 32\n\t"

                    "vfadd.vv       v6, v4, v5\n\t"
                    "vfmul.vf       v7, v6, %8\n\t"
                    "vse.v          v7, (%2)\n\t"
                    "addi           %2, %2, 16\n\t"

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                "2:\n\t"
                    "andi           t0, t3, 7\n\t"
                    "beqz           t0, 3f\n\t"

                    // out_w_tail
                    "vsetvli        zero, t0, e16, m1\n\t"
                    "slli           t1, t0, 1\n\t"
                    "slli           t2, t0, 2\n\t"

                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v4, v0, v1\n\t"
                    "add            %0, %0, t2\n\t"

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v5, v2, v3\n\t"
                    "add            %1, %1, t2\n\t"

                    "vfadd.vv       v6, v4, v5\n\t"
                    "vfmul.vf       v7, v6, %8\n\t"
                    "vse.v          v7, (%2)\n\t"
                    "add            %2, %2, t1\n\t"

                "3:\n\t"

                    :"=r"(line0),   // %0
                    "=r"(line1),    // %1
                    "=r"(outptr),   // %2
                    "=r"(out_w)     // %3
                    :"0"(line0),
                    "1"(line1),
                    "2"(outptr),
                    "3"(out_w),
                    "f"(ratio)      // %8
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "t0", "t1", "t2", "t3"
                );

                // h mid ---- w right
                ratio = (params->count_include_pad) ? 0.25f : 0.5f;
                if(extend_w) {
                    outptr[0] = (line0[0] + line1[0]) * ratio;
                    outptr++;
                }
                line0 += remain_w + in_w;
                line1 += remain_w + in_w;
            }

            // h bottom
            if(extend_h) {
                // h bottom ---- w left
                ratio = (params->count_include_pad) ? 0.25f : 1.0f;
                outptr[0] = line0[0] * ratio;
                outptr++;
                line0++;
                // h bottom ---- w mid
                ratio = (params->count_include_pad) ? 0.25f : 0.5f;

                asm volatile(
                    "addi           t3, %2, -1\n\t" // out_w --
                    "srai           t0, t3, 3\n\t"  // t0 = (out_w - 1) >> 3 (out_w8)
                    "beqz           t0, 2f\n\t"
                    "vsetvli        zero, zero, e16, m1\n\t"

                "1:\n\t"
                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v2, v0, v1\n\t"
                    "addi           %0, %0, 32\n\t"

                    "vfmul.vf       v3, v2, %6\n\t"
                    "vse.v          v3, (%1)\n\t"
                    "addi           %1, %1, 16\n\t"

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                "2:\n\t"
                    "andi           t0, t3, 7\n\t"
                    "beqz           t0, 3f\n\t"

                    // out_w_tail
                    "vsetvli        zero, t0, e16, m1\n\t"
                    "slli           t1, t0, 1\n\t"
                    "slli           t2, t0, 2\n\t"

                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v2, v0, v1\n\t"
                    "add            %0, %0, t2\n\t"

                    "vfmul.vf       v3, v2, %6\n\t"
                    "vse.v          v3, (%1)\n\t"
                    "add            %1, %1, t1\n\t"

                "3:\n\t"

                    :"=r"(line0),   // %0
                    "=r"(outptr),   // %1
                    "=r"(out_w)     // %2
                    :"0"(line0),
                    "1"(outptr),
                    "2"(out_w),
                    "f"(ratio)      // %6
                    :"cc", "memory", "v0", "v1", "v2", "v3", "t0", "t1", "t2", "t3"
                );

                // h bottom ---- w right
                ratio = (params->count_include_pad) ? 0.25f : 1.0f;
                if(extend_w) {
                    outptr[0] = line0[0] * ratio;
                }
            }
        }
        input_data  += input_size;
        output_data += output_size;
    }
    return CSINN_TRUE;
}

/*
    pad_left = pad_top = 0
    pad_right = 0 or 1
    pad_down = 0 or 1
*/
static int avgpool3x3s2(struct csi_tensor *input,
                        struct csi_tensor *output,
                        struct pool_params *params)
{
    float *input_data  = (float *)input->data;
    float *output_data = (float *)output->data;

    int batch = input->dim[0];
    int in_c  = input->dim[1];
    int in_h  = input->dim[2];
    int in_w  = input->dim[3];
    int input_size = in_c * in_h * in_w;

    int out_h = output->dim[2];
    int out_w = output->dim[3];
    int out_hw = out_h * out_w;
    int output_size = in_c * out_h * out_w;

    int extend_h = 0;
    int extend_w = 0;

    if (in_h % 2 == 0 && params->pad_down == 1) {
        extend_h = 1;
        out_h--;
    }
    if (in_w % 2 == 0 && params->pad_right == 1) {
        extend_w = 1;
        out_w--;
    }

    int out_w4 = out_w >> 2;
    int remain_w = in_w - 2 * out_w;
    float ratio = 0.11111111f;

    for(int b = 0; b < batch; b++) {

        for(int c = 0; c < in_c; c++) {

            const float *line0 = input_data + c * in_h * in_w;
            const float *line1 = line0 + in_w;
            const float *line2 = line1 + in_w;
            float *outptr = output_data + c * out_hw;

            for(int h = 0; h < out_h; h++) {
                float ratio = 0.11111111f;
#if __riscv_vector == 128
                if(out_w4 > 0) {
                    int load_stride = 8;
                    asm volatile(
                        "vsetvli        zero, zero, e32, m1\n\t"
                        "mv             t0, %4\n\t"     // t0 = out_w4
                    "1:\n\t"
                        "vlseg2e.v      v0, (%0)\n\t"   // v0[0..3] = line0[0,2.4.6]   v1[0..3] = line0[1,3,5,7]
                        "vfadd.vv       v6, v0, v1\n\t"
                        "addi           %0, %0, 8\n\t"  // line0 += 2
                        "vlsw.v         v9, (%0), %5\n\t"   // v9 = line0[2,4,6,8]
                        "vfadd.vv       v12, v6, v9\n\t"
                        "addi           %0, %0, 24\n\t" // line0 += 6

                        "vlseg2e.v      v2, (%1)\n\t"
                        "vfadd.vv       v7, v2, v3\n\t"
                        "addi           %1, %1, 8\n\t"  // line1 += 2
                        "vlsw.v         v10, (%1), %5\n\t"
                        "vfadd.vv       v13, v7, v10\n\t"
                        "addi           %1, %1, 24\n\t" // line1 += 6

                        "vlseg2e.v      v4, (%2)\n\t"
                        "vfadd.vv       v8, v4, v5\n\t"
                        "addi           %2, %2, 8\n\t"  // line2 += 2
                        "vlsw.v         v11, (%2), %5\n\t"
                        "vfadd.vv       v14, v8, v11\n\t"
                        "addi           %2, %2, 24\n\t" // line2 += 6

                        "vfadd.vv       v15, v12, v13\n\t"
                        "vfadd.vv       v15, v14, v15\n\t"
                        "vfmul.vf       v16, v15, %12\n\t"

                        "vsw.v          v16, (%3)\n\t"
                        "addi           %3, %3, 16\n\t" //outptr += 4

                        "addi           t0, t0, -1\n\t"
                        "bnez           t0, 1b\n\t"

                        :"=r"(line0),   // %0
                        "=r"(line1),    // %1
                        "=r"(line2),    // %2
                        "=r"(outptr),   // %3
                        "=r"(out_w4),   // %4
                        "=r"(load_stride)   // %5
                        :"0"(line0),
                        "1"(line1),
                        "2"(line2),
                        "3"(outptr),
                        "4"(out_w4),
                        "5"(load_stride),
                        "f"(ratio)      // %12
                        :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "t0"
                    );
                }
#else
                for(int w4 = 0; w4 < out_w4; w4++) {
                    float sum00 = line0[0] + line0[1] + line0[2];
                    float sum01 = line0[2] + line0[3] + line0[4];
                    float sum02 = line0[4] + line0[5] + line0[6];
                    float sum03 = line0[6] + line0[7] + line0[8];

                    float sum10 = line1[0] + line1[1] + line1[2];
                    float sum11 = line1[2] + line1[3] + line1[4];
                    float sum12 = line1[4] + line1[5] + line1[6];
                    float sum13 = line1[6] + line1[7] + line1[8];

                    float sum20 = line2[0] + line2[1] + line2[2];
                    float sum21 = line2[2] + line2[3] + line2[4];
                    float sum22 = line2[4] + line2[5] + line2[6];
                    float sum23 = line2[6] + line2[7] + line2[8];

                    outptr[0] = (sum00 + sum10 + sum20) * ratio;
                    outptr[1] = (sum01 + sum11 + sum21) * ratio;
                    outptr[2] = (sum02 + sum12 + sum22) * ratio;
                    outptr[3] = (sum03 + sum13 + sum23) * ratio;

                    line0 += 8;
                    line1 += 8;
                    line2 += 8;
                    outptr += 4;
                }
#endif  // __riscv_vector
                for(int i = out_w4 * 4; i < out_w; i++) {
                    float sum0 = line0[0] + line0[1] + line0[2];
                    float sum1 = line1[0] + line1[1] + line1[2];
                    float sum2 = line2[0] + line2[1] + line2[2];
                    outptr[0] = (sum0 + sum1 + sum2) * ratio;

                    line0 += 2;
                    line1 += 2;
                    line2 += 2;
                    outptr++;
                }
                if(extend_w) {
                    ratio = (params->count_include_pad) ? 0.11111111f : 0.16666667f;
                    outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
                    outptr++;
                }
                line0 += remain_w + in_w;
                line1 += remain_w + in_w;
                line2 += remain_w + in_w;
            }
            if(extend_h) {
                ratio = (params->count_include_pad) ? 0.11111111f : 0.16666667f;
#if __riscv_vector == 128
                if(out_w4 > 0) {
                    int load_stride = 8;
                    asm volatile(
                        "vsetvli        zero, zero, e32, m1\n\t"
                        "mv             t0, %3\n\t"
                    "1:\n\t"
                        "vlseg2e.v      v0, (%0)\n\t"
                        "vfadd.vv       v4, v0, v1\n\t"
                        "addi           %0, %0, 8\n\t"
                        "vlsw.v         v6, (%0), %4\n\t"
                        "vfadd.vv       v8, v4, v6\n\t"
                        "addi           %0, %0, 24\n\t"

                        "vlseg2e.v      v2, (%1)\n\t"
                        "vfadd.vv       v5, v2, v3\n\t"
                        "addi           %1, %1, 8\n\t"
                        "vlsw.v         v7, (%1), %4\n\t"
                        "vfadd.vv       v9, v5, v7\n\t"
                        "addi           %1, %1, 24\n\t"

                        "vfadd.vv       v10, v8, v9\n\t"
                        "vfmul.vf       v11, v10, %10\n\t"

                        "vsw.v          v11, (%2)\n\t"
                        "addi           %2, %2, 16\n\t"

                        "addi           t0, t0, -1\n\t"
                        "bnez           t0, 1b\n\t"

                        :"=r"(line0),   // %0
                        "=r"(line1),    // %1
                        "=r"(outptr),   // %2
                        "=r"(out_w4),   // %3
                        "=r"(load_stride)   // %4
                        :"0"(line0),
                        "1"(line1),
                        "2"(outptr),
                        "3"(out_w4),
                        "4"(load_stride),
                        "f"(ratio)      // %10
                        :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "t0"
                    );
                }
#else
                for(int w4 = 0; w4 < out_w4; w4++) {
                    float sum00 = line0[0] + line0[1] + line0[2];
                    float sum01 = line0[2] + line0[3] + line0[4];
                    float sum02 = line0[4] + line0[5] + line0[6];
                    float sum03 = line0[6] + line0[7] + line0[8];

                    float sum10 = line1[0] + line1[1] + line1[2];
                    float sum11 = line1[2] + line1[3] + line1[4];
                    float sum12 = line1[4] + line1[5] + line1[6];
                    float sum13 = line1[6] + line1[7] + line1[8];

                    outptr[0] = (sum00 + sum10) * ratio;
                    outptr[1] = (sum01 + sum11) * ratio;
                    outptr[2] = (sum02 + sum12) * ratio;
                    outptr[3] = (sum03 + sum13) * ratio;

                    line0 += 8;
                    line1 += 8;
                    outptr += 4;
                }
#endif  // __riscv_vector
                for(int i = out_w4 * 4; i < out_w; i++) {
                    float sum0 = line0[0] + line0[1] + line0[2];
                    float sum1 = line1[0] + line1[1] + line1[2];
                    outptr[0] = (sum0 + sum1) * ratio;

                    line0 += 2;
                    line1 += 2;
                    outptr++;
                }
                if(extend_w) {
                    ratio = (params->count_include_pad) ? 0.11111111f : 0.25f;
                    outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
                    outptr++;
                }
            }
        }
        input_data  += input_size;
        output_data  += output_size;
    }
    return CSINN_TRUE;
}

static int avgpool3x3s2_fp16(struct csi_tensor *input,
                             struct csi_tensor *output,
                             struct pool_params *params)
{
    __fp16 *input_data  = (__fp16 *)input->data;
    __fp16 *output_data = (__fp16 *)output->data;

    int batch = input->dim[0];
    int in_c  = input->dim[1];
    int in_h  = input->dim[2];
    int in_w  = input->dim[3];
    int input_size = in_c * in_h * in_w;

    int out_h = output->dim[2];
    int out_w = output->dim[3];
    int out_hw = out_h * out_w;
    int output_size = in_c * out_h * out_w;

    int extend_h = 0;
    int extend_w = 0;

    if (in_h % 2 == 0 && params->pad_down == 1) {
        extend_h = 1;
        out_h--;
    }
    if (in_w % 2 == 0 && params->pad_right == 1) {
        extend_w = 1;
        out_w--;
    }

    int out_w8 = out_w >> 3;
    int remain_w = in_w - 2 * out_w;
    __fp16 ratio = 0.11111f;

    for(int b = 0; b < batch; b++) {

        for(int c = 0; c < in_c; c++) {

            const __fp16 *line0 = input_data + c * in_h * in_w;
            const __fp16 *line1 = line0 + in_w;
            const __fp16 *line2 = line1 + in_w;
            __fp16 *outptr = output_data + c * out_hw;

            for(int h = 0; h < out_h; h++) {
                ratio = 0.11111f;

                asm volatile(
                    "li             t3, 4\n\t"      // load stride = 2 ele
                    "srai           t0, %4, 3\n\t"  // t0 = out_w >> 3 (out_w8)
                    "beqz           t0, 2f\n\t"
                    "vsetvli        zero, zero, e16, m1\n\t"

                "1:\n\t"
                    "vlseg2e.v      v0, (%0)\n\t"       // v0[0..7] = line0[0,2,4,6,8,10,12,14]   v1[0..7] = line0[1,3,5,7,9,11,13,15]
                    "vfadd.vv       v6, v0, v1\n\t"
                    "addi           %0, %0, 4\n\t"      // line0 += 2
                    "vlse.v         v9, (%0), t3\n\t"   // v9 = line0[2,4,6,8,10,12,14,16]
                    "vfadd.vv       v12, v6, v9\n\t"
                    "addi           %0, %0, 28\n\t"     // line0 += 14

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v7, v2, v3\n\t"
                    "addi           %1, %1, 4\n\t"      // line1 += 2
                    "vlse.v         v10, (%1), t3\n\t"
                    "vfadd.vv       v13, v7, v10\n\t"
                    "addi           %1, %1, 28\n\t"     // line1 += 14

                    "vlseg2e.v      v4, (%2)\n\t"
                    "vfadd.vv       v8, v4, v5\n\t"
                    "addi           %2, %2, 4\n\t"      // line2 += 2
                    "vlse.v         v11, (%2), t3\n\t"
                    "vfadd.vv       v14, v8, v11\n\t"
                    "addi           %2, %2, 28\n\t"     // line2 += 14

                    "vfadd.vv       v15, v12, v13\n\t"
                    "vfadd.vv       v15, v14, v15\n\t"
                    "vfmul.vf       v16, v15, %10\n\t"

                    "vse.v          v16, (%3)\n\t"
                    "addi           %3, %3, 16\n\t"     //outptr += 8

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                "2:\n\t"
                    "andi           t0, %4, 7\n\t"
                    "beqz           t0, 3f\n\t"

                    // out_w_tail
                    "vsetvli        zero, t0, e16, m1\n\t"
                    "slli           t1, t0, 1\n\t"
                    "slli           t2, t0, 2\n\t"
                    "addi           t2, t2, -4\n\t"

                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v6, v0, v1\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlse.v         v9, (%0), t3\n\t"
                    "vfadd.vv       v12, v6, v9\n\t"
                    "add            %0, %0, t2\n\t"

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v7, v2, v3\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlse.v         v10, (%1), t3\n\t"
                    "vfadd.vv       v13, v7, v10\n\t"
                    "add            %1, %1, t2\n\t"

                    "vlseg2e.v      v4, (%2)\n\t"
                    "vfadd.vv       v8, v4, v5\n\t"
                    "addi           %2, %2, 4\n\t"
                    "vlse.v         v11, (%2), t3\n\t"
                    "vfadd.vv       v14, v8, v11\n\t"
                    "add            %2, %2, t2\n\t"

                    "vfadd.vv       v15, v12, v13\n\t"
                    "vfadd.vv       v15, v14, v15\n\t"
                    "vfmul.vf       v16, v15, %10\n\t"

                    "vse.v          v16, (%3)\n\t"
                    "add            %3, %3, t1\n\t"

                "3:\n\t"

                    :"=r"(line0),   // %0
                    "=r"(line1),    // %1
                    "=r"(line2),    // %2
                    "=r"(outptr),   // %3
                    "=r"(out_w)     // %4
                    :"0"(line0),
                    "1"(line1),
                    "2"(line2),
                    "3"(outptr),
                    "4"(out_w),
                    "f"(ratio)      // %10
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
                     "t0", "t1", "t2", "t3"
                );

                if(extend_w) {
                    ratio = (params->count_include_pad) ? 0.11111f : 0.16667f;
                    outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
                    outptr++;
                }
                line0 += remain_w + in_w;
                line1 += remain_w + in_w;
                line2 += remain_w + in_w;
            }
            if(extend_h) {
                ratio = (params->count_include_pad) ? 0.11111f : 0.16667f;

                asm volatile(
                    "li             t3, 4\n\t"      // load stride = 2 ele
                    "srai           t0, %3, 3\n\t"  // t0 = out_w >> 3 (out_w8)
                    "beqz           t0, 2f\n\t"
                    "vsetvli        zero, zero, e16, m1\n\t"

                "1:\n\t"
                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v4, v0, v1\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlse.v         v6, (%0), t3\n\t"
                    "vfadd.vv       v8, v4, v6\n\t"
                    "addi           %0, %0, 28\n\t"

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v5, v2, v3\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlse.v         v7, (%1), t3\n\t"
                    "vfadd.vv       v9, v5, v7\n\t"
                    "addi           %1, %1, 28\n\t"

                    "vfadd.vv       v10, v8, v9\n\t"
                    "vfmul.vf       v11, v10, %8\n\t"

                    "vse.v          v11, (%2)\n\t"
                    "addi           %2, %2, 16\n\t"

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                "2:\n\t"
                    "andi           t0, %3, 7\n\t"
                    "beqz           t0, 3f\n\t"

                    // out_w_tail
                    "vsetvli        zero, t0, e16, m1\n\t"
                    "slli           t1, t0, 1\n\t"
                    "slli           t2, t0, 2\n\t"
                    "addi           t2, t2, -4\n\t"

                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v4, v0, v1\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlse.v         v6, (%0), t3\n\t"
                    "vfadd.vv       v8, v4, v6\n\t"
                    "add            %0, %0, t2\n\t"

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v5, v2, v3\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlse.v         v7, (%1), t3\n\t"
                    "vfadd.vv       v9, v5, v7\n\t"
                    "add            %1, %1, t2\n\t"

                    "vfadd.vv       v10, v8, v9\n\t"
                    "vfmul.vf       v11, v10, %8\n\t"

                    "vse.v          v11, (%2)\n\t"
                    "add            %2, %2, t1\n\t"

                "3:\n\t"

                    :"=r"(line0),   // %0
                    "=r"(line1),    // %1
                    "=r"(outptr),   // %2
                    "=r"(out_w)     // %3
                    :"0"(line0),
                    "1"(line1),
                    "2"(outptr),
                    "3"(out_w),
                    "f"(ratio)      // %8
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11",
                     "t0", "t1", "t2", "t3"
                );

                if(extend_w) {
                    ratio = (params->count_include_pad) ? 0.11111111f : 0.25f;
                    outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
                    outptr++;
                }
            }
        }
        input_data  += input_size;
        output_data  += output_size;
    }
    return CSINN_TRUE;
}

/*
    pad_left = pad_top = 1
    pad_right = 0 or 1
    pad_down = 0 or 1
*/
static int avgpool3x3s2_p1(struct csi_tensor *input,
                           struct csi_tensor *output,
                           struct pool_params *params)
{
    float *input_data  = (float *)input->data;
    float *output_data = (float *)output->data;

    int batch = input->dim[0];
    int in_c  = input->dim[1];
    int in_h  = input->dim[2];
    int in_w  = input->dim[3];
    int input_size = in_c * in_h * in_w;

    int out_h = output->dim[2];
    int out_w = output->dim[3];
    int out_hw = out_h * out_w;
    int output_size = in_c * out_h * out_w;

    int extend_h = 0;
    int extend_w = 0;

    if (in_h % 2 == 1 && params->pad_down == 1) {
        extend_h = 1;
        out_h--;
    }
    if (in_w % 2 == 1 && params->pad_right == 1) {
        extend_w = 1;
        out_w--;
    }

    int out_w4 = (out_w - 1) >> 2;
    int remain_w = in_w - 2 * out_w + 1;
    float ratio = 0.11111111f;

    for(int b = 0; b < batch; b++) {

        for(int c = 0; c < in_c; c++) {

            const float *line0 = input_data + c * in_h * in_w;
            const float *line1 = line0 + in_w;
            float *outptr = output_data + c * out_hw;

            // h top ---- w left
            ratio = (params->count_include_pad) ? 0.11111111f : 0.25f;
            outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
            outptr++;
            line0++;
            line1++;
            // h top ---- w mid
            ratio = (params->count_include_pad) ? 0.11111111f : 0.16666667f;
#if __riscv_vector == 128
            if(out_w4 > 0) {
                int load_stride = 8;
                float ratio = 0.11111111f;
                asm volatile(
                    "vsetvli        zero, zero, e32, m1\n\t"
                    "mv             t0, %3\n\t"     // t0 = out_w4
                "1:\n\t"
                    "vlseg2e.v      v0, (%0)\n\t"   // v0[0..3] = line0[0,2.4.6]   v1[0..3] = line0[1,3,5,7]
                    "vfadd.vv       v4, v0, v1\n\t"
                    "addi           %0, %0, 8\n\t"  // line0 += 2
                    "vlsw.v         v6, (%0), %4\n\t"   // v9 = line0[2,4,6,8]
                    "vfadd.vv       v8, v4, v6\n\t"
                    "addi           %0, %0, 24\n\t" // line0 += 6

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v5, v2, v3\n\t"
                    "addi           %1, %1, 8\n\t"  // line1 += 2
                    "vlsw.v         v7, (%1), %4\n\t"
                    "vfadd.vv       v9, v5, v7\n\t"
                    "addi           %1, %1, 24\n\t" // line1 += 6

                    "vfadd.vv       v10, v8, v9\n\t"
                    "vfmul.vf       v11, v10, %10\n\t"

                    "vsw.v          v11, (%2)\n\t"
                    "addi           %2, %2, 16\n\t" //outptr += 4

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                    :"=r"(line0),   // %0
                    "=r"(line1),    // %1
                    "=r"(outptr),   // %2
                    "=r"(out_w4),   // %3
                    "=r"(load_stride)   // %4
                    :"0"(line0),
                    "1"(line1),
                    "2"(outptr),
                    "3"(out_w4),
                    "4"(load_stride),
                    "f"(ratio)      // %10
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "t0"
                );

            }
#else
            for(int w4 = 0; w4 < out_w4; w4++) {
                float sum00 = line0[0] + line0[1] + line0[2];
                float sum01 = line0[2] + line0[3] + line0[4];
                float sum02 = line0[4] + line0[5] + line0[6];
                float sum03 = line0[6] + line0[7] + line0[8];

                float sum10 = line1[0] + line1[1] + line1[2];
                float sum11 = line1[2] + line1[3] + line1[4];
                float sum12 = line1[4] + line1[5] + line1[6];
                float sum13 = line1[6] + line1[7] + line1[8];

                outptr[0] = (sum00 + sum10) * ratio;
                outptr[1] = (sum01 + sum11) * ratio;
                outptr[2] = (sum02 + sum12) * ratio;
                outptr[3] = (sum03 + sum13) * ratio;

                line0 += 8;
                line1 += 8;
                outptr += 4;
            }
#endif  // __riscv_vector
            for(int i = out_w4 * 4 + 1; i < out_w; i++) {
                float sum0 = line0[0] + line0[1] + line0[2];
                float sum1 = line1[0] + line1[1] + line1[2];
                outptr[0] = (sum0 + sum1) * ratio;

                line0 += 2;
                line1 += 2;
                outptr++;
            }
            // h top ---- w right
            ratio = (params->count_include_pad) ? 0.11111111f : 0.25f;
            if(extend_w) {
                outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
                outptr++;
            }
            line0 += remain_w;
            line1 += remain_w;

            // h mid
            const float *line2 = line1 + in_w;
            for(int h = 0; h < out_h -1; h++) {
                // h mid ---- w left
                ratio = (params->count_include_pad) ? 0.11111111f : 0.16666667f;
                outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
                outptr++;
                line0++;
                line1++;
                line2++;
                // h mid ---- w mid
                ratio = 0.11111111f;
#if __riscv_vector == 128
                if(out_w4 > 0) {
                    int load_stride = 8;
                    asm volatile(
                        "vsetvli        zero, zero, e32, m1\n\t"
                        "mv             t0, %4\n\t"
                    "1:\n\t"
                        "vlseg2e.v      v0, (%0)\n\t"
                        "vfadd.vv       v6, v0, v1\n\t"
                        "addi           %0, %0, 8\n\t"
                        "vlsw.v         v9, (%0), %5\n\t"
                        "vfadd.vv       v12, v6, v9\n\t"
                        "addi           %0, %0, 24\n\t"

                        "vlseg2e.v      v2, (%1)\n\t"
                        "vfadd.vv       v7, v2, v3\n\t"
                        "addi           %1, %1, 8\n\t"
                        "vlsw.v         v10, (%1), %5\n\t"
                        "vfadd.vv       v13, v7, v10\n\t"
                        "addi           %1, %1, 24\n\t"

                        "vlseg2e.v      v4, (%2)\n\t"
                        "vfadd.vv       v8, v4, v5\n\t"
                        "addi           %2, %2, 8\n\t"
                        "vlsw.v         v11, (%2), %5\n\t"
                        "vfadd.vv       v14, v8, v11\n\t"
                        "addi           %2, %2, 24\n\t"

                        "vfadd.vv       v15, v12, v13\n\t"
                        "vfadd.vv       v15, v14, v15\n\t"
                        "vfmul.vf       v16, v15, %12\n\t"

                        "vsw.v          v16, (%3)\n\t"
                        "addi           %3, %3, 16\n\t"

                        "addi           t0, t0, -1\n\t"
                        "bnez           t0, 1b\n\t"

                        :"=r"(line0),   // %0
                        "=r"(line1),    // %1
                        "=r"(line2),    // %2
                        "=r"(outptr),   // %3
                        "=r"(out_w4),   // %4
                        "=r"(load_stride)   // %5
                        :"0"(line0),
                        "1"(line1),
                        "2"(line2),
                        "3"(outptr),
                        "4"(out_w4),
                        "5"(load_stride),
                        "f"(ratio)      // %12
                        :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "t0"
                    );
                }
#else
                for(int w4 = 0; w4 < out_w4; w4++) {
                    float sum00 = line0[0] + line0[1] + line0[2];
                    float sum01 = line0[2] + line0[3] + line0[4];
                    float sum02 = line0[4] + line0[5] + line0[6];
                    float sum03 = line0[6] + line0[7] + line0[8];

                    float sum10 = line1[0] + line1[1] + line1[2];
                    float sum11 = line1[2] + line1[3] + line1[4];
                    float sum12 = line1[4] + line1[5] + line1[6];
                    float sum13 = line1[6] + line1[7] + line1[8];

                    float sum20 = line2[0] + line2[1] + line2[2];
                    float sum21 = line2[2] + line2[3] + line2[4];
                    float sum22 = line2[4] + line2[5] + line2[6];
                    float sum23 = line2[6] + line2[7] + line2[8];

                    outptr[0] = (sum00 + sum10 + sum20) * ratio;
                    outptr[1] = (sum01 + sum11 + sum21) * ratio;
                    outptr[2] = (sum02 + sum12 + sum22) * ratio;
                    outptr[3] = (sum03 + sum13 + sum23) * ratio;

                    line0 += 8;
                    line1 += 8;
                    line2 += 8;
                    outptr += 4;
                }
#endif  // __riscv_vector
                for(int i = out_w4 * 4 + 1; i < out_w; i++) {
                    float sum0 = line0[0] + line0[1] + line0[2];
                    float sum1 = line1[0] + line1[1] + line1[2];
                    float sum2 = line2[0] + line2[1] + line2[2];
                    outptr[0] = (sum0 + sum1 + sum2) * ratio;

                    line0 += 2;
                    line1 += 2;
                    line2 += 2;
                    outptr++;
                }
                // h mid ---- w right
                ratio = (params->count_include_pad) ? 0.11111111f : 0.16666667f;
                if(extend_w) {
                    outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
                    outptr++;
                }
                line0 += in_w + remain_w;
                line1 += in_w + remain_w;
                line2 += in_w + remain_w;
            }

            // h bottom
            if(extend_h) {
                // h bottom ---- w left
                ratio = (params->count_include_pad) ? 0.11111111f : 0.25f;
                outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
                outptr++;
                line0++;
                line1++;

                // h bottom ---- w mid
                ratio = (params->count_include_pad) ? 0.11111111f : 0.16666667f;
#if __riscv_vector == 128
                if(out_w4 > 0) {
                    int load_stride = 8;
                    asm volatile(
                        "vsetvli        zero, zero, e32, m1\n\t"
                        "mv             t0, %3\n\t"
                    "1:\n\t"
                        "vlseg2e.v      v0, (%0)\n\t"
                        "vfadd.vv       v4, v0, v1\n\t"
                        "addi           %0, %0, 8\n\t"
                        "vlsw.v         v6, (%0), %4\n\t"
                        "vfadd.vv       v8, v4, v6\n\t"
                        "addi           %0, %0, 24\n\t"

                        "vlseg2e.v      v2, (%1)\n\t"
                        "vfadd.vv       v5, v2, v3\n\t"
                        "addi           %1, %1, 8\n\t"
                        "vlsw.v         v7, (%1), %4\n\t"
                        "vfadd.vv       v9, v5, v7\n\t"
                        "addi           %1, %1, 24\n\t"

                        "vfadd.vv       v10, v8, v9\n\t"
                        "vfmul.vf       v11, v10, %10\n\t"

                        "vsw.v          v11, (%2)\n\t"
                        "addi           %2, %2, 16\n\t"

                        "addi           t0, t0, -1\n\t"
                        "bnez           t0, 1b\n\t"

                        :"=r"(line0),   // %0
                        "=r"(line1),    // %1
                        "=r"(outptr),   // %2
                        "=r"(out_w4),   // %3
                        "=r"(load_stride)   // %4
                        :"0"(line0),
                        "1"(line1),
                        "2"(outptr),
                        "3"(out_w4),
                        "4"(load_stride),
                        "f"(ratio)      // %10
                        :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "t0"
                    );
                }
#else
                for(int w4 = 0; w4 < out_w4; w4++) {
                    float sum00 = line0[0] + line0[1] + line0[2];
                    float sum01 = line0[2] + line0[3] + line0[4];
                    float sum02 = line0[4] + line0[5] + line0[6];
                    float sum03 = line0[6] + line0[7] + line0[8];

                    float sum10 = line1[0] + line1[1] + line1[2];
                    float sum11 = line1[2] + line1[3] + line1[4];
                    float sum12 = line1[4] + line1[5] + line1[6];
                    float sum13 = line1[6] + line1[7] + line1[8];

                    outptr[0] = (sum00 + sum10) * ratio;
                    outptr[1] = (sum01 + sum11) * ratio;
                    outptr[2] = (sum02 + sum12) * ratio;
                    outptr[3] = (sum03 + sum13) * ratio;

                    line0 += 8;
                    line1 += 8;
                    outptr += 4;
                }
#endif  // __riscv_vector
                for(int i = out_w4 * 4 + 1; i < out_w; i++) {
                    float sum0 = line0[0] + line0[1] + line0[2];
                    float sum1 = line1[0] + line1[1] + line1[2];
                    outptr[0] = (sum0 + sum1) * ratio;

                    line0 += 2;
                    line1 += 2;
                    outptr++;
                }
                // h bottom ---- w right
                ratio = (params->count_include_pad) ? 0.11111111f : 0.25f;
                if(extend_w) {
                    outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
                    outptr++;
                }
            }
        }
        input_data  += input_size;
        output_data += output_size;
    }
    return CSINN_TRUE;
}

static int avgpool3x3s2_p1_fp16(struct csi_tensor *input,
                                struct csi_tensor *output,
                                struct pool_params *params)
{
    __fp16 *input_data  = (__fp16 *)input->data;
    __fp16 *output_data = (__fp16 *)output->data;

    int batch = input->dim[0];
    int in_c  = input->dim[1];
    int in_h  = input->dim[2];
    int in_w  = input->dim[3];
    int input_size = in_c * in_h * in_w;

    int out_h = output->dim[2];
    int out_w = output->dim[3];
    int out_hw = out_h * out_w;
    int output_size = in_c * out_h * out_w;

    int extend_h = 0;
    int extend_w = 0;

    if (in_h % 2 == 1 && params->pad_down == 1) {
        extend_h = 1;
        out_h--;
    }
    if (in_w % 2 == 1 && params->pad_right == 1) {
        extend_w = 1;
        out_w--;
    }

    int out_w8 = (out_w - 1) >> 3;
    int remain_w = in_w - 2 * out_w + 1;
    __fp16 ratio = 0.11111f;

    for(int b = 0; b < batch; b++) {

        for(int c = 0; c < in_c; c++) {

            const __fp16 *line0 = input_data + c * in_h * in_w;
            const __fp16 *line1 = line0 + in_w;
            __fp16 *outptr = output_data + c * out_hw;

            // h top ---- w left
            ratio = (params->count_include_pad) ? 0.11111f : 0.25f;
            outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
            outptr++;
            line0++;
            line1++;
            // h top ---- w mid
            ratio = (params->count_include_pad) ? 0.11111f : 0.16667f;

            asm volatile(
                "li             t3, 4\n\t"          // load stride = 2 ele
                "addi           t4, %3, -1\n\t"     // t4 = out_w --
                "srai           t0, t4, 3\n\t"      // t0 = (out_w - 1) >> 3 (out_w8)
                "beqz           t0, 2f\n\t"
                "vsetvli        zero, zero, e16, m1\n\t"

            "1:\n\t"
                "vlseg2e.v      v0, (%0)\n\t"       // v0[0..7] = line0[0,2,4,6,8,10,12,14]   v1[0..7] = line0[1,3,5,7,9,11,13,15]
                "vfadd.vv       v4, v0, v1\n\t"
                "addi           %0, %0, 4\n\t"      // line0 += 2
                "vlse.v         v6, (%0), t3\n\t"   // v9 = line0[2,4,6,8,10,12,14,16]
                "vfadd.vv       v8, v4, v6\n\t"
                "addi           %0, %0, 28\n\t"     // line0 += 14

                "vlseg2e.v      v2, (%1)\n\t"
                "vfadd.vv       v5, v2, v3\n\t"
                "addi           %1, %1, 4\n\t"      // line1 += 2
                "vlse.v         v7, (%1), t3\n\t"
                "vfadd.vv       v9, v5, v7\n\t"
                "addi           %1, %1, 28\n\t"     // line1 += 14

                "vfadd.vv       v10, v8, v9\n\t"
                "vfmul.vf       v11, v10, %8\n\t"

                "vse.v          v11, (%2)\n\t"
                "addi           %2, %2, 16\n\t"     // outptr += 8

                "addi           t0, t0, -1\n\t"
                "bnez           t0, 1b\n\t"

            "2:\n\t"
                "andi           t0, t4, 7\n\t"
                "beqz           t0, 3f\n\t"

                // out_w_tail
                "vsetvli        zero, t0, e16, m1\n\t"
                "slli           t1, t0, 1\n\t"
                "slli           t2, t0, 2\n\t"
                "addi           t2, t2, -4\n\t"

                "vlseg2e.v      v0, (%0)\n\t"
                "vfadd.vv       v4, v0, v1\n\t"
                "addi           %0, %0, 4\n\t"
                "vlse.v         v6, (%0), t3\n\t"
                "vfadd.vv       v8, v4, v6\n\t"
                "add            %0, %0, t2\n\t"

                "vlseg2e.v      v2, (%1)\n\t"
                "vfadd.vv       v5, v2, v3\n\t"
                "addi           %1, %1, 4\n\t"
                "vlse.v         v7, (%1), t3\n\t"
                "vfadd.vv       v9, v5, v7\n\t"
                "add            %1, %1, t2\n\t"

                "vfadd.vv       v10, v8, v9\n\t"
                "vfmul.vf       v11, v10, %8\n\t"

                "vse.v          v11, (%2)\n\t"
                "add            %2, %2, t1\n\t"

            "3:\n\t"

                :"=r"(line0),   // %0
                "=r"(line1),    // %1
                "=r"(outptr),   // %2
                "=r"(out_w)     // %3
                :"0"(line0),
                "1"(line1),
                "2"(outptr),
                "3"(out_w),
                "f"(ratio)      // %8
                :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11",
                 "t0", "t1", "t2", "t3", "t4"

            );

            // h top ---- w right
            ratio = (params->count_include_pad) ? 0.11111f : 0.25f;
            if(extend_w) {
                outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
                outptr++;
            }
            line0 += remain_w;
            line1 += remain_w;

            // h mid
            const __fp16 *line2 = line1 + in_w;
            for(int h = 0; h < out_h -1; h++) {
                // h mid ---- w left
                ratio = (params->count_include_pad) ? 0.11111f : 0.16667f;
                outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
                outptr++;
                line0++;
                line1++;
                line2++;
                // h mid ---- w mid
                ratio = 0.11111f;

                asm volatile(
                    "li             t3, 4\n\t"          // load stride = 2 ele
                    "addi           t4, %4, -1\n\t"     // t4 = out_w --
                    "srai           t0, t4, 3\n\t"      // t0 = (out_w - 1) >> 3 (out_w8)
                    "beqz           t0, 2f\n\t"
                    "vsetvli        zero, zero, e16, m1\n\t"

                "1:\n\t"
                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v6, v0, v1\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlse.v         v9, (%0), t3\n\t"
                    "vfadd.vv       v12, v6, v9\n\t"
                    "addi           %0, %0, 28\n\t"

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v7, v2, v3\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlse.v         v10, (%1), t3\n\t"
                    "vfadd.vv       v13, v7, v10\n\t"
                    "addi           %1, %1, 28\n\t"

                    "vlseg2e.v      v4, (%2)\n\t"
                    "vfadd.vv       v8, v4, v5\n\t"
                    "addi           %2, %2, 4\n\t"
                    "vlse.v         v11, (%2), t3\n\t"
                    "vfadd.vv       v14, v8, v11\n\t"
                    "addi           %2, %2, 28\n\t"

                    "vfadd.vv       v15, v12, v13\n\t"
                    "vfadd.vv       v15, v14, v15\n\t"
                    "vfmul.vf       v16, v15, %10\n\t"

                    "vse.v          v16, (%3)\n\t"
                    "addi           %3, %3, 16\n\t"

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                "2:\n\t"
                    "andi           t0, t4, 7\n\t"
                    "beqz           t0, 3f\n\t"

                    // out_w_tail
                    "vsetvli        zero, t0, e16, m1\n\t"
                    "slli           t1, t0, 1\n\t"
                    "slli           t2, t0, 2\n\t"
                    "addi           t2, t2, -4\n\t"

                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v6, v0, v1\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlse.v         v9, (%0), t3\n\t"
                    "vfadd.vv       v12, v6, v9\n\t"
                    "add            %0, %0, t2\n\t"

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v7, v2, v3\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlse.v         v10, (%1), t3\n\t"
                    "vfadd.vv       v13, v7, v10\n\t"
                    "add            %1, %1, t2\n\t"

                    "vlseg2e.v      v4, (%2)\n\t"
                    "vfadd.vv       v8, v4, v5\n\t"
                    "addi           %2, %2, 4\n\t"
                    "vlse.v         v11, (%2), t3\n\t"
                    "vfadd.vv       v14, v8, v11\n\t"
                    "add            %2, %2, t2\n\t"

                    "vfadd.vv       v15, v12, v13\n\t"
                    "vfadd.vv       v15, v14, v15\n\t"
                    "vfmul.vf       v16, v15, %10\n\t"

                    "vse.v          v16, (%3)\n\t"
                    "add            %3, %3, t1\n\t"

                "3:\n\t"

                    :"=r"(line0),   // %0
                    "=r"(line1),    // %1
                    "=r"(line2),    // %2
                    "=r"(outptr),   // %3
                    "=r"(out_w)     // %4
                    :"0"(line0),
                    "1"(line1),
                    "2"(line2),
                    "3"(outptr),
                    "4"(out_w),
                    "f"(ratio)      // %10
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16",
                     "t0", "t1", "t2", "t3", "t4"

                );

                // h mid ---- w right
                ratio = (params->count_include_pad) ? 0.11111f : 0.16667f;
                if(extend_w) {
                    outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
                    outptr++;
                }
                line0 += in_w + remain_w;
                line1 += in_w + remain_w;
                line2 += in_w + remain_w;
            }

            // h bottom
            if(extend_h) {
                // h bottom ---- w left
                ratio = (params->count_include_pad) ? 0.11111f : 0.25f;
                outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
                outptr++;
                line0++;
                line1++;

                // h bottom ---- w mid
                ratio = (params->count_include_pad) ? 0.11111f : 0.16667f;
                asm volatile(
                    "li             t3, 4\n\t"          // load stride = 2 ele
                    "addi           t4, %3, -1\n\t"     // t4 = out_w --
                    "srai           t0, t4, 3\n\t"      // t0 = (out_w - 1) >> 3 (out_w8)
                    "beqz           t0, 2f\n\t"
                    "vsetvli        zero, zero, e16, m1\n\t"

                "1:\n\t"
                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v4, v0, v1\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlse.v         v6, (%0), t3\n\t"
                    "vfadd.vv       v8, v4, v6\n\t"
                    "addi           %0, %0, 28\n\t"

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v5, v2, v3\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlse.v         v7, (%1), t3\n\t"
                    "vfadd.vv       v9, v5, v7\n\t"
                    "addi           %1, %1, 28\n\t"

                    "vfadd.vv       v10, v8, v9\n\t"
                    "vfmul.vf       v11, v10, %8\n\t"

                    "vse.v          v11, (%2)\n\t"
                    "addi           %2, %2, 16\n\t"

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                "2:\n\t"
                    "andi           t0, t4, 7\n\t"
                    "beqz           t0, 3f\n\t"

                    // out_w_tail
                    "vsetvli        zero, t0, e16, m1\n\t"
                    "slli           t1, t0, 1\n\t"
                    "slli           t2, t0, 2\n\t"
                    "addi           t2, t2, -4\n\t"

                    "vlseg2e.v      v0, (%0)\n\t"
                    "vfadd.vv       v4, v0, v1\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlse.v         v6, (%0), t3\n\t"
                    "vfadd.vv       v8, v4, v6\n\t"
                    "add            %0, %0, t2\n\t"

                    "vlseg2e.v      v2, (%1)\n\t"
                    "vfadd.vv       v5, v2, v3\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlse.v         v7, (%1), t3\n\t"
                    "vfadd.vv       v9, v5, v7\n\t"
                    "add            %1, %1, t2\n\t"

                    "vfadd.vv       v10, v8, v9\n\t"
                    "vfmul.vf       v11, v10, %8\n\t"

                    "vse.v          v11, (%2)\n\t"
                    "add            %2, %2, t1\n\t"

                "3:\n\t"

                    :"=r"(line0),   // %0
                    "=r"(line1),    // %1
                    "=r"(outptr),   // %2
                    "=r"(out_w)     // %3
                    :"0"(line0),
                    "1"(line1),
                    "2"(outptr),
                    "3"(out_w),
                    "f"(ratio)      // %8
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11",
                     "t0", "t1", "t2", "t3", "t4"
                );

                // h bottom ---- w right
                ratio = (params->count_include_pad) ? 0.11111f : 0.25f;
                if(extend_w) {
                    outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
                    outptr++;
                }
            }
        }
        input_data  += input_size;
        output_data += output_size;
    }
    return CSINN_TRUE;
}


/*
    pad_left = pad_right = pad_top = pad_down = 1
    in_w = out_w   in_h = out_h
*/
static int avgpool3x3s1_p1(struct csi_tensor *input,
                           struct csi_tensor *output,
                           struct pool_params *params)
{
    float *input_data  = (float *)input->data;
    float *output_data = (float *)output->data;

    int batch = input->dim[0];
    int in_c  = input->dim[1];
    int in_h  = input->dim[2];
    int in_w  = input->dim[3];
    int input_size = in_c * in_h * in_w;

    int out_h = output->dim[2];
    int out_w = output->dim[3];
    int output_size = in_c * out_h * out_w;

    int out_w4 = (out_w - 2) >> 2;
    float ratio = 0.11111111f;

    for(int b = 0; b < batch; b++) {

        for(int c = 0; c < in_c; c++) {

            const float *line1 = input_data + c * in_h * in_w;
            const float *line2 = line1 + in_w;
            float *outptr = output_data + c * out_h * out_w;
            // h top ---- w left
            ratio = (params->count_include_pad) ? 0.11111111f : 0.25f;
            outptr[0] = (line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
            outptr++;
            // h top ---- w mid
            ratio = (params->count_include_pad) ? 0.11111111f : 0.16666667f;
#if __riscv_vector == 128
            if(out_w4 > 0) {
                asm volatile(
                    "vsetvli        zero, zero, e32, m1\n\t"
                    "mv             t0, %3\n\t"
                "1:\n\t"
                    "vlw.v          v0, (%0)\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlw.v          v1, (%0)\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlw.v          v2, (%0)\n\t"
                    "addi           %0, %0, 8\n\t"
                    "vfadd.vv       v3, v0, v1\n\t"
                    "vfadd.vv       v4, v2, v3\n\t"

                    "vlw.v          v5, (%1)\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlw.v          v6, (%1)\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlw.v          v7, (%1)\n\t"
                    "addi           %1, %1, 8\n\t"
                    "vfadd.vv       v8, v5, v6\n\t"
                    "vfadd.vv       v9, v7, v8\n\t"

                    "vfadd.vv       v10, v4, v9\n\t"
                    "vfmul.vf       v11, v10, %8\n\t"
                    "vsw.v          v11, (%2)\n\t"
                    "addi           %2, %2, 16\n\t"

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                    :"=r"(line1),   // %0
                    "=r"(line2),    // %1
                    "=r"(outptr),   // %2
                    "=r"(out_w4)    // %3
                    :"0"(line1),
                    "1"(line2),
                    "2"(outptr),
                    "3"(out_w4),
                    "f"(ratio)      // %8
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "t0"
                );
            }
#else
            for(int w4 = 0; w4 < out_w4; w4++) {
                float sum10 = line1[0] + line1[1] + line1[2];
                float sum11 = line1[1] + line1[2] + line1[3];
                float sum12 = line1[2] + line1[3] + line1[4];
                float sum13 = line1[3] + line1[4] + line1[5];

                float sum20 = line2[0] + line2[1] + line2[2];
                float sum21 = line2[1] + line2[2] + line2[3];
                float sum22 = line2[2] + line2[3] + line2[4];
                float sum23 = line2[3] + line2[4] + line2[5];

                outptr[0] = (sum10 + sum20) * ratio;
                outptr[1] = (sum11 + sum21) * ratio;
                outptr[2] = (sum12 + sum22) * ratio;
                outptr[3] = (sum13 + sum23) * ratio;

                line1 += 4;
                line2 += 4;
                outptr += 4;
            }
#endif  // __riscv_vctor
            for(int i = out_w4 * 4; i < out_w - 2; i++) {
                outptr[0] = (line1[0] + line1[1] + line1[2] + line2[0] + line2[1] + line2[2]) * ratio;
                outptr++;
                line1++;
                line2++;
            }
            // h top ---- w right
            ratio = (params->count_include_pad) ? 0.11111111f : 0.25f;
            outptr[0] = (line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
            outptr++;
            line1 += 2;     // bump next line: line1 --> line2
            line2 += 2;

            // h mid
            const float *line0 = input_data + c * in_h * in_w;
            for(int h = 0; h < out_h - 2; h++) {

                // h mid ---- w left
                ratio = (params->count_include_pad) ? 0.11111111f : 0.16666667f;
                outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
                outptr++;
                // h mid ---- w mid
                ratio = 0.11111111f;
#if __riscv_vector == 128
                if(out_w4 > 0) {
                    asm volatile(
                        "vsetvli        zero, zero, e32, m1\n\t"
                        "mv             t0, %4\n\t"
                    "1:\n\t"
                        "vlw.v          v0, (%0)\n\t"
                        "addi           %0, %0, 4\n\t"
                        "vlw.v          v1, (%0)\n\t"
                        "addi           %0, %0, 4\n\t"
                        "vlw.v          v2, (%0)\n\t"
                        "addi           %0, %0, 8\n\t"
                        "vfadd.vv       v3, v0, v1\n\t"
                        "vfadd.vv       v4, v2, v3\n\t"

                        "vlw.v          v5, (%1)\n\t"
                        "addi           %1, %1, 4\n\t"
                        "vlw.v          v6, (%1)\n\t"
                        "addi           %1, %1, 4\n\t"
                        "vlw.v          v7, (%1)\n\t"
                        "addi           %1, %1, 8\n\t"
                        "vfadd.vv       v8, v5, v6\n\t"
                        "vfadd.vv       v9, v7, v8\n\t"

                        "vlw.v          v10, (%2)\n\t"
                        "addi           %2, %2, 4\n\t"
                        "vlw.v          v11, (%2)\n\t"
                        "addi           %2, %2, 4\n\t"
                        "vlw.v          v12, (%2)\n\t"
                        "addi           %2, %2, 8\n\t"
                        "vfadd.vv       v13, v10, v11\n\t"
                        "vfadd.vv       v14, v12, v13\n\t"

                        "vfadd.vv       v15, v4, v9\n\t"
                        "vfadd.vv       v15, v14, v15\n\t"
                        "vfmul.vf       v16, v15, %10\n\t"
                        "vsw.v          v16, (%3)\n\t"
                        "addi           %3, %3, 16\n\t"

                        "addi           t0, t0, -1\n\t"
                        "bnez           t0, 1b\n\t"

                        :"=r"(line0),   // %0
                        "=r"(line1),    // %1
                        "=r"(line2),    // %2
                        "=r"(outptr),   // %3
                        "=r"(out_w4)    // %4
                        :"0"(line0),
                        "1"(line1),
                        "2"(line2),
                        "3"(outptr),
                        "4"(out_w4),
                        "f"(ratio)      // %10
                        :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "t0"
                    );
                }
#else
                for(int w4 = 0; w4 < out_w4; w4++) {
                    float sum00 = line0[0] + line0[1] + line0[2];
                    float sum01 = line0[1] + line0[2] + line0[3];
                    float sum02 = line0[2] + line0[3] + line0[4];
                    float sum03 = line0[3] + line0[4] + line0[5];

                    float sum10 = line1[0] + line1[1] + line1[2];
                    float sum11 = line1[1] + line1[2] + line1[3];
                    float sum12 = line1[2] + line1[3] + line1[4];
                    float sum13 = line1[3] + line1[4] + line1[5];

                    float sum20 = line2[0] + line2[1] + line2[2];
                    float sum21 = line2[1] + line2[2] + line2[3];
                    float sum22 = line2[2] + line2[3] + line2[4];
                    float sum23 = line2[3] + line2[4] + line2[5];

                    outptr[0] = (sum00 + sum10 + sum20) * ratio;
                    outptr[1] = (sum01 + sum11 + sum21) * ratio;
                    outptr[2] = (sum02 + sum12 + sum22) * ratio;
                    outptr[3] = (sum03 + sum13 + sum23) * ratio;

                    line0 += 4;
                    line1 += 4;
                    line2 += 4;
                    outptr += 4;
                }
#endif  // __riscv_vctor
                for(int i = out_w4 * 4; i < out_w - 2; i++) {
                    outptr[0] = (line0[0] + line0[1] + line0[2] + line1[0] + line1[1] + line1[2]
                               + line2[0] + line2[1] + line2[2]) * ratio;
                    outptr++;
                    line0++;
                    line1++;
                    line2++;
                }
                // h mid ---- w right
                ratio = (params->count_include_pad) ? 0.11111111f : 0.16666667f;
                outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
                outptr++;
                line0 += 2;
                line1 += 2;
                line2 += 2;
            }

            // h bottom ---- w left
            ratio = (params->count_include_pad) ? 0.11111111f : 0.25f;
            outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
            outptr++;
            // h bottom ---- w mid
            ratio = (params->count_include_pad) ? 0.11111111f : 0.16666667f;
#if __riscv_vector == 128
            if(out_w4 > 0) {
                asm volatile(
                    "vsetvli        zero, zero, e32, m1\n\t"
                    "mv             t0, %3\n\t"
                "1:\n\t"
                    "vlw.v          v0, (%0)\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlw.v          v1, (%0)\n\t"
                    "addi           %0, %0, 4\n\t"
                    "vlw.v          v2, (%0)\n\t"
                    "addi           %0, %0, 8\n\t"
                    "vfadd.vv       v3, v0, v1\n\t"
                    "vfadd.vv       v4, v2, v3\n\t"

                    "vlw.v          v5, (%1)\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlw.v          v6, (%1)\n\t"
                    "addi           %1, %1, 4\n\t"
                    "vlw.v          v7, (%1)\n\t"
                    "addi           %1, %1, 8\n\t"
                    "vfadd.vv       v8, v5, v6\n\t"
                    "vfadd.vv       v9, v7, v8\n\t"

                    "vfadd.vv       v10, v4, v9\n\t"
                    "vfmul.vf       v11, v10, %8\n\t"
                    "vsw.v          v11, (%2)\n\t"
                    "addi           %2, %2, 16\n\t"

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                    :"=r"(line0),   // %0
                    "=r"(line1),    // %1
                    "=r"(outptr),   // %2
                    "=r"(out_w4)    // %3
                    :"0"(line0),
                    "1"(line1),
                    "2"(outptr),
                    "3"(out_w4),
                    "f"(ratio)      // %8
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "t0"
                );
            }
#else
            for(int w4 = 0; w4 < out_w4; w4++) {
                float sum00 = line0[0] + line0[1] + line0[2];
                float sum01 = line0[1] + line0[2] + line0[3];
                float sum02 = line0[2] + line0[3] + line0[4];
                float sum03 = line0[3] + line0[4] + line0[5];

                float sum10 = line1[0] + line1[1] + line1[2];
                float sum11 = line1[1] + line1[2] + line1[3];
                float sum12 = line1[2] + line1[3] + line1[4];
                float sum13 = line1[3] + line1[4] + line1[5];

                outptr[0] = (sum00 + sum10) * ratio;
                outptr[1] = (sum01 + sum11) * ratio;
                outptr[2] = (sum02 + sum12) * ratio;
                outptr[3] = (sum03 + sum13) * ratio;
                line0 += 4;
                line1 += 4;
                outptr += 4;
            }
#endif  // __riscv_vctor
            for(int i = out_w4 * 4; i < out_w - 2; i++) {
                outptr[0] = (line0[0] + line0[1] + line0[2] + line1[0] + line1[1] + line1[2]) * ratio;
                outptr++;
                line0++;
                line1++;
            }
            // h bottom ---- w right
            ratio = (params->count_include_pad) ? 0.11111111f : 0.25f;
            outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
        }
        input_data  += input_size;
        output_data += output_size;
    }
    return CSINN_TRUE;
}

static int avgpool3x3s1_p1_fp16(struct csi_tensor *input,
                                struct csi_tensor *output,
                                struct pool_params *params)
{
    __fp16 *input_data  = (__fp16 *)input->data;
    __fp16 *output_data = (__fp16 *)output->data;

    int batch = input->dim[0];
    int in_c  = input->dim[1];
    int in_h  = input->dim[2];
    int in_w  = input->dim[3];
    int input_size = in_c * in_h * in_w;

    int out_h = output->dim[2];
    int out_w = output->dim[3];
    int output_size = in_c * out_h * out_w;

    int out_w4 = (out_w - 2) >> 2;
    __fp16 ratio = 0.11111f;

    for(int b = 0; b < batch; b++) {

        for(int c = 0; c < in_c; c++) {

            const __fp16 *line1 = input_data + c * in_h * in_w;
            const __fp16 *line2 = line1 + in_w;
            __fp16 *outptr = output_data + c * out_h * out_w;
            // h top ---- w left
            ratio = (params->count_include_pad) ? 0.11111f : 0.25f;
            outptr[0] = (line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
            outptr++;
            // h top ---- w mid
            ratio = (params->count_include_pad) ? 0.11111f : 0.16667f;
            asm volatile(
                "addi           t3, %3, -2\n\t"     // t3 = out_w - 2
                "srai           t0, t3, 3\n\t"      // t0 = (out_w - 2) >> 3 (out_w8)
                "beqz           t0, 2f\n\t"
                "vsetvli        zero, zero, e16, m1\n\t"

            "1:\n\t"
                "vle.v          v0, (%0)\n\t"
                "addi           %0, %0, 2\n\t"
                "vle.v          v1, (%0)\n\t"
                "addi           %0, %0, 2\n\t"
                "vle.v          v2, (%0)\n\t"
                "addi           %0, %0, 12\n\t"
                "vfadd.vv       v3, v0, v1\n\t"
                "vfadd.vv       v4, v2, v3\n\t"

                "vle.v          v5, (%1)\n\t"
                "addi           %1, %1, 2\n\t"
                "vle.v          v6, (%1)\n\t"
                "addi           %1, %1, 2\n\t"
                "vle.v          v7, (%1)\n\t"
                "addi           %1, %1, 12\n\t"
                "vfadd.vv       v8, v5, v6\n\t"
                "vfadd.vv       v9, v7, v8\n\t"

                "vfadd.vv       v10, v4, v9\n\t"
                "vfmul.vf       v11, v10, %8\n\t"
                "vse.v          v11, (%2)\n\t"
                "addi           %2, %2, 16\n\t"

                "addi           t0, t0, -1\n\t"
                "bnez           t0, 1b\n\t"

            "2:\n\t"
                "andi           t0, t3, 7\n\t"
                "beqz           t0, 3f\n\t"

                // out_w_tail
                "vsetvli        zero, t0, e16, m1\n\t"
                "slli           t1, t0, 1\n\t"
                "addi           t2, t1, -4\n\t"

                "vle.v          v0, (%0)\n\t"
                "addi           %0, %0, 2\n\t"
                "vle.v          v1, (%0)\n\t"
                "addi           %0, %0, 2\n\t"
                "vle.v          v2, (%0)\n\t"
                "add            %0, %0, t2\n\t"
                "vfadd.vv       v3, v0, v1\n\t"
                "vfadd.vv       v4, v2, v3\n\t"

                "vle.v          v5, (%1)\n\t"
                "addi           %1, %1, 2\n\t"
                "vle.v          v6, (%1)\n\t"
                "addi           %1, %1, 2\n\t"
                "vle.v          v7, (%1)\n\t"
                "add            %1, %1, t2\n\t"
                "vfadd.vv       v8, v5, v6\n\t"
                "vfadd.vv       v9, v7, v8\n\t"

                "vfadd.vv       v10, v4, v9\n\t"
                "vfmul.vf       v11, v10, %8\n\t"
                "vse.v          v11, (%2)\n\t"
                "add            %2, %2, t1\n\t"

            "3:\n\t"

                :"=r"(line1),   // %0
                "=r"(line2),    // %1
                "=r"(outptr),   // %2
                "=r"(out_w)     // %3
                :"0"(line1),
                "1"(line2),
                "2"(outptr),
                "3"(out_w),
                "f"(ratio)      // %8
                :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11",
                 "t0", "t1", "t2", "t3"
            );
            // h top ---- w right
            ratio = (params->count_include_pad) ? 0.11111f : 0.25f;
            outptr[0] = (line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
            outptr++;
            line1 += 2;     // bump next line: line1 --> line2
            line2 += 2;

            // h mid
            const __fp16 *line0 = input_data + c * in_h * in_w;
            for(int h = 0; h < out_h - 2; h++) {

                // h mid ---- w left
                ratio = (params->count_include_pad) ? 0.11111f : 0.16667f;
                outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
                outptr++;
                // h mid ---- w mid
                ratio = 0.11111f;
                asm volatile(
                    "addi           t3, %4, -2\n\t"     // t3 = out_w - 2
                    "srai           t0, t3, 3\n\t"      // t0 = (out_w - 2) >> 3 (out_w8)
                    "beqz           t0, 2f\n\t"
                    "vsetvli        zero, zero, e16, m1\n\t"

                "1:\n\t"
                    "vle.v          v0, (%0)\n\t"
                    "addi           %0, %0, 2\n\t"
                    "vle.v          v1, (%0)\n\t"
                    "addi           %0, %0, 2\n\t"
                    "vle.v          v2, (%0)\n\t"
                    "addi           %0, %0, 12\n\t"
                    "vfadd.vv       v3, v0, v1\n\t"
                    "vfadd.vv       v4, v2, v3\n\t"

                    "vle.v          v5, (%1)\n\t"
                    "addi           %1, %1, 2\n\t"
                    "vle.v          v6, (%1)\n\t"
                    "addi           %1, %1, 2\n\t"
                    "vle.v          v7, (%1)\n\t"
                    "addi           %1, %1, 12\n\t"
                    "vfadd.vv       v8, v5, v6\n\t"
                    "vfadd.vv       v9, v7, v8\n\t"

                    "vle.v          v10, (%2)\n\t"
                    "addi           %2, %2, 2\n\t"
                    "vle.v          v11, (%2)\n\t"
                    "addi           %2, %2, 2\n\t"
                    "vle.v          v12, (%2)\n\t"
                    "addi           %2, %2, 12\n\t"
                    "vfadd.vv       v13, v10, v11\n\t"
                    "vfadd.vv       v14, v12, v13\n\t"

                    "vfadd.vv       v15, v4, v9\n\t"
                    "vfadd.vv       v15, v14, v15\n\t"
                    "vfmul.vf       v16, v15, %10\n\t"
                    "vse.v          v16, (%3)\n\t"
                    "addi           %3, %3, 16\n\t"

                    "addi           t0, t0, -1\n\t"
                    "bnez           t0, 1b\n\t"

                "2:\n\t"
                    "andi           t0, t3, 7\n\t"
                    "beqz           t0, 3f\n\t"

                    // out_w_tail
                    "vsetvli        zero, t0, e16, m1\n\t"
                    "slli           t1, t0, 1\n\t"
                    "addi           t2, t1, -4\n\t"

                    "vle.v          v0, (%0)\n\t"
                    "addi           %0, %0, 2\n\t"
                    "vle.v          v1, (%0)\n\t"
                    "addi           %0, %0, 2\n\t"
                    "vle.v          v2, (%0)\n\t"
                    "add            %0, %0, t2\n\t"
                    "vfadd.vv       v3, v0, v1\n\t"
                    "vfadd.vv       v4, v2, v3\n\t"

                    "vle.v          v5, (%1)\n\t"
                    "addi           %1, %1, 2\n\t"
                    "vle.v          v6, (%1)\n\t"
                    "addi           %1, %1, 2\n\t"
                    "vle.v          v7, (%1)\n\t"
                    "add            %1, %1, t2\n\t"
                    "vfadd.vv       v8, v5, v6\n\t"
                    "vfadd.vv       v9, v7, v8\n\t"

                    "vle.v          v10, (%2)\n\t"
                    "addi           %2, %2, 2\n\t"
                    "vle.v          v11, (%2)\n\t"
                    "addi           %2, %2, 2\n\t"
                    "vle.v          v12, (%2)\n\t"
                    "add            %2, %2, t2\n\t"
                    "vfadd.vv       v13, v10, v11\n\t"
                    "vfadd.vv       v14, v12, v13\n\t"

                    "vfadd.vv       v15, v4, v9\n\t"
                    "vfadd.vv       v15, v14, v15\n\t"
                    "vfmul.vf       v16, v15, %10\n\t"
                    "vse.v          v16, (%3)\n\t"
                    "add            %3, %3, t1\n\t"

                "3:\n\t"

                    :"=r"(line0),   // %0
                    "=r"(line1),    // %1
                    "=r"(line2),    // %2
                    "=r"(outptr),   // %3
                    "=r"(out_w)     // %4
                    :"0"(line0),
                    "1"(line1),
                    "2"(line2),
                    "3"(outptr),
                    "4"(out_w),
                    "f"(ratio)      // %10
                    :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16",
                     "t0", "t1", "t2", "t3"
                );
                // h mid ---- w right
                ratio = (params->count_include_pad) ? 0.11111f : 0.16667f;
                outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1] + line2[0] + line2[1]) * ratio;
                outptr++;
                line0 += 2;
                line1 += 2;
                line2 += 2;
            }
            // h bottom ---- w left
            ratio = (params->count_include_pad) ? 0.11111f : 0.25f;
            outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
            outptr++;
            // h bottom ---- w mid
            ratio = (params->count_include_pad) ? 0.11111f : 0.16667f;

            asm volatile(
                "addi           t3, %3, -2\n\t"     // t3 = out_w - 2
                "srai           t0, t3, 3\n\t"      // t0 = (out_w - 2) >> 3 (out_w8)
                "beqz           t0, 2f\n\t"
                "vsetvli        zero, zero, e16, m1\n\t"

            "1:\n\t"
                "vle.v          v0, (%0)\n\t"
                "addi           %0, %0, 2\n\t"
                "vle.v          v1, (%0)\n\t"
                "addi           %0, %0, 2\n\t"
                "vle.v          v2, (%0)\n\t"
                "addi           %0, %0, 12\n\t"
                "vfadd.vv       v3, v0, v1\n\t"
                "vfadd.vv       v4, v2, v3\n\t"

                "vle.v          v5, (%1)\n\t"
                "addi           %1, %1, 2\n\t"
                "vle.v          v6, (%1)\n\t"
                "addi           %1, %1, 2\n\t"
                "vle.v          v7, (%1)\n\t"
                "addi           %1, %1, 12\n\t"
                "vfadd.vv       v8, v5, v6\n\t"
                "vfadd.vv       v9, v7, v8\n\t"

                "vfadd.vv       v10, v4, v9\n\t"
                "vfmul.vf       v11, v10, %8\n\t"
                "vse.v          v11, (%2)\n\t"
                "addi           %2, %2, 16\n\t"

                "addi           t0, t0, -1\n\t"
                "bnez           t0, 1b\n\t"

            "2:\n\t"
                "andi           t0, t3, 7\n\t"
                "beqz           t0, 3f\n\t"

                // out_w_tail
                "vsetvli        zero, t0, e16, m1\n\t"
                "slli           t1, t0, 1\n\t"
                "addi           t2, t1, -4\n\t"

                "vle.v          v0, (%0)\n\t"
                "addi           %0, %0, 2\n\t"
                "vle.v          v1, (%0)\n\t"
                "addi           %0, %0, 2\n\t"
                "vle.v          v2, (%0)\n\t"
                "add            %0, %0, t2\n\t"
                "vfadd.vv       v3, v0, v1\n\t"
                "vfadd.vv       v4, v2, v3\n\t"

                "vle.v          v5, (%1)\n\t"
                "addi           %1, %1, 2\n\t"
                "vle.v          v6, (%1)\n\t"
                "addi           %1, %1, 2\n\t"
                "vle.v          v7, (%1)\n\t"
                "add            %1, %1, t2\n\t"
                "vfadd.vv       v8, v5, v6\n\t"
                "vfadd.vv       v9, v7, v8\n\t"

                "vfadd.vv       v10, v4, v9\n\t"
                "vfmul.vf       v11, v10, %8\n\t"
                "vse.v          v11, (%2)\n\t"
                "add            %2, %2, t1\n\t"

            "3:\n\t"

                :"=r"(line0),   // %0
                "=r"(line1),    // %1
                "=r"(outptr),   // %2
                "=r"(out_w)     // %3
                :"0"(line0),
                "1"(line1),
                "2"(outptr),
                "3"(out_w),
                "f"(ratio)      // %8
                :"cc", "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11",
                 "t0", "t1", "t2", "t3"
            );
            // h bottom ---- w right
            ratio = (params->count_include_pad) ? 0.11111f : 0.25f;
            outptr[0] = (line0[0] + line0[1] + line1[0] + line1[1]) * ratio;
        }
        input_data  += input_size;
        output_data += output_size;
    }
    return CSINN_TRUE;
}


int csi_c906_avgpool2d_init(struct csi_tensor *input,
                            struct csi_tensor *output,
                            struct pool_params *params)
{
    int32_t input_h = input->dim[2];
    int32_t input_w = input->dim[3];

    int32_t kernel_h = params->filter_height;
    int32_t kernel_w = params->filter_width;
    int32_t stride_h = params->stride_height;
    int32_t stride_w = params->stride_width;

    int32_t pad_left  = params->pad_left;
    int32_t pad_right = params->pad_right;
    int32_t pad_top   = params->pad_top;
    int32_t pad_down  = params->pad_down;

    params->base.bc = NULL;

    // global avgpool2d
    if (input_h == kernel_h && input_w == kernel_w) {
        if (input->dtype == CSINN_DTYPE_FLOAT32) {
            params->base.bc = csi_c906_global_avgpool2d_f32;
        } else if (input->dtype == CSINN_DTYPE_FLOAT16) {
            params->base.bc = csi_c906_global_avgpool2d_fp16;
        }
        return CSINN_TRUE;
    }

    if (stride_h == 2 && stride_w == 2) {
        if (kernel_h == 2 && kernel_w == 2) {
            if (pad_left == 0 && pad_top == 0) {
                // adjust pad according to ceil_mode (ceil mode on caffe pytorch..)
                if (input_h % 2 == 1 && params->ceil_mode == 1) {
                    if (params->pad_down)   params->pad_down++;
                }
                if (input_w % 2 == 1 && params->ceil_mode == 1) {
                    if (params->pad_right)  params->pad_right++;
                }
                // end consider ceil_mode 2x2s2p0

                if (input->dtype == CSINN_DTYPE_FLOAT32) {
                    params->base.bc = avgpool2x2s2;
                } else if (input->dtype == CSINN_DTYPE_FLOAT16) {
                    params->base.bc = avgpool2x2s2_fp16;
                }
            } else if (pad_left == 1 && pad_top == 1) {
                if (input->dtype == CSINN_DTYPE_FLOAT32) {
                    params->base.bc = avgpool2x2s2_p1;
                } else if (input->dtype == CSINN_DTYPE_FLOAT16) {
                    params->base.bc = avgpool2x2s2_p1_fp16;
                }
            }
        } else if (kernel_h == 3 && kernel_w == 3) {
            if (pad_left == 0 && pad_top == 0) {
                // adjust pad according to ceil_mode (ceil mode on caffe pytorch..)
                if (input_h % 2 == 0 && params->ceil_mode == 1) {
                    if (params->pad_down)   params->pad_down++;     // origin pad_down mast be equal to zero ?
                }
                if (input_w % 2 == 0 && params->ceil_mode == 1) {
                    if (params->pad_right)  params->pad_right++;
                }
                // end consider ceil_mode 3x3s2p0

                if (input->dtype == CSINN_DTYPE_FLOAT32) {
                    params->base.bc = avgpool3x3s2;
                } else if (input->dtype == CSINN_DTYPE_FLOAT16) {
                    params->base.bc = avgpool3x3s2_fp16;
                }
            } else if (pad_left == 1 && pad_top == 1) {
                if (input->dtype == CSINN_DTYPE_FLOAT32) {
                    params->base.bc = avgpool3x3s2_p1;
                } else if (input->dtype == CSINN_DTYPE_FLOAT16) {
                    params->base.bc = avgpool3x3s2_p1_fp16;
                }
            }
        }
    } else if (stride_h == 1 && stride_w == 1) {
        if (kernel_h == 3 && kernel_w == 3) {
            if (pad_left == 1 && pad_top == 1 && pad_right == 1 && pad_down == 1) {
                if (input->dtype == CSINN_DTYPE_FLOAT32) {
                    params->base.bc = avgpool3x3s1_p1;
                } else if (input->dtype == CSINN_DTYPE_FLOAT16) {
                    params->base.bc = avgpool3x3s1_p1_fp16;
                }
            }
        }
    }

    if (params->base.bc == NULL) {
        csi_debug_warning("avgpool is not optimized to achieve under this condition on C906, call reference func replaced.\n");
        if (input->dtype == CSINN_DTYPE_FLOAT32) {
            params->base.bc = csi_ref_avgpool2d_f32;
        } else if (input->dtype == CSINN_DTYPE_FLOAT16) {
            params->base.bc = csi_ref_avgpool2d_quant;
        }
    }
    return CSINN_TRUE;
}
