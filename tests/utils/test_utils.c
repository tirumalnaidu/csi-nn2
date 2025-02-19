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

#include "stdint.h"
#include "stdio.h"
#include "math.h"
#include "float.h"
#include "math_snr.h"
#include "test_utils.h"

int test_number = 0;
int failures = 0;

int done_testing(void)
{
    if (0 < failures) {
		printf("Failed %d tests\n", failures);
		exit(EXIT_FAILURE);
	} else {
		printf("All functions tested sucessfully\n");
		exit(EXIT_SUCCESS);
	}
	return failures;
}

void init_testsuite(const char* testname)
{
	printf("%s", testname);
	test_number = 0;
	failures = 0;
}


int *read_input_data_f32(char *path)
{
    FILE *fp = fopen(path, "rb");
    if(fp == NULL) {
        printf("Invalid input file: %s\n", path);
        return NULL;
    }

    int size;
    fread(&size, 4, 1, fp);

    int *buffer = malloc(size* sizeof(int));
    if(buffer == NULL) {
        printf("Malloc fail.\n");
        return NULL;
    }

    fread(buffer, 4, size, fp);

    fclose(fp);
    return buffer;
}

char *read_input_data_fp16(char *path, int int_size)
{
    FILE *fp = fopen(path, "rb");
    if(fp == NULL) {
        printf("Invalid input file: %s\n", path);
        return NULL;
    }

    int size;
    fread(&size, 4, 1, fp);

    char *buffer = malloc(int_size * 4 + (size - int_size) * 2);

    fread(buffer, 4, int_size, fp);

    fread(buffer+int_size*4, 2, size-int_size, fp);

    fclose(fp);
    return buffer;
}

// calculate Kullback-Leibler divergence
float compute_kl(float *p, float *q, uint32_t size)
{
    float p_sum = 0.0f, q_sum = 0.0f, ret = 0.0f;

    for (int i = 0; i < size; i++) {
        p_sum += fabs(p[i]);
        q_sum += fabs(q[i]);
    }
    float p_tmp = 0.0f, q_tmp = 0.0f;
    for (int i = 0; i < size; i++) {
        p_tmp = fabs(p[i]);
        q_tmp = fabs(q[i]);
        p_tmp = p_tmp / p_sum;
        q_tmp = q_tmp / q_sum;
        if (fabs(p_tmp) <= 1e-9) {
            p_tmp += 1e-9;
        }
        if (fabs(q_tmp) <= 1e-9) {
            q_tmp += 1e-9;
        }
        if (!isnan(p_tmp) && !isnan(q_tmp)) {
            ret += p_tmp * log(p_tmp / q_tmp);
        }
    }
    return ret;
}

// calculate cosine similarity
float compute_cs(float *a, float *b, uint32_t size)
{
    float dot_sum = 0.0;
    float a_norm = 0.0;
    float b_norm = 0.0;
    float res = 0.0;

    for (int i = 0; i < size; i++) {
        dot_sum += (a[i] * b[i]);
        a_norm += (a[i] * a[i]);
        b_norm += (b[i] * b[i]);
    }
    res = dot_sum / (sqrt(a_norm * b_norm));
    return res;
}

void result_verify_int32(int *reference, int *output, int *input, float gap, int size, bool save)
{
    int i;
    float error, snr;

    for (i = 0; i < size; i++) {
        error = abs(reference[i] - output[i]);

        test_number++;
        if (error > gap) {
            failures++;
        }
#ifdef BASIC_DEBUG
        if (error > gap)
        {
          printf("i = %d :%d, %d, %d\n", i, reference[i], output[i], input[i]);
        }
#endif
    }
}

void result_verify_f32(float *reference, float *output, float *input, float gap, int size, bool save)
{
    int i;
    float error, snr;
    float max_error = 0;

    for (i = 0; i < size; i++) {
        if(isinf(reference[i]) && isinf(output[i]) || isnan(reference[i]) && isnan(output[i])){
            error = 0;
        } else {
            error = fabs(reference[i] - output[i]);
            if(error > gap) {
                error = fabs(reference[i] - output[i])/fabs(reference[i] + 1e-9);
            }
        }
        if(error > max_error) {
            max_error = error;
        }

        test_number++;
#ifdef BASIC_DEBUG
        if (error > gap)
        {
          printf("i = %d :%.6f, %.6f, %.6f\n", i, reference[i], output[i], input[i]);
        }
#endif
    }
    printf("The max error is %f\n", max_error);

    float kl = compute_kl(output, reference, size);
    printf("The kl diver is %f.\n", kl);

    float cs = compute_cs(output, reference, size);
    printf("The cos sim is %f.\n", cs);
    if (cs < gap) {
        failures++;
    }
}

#ifdef RISCV_TEST
float compute_cs_fp16(__fp16 *a, __fp16 *b, uint32_t size)
{
    float dot_sum = 0.0;
    float a_norm = 0.0;
    float b_norm = 0.0;
    float res = 0.0;

    for (int i = 0; i < size; i++) {
        dot_sum += (a[i] * b[i]);
        a_norm += (a[i] * a[i]);
        b_norm += (b[i] * b[i]);
    }
    res = dot_sum / (sqrt(a_norm * b_norm));
    return res;
}

void result_verify_fp16(__fp16 *reference, __fp16 *output, __fp16 *input, float gap, int size, bool save)
{
    int i;
    __fp16 error = 0;
    __fp16 max_error = 0;

    for (i = 0; i < size; i++) {
        error = fabs(reference[i] - output[i]);
        if(error > gap) {
            error = fabs(reference[i] - output[i])/fabs(reference[i] + 1e-9);
        }
        if(error > max_error) {
            max_error = error;
        }

        test_number++;
#ifdef BASIC_DEBUG
        if (error > gap)
        {
          printf("i = %d :%.6f, %.6f, %.6f\n", i, reference[i], output[i], input[i]);
        }
#endif
    }
    printf("The max error is %f\n", max_error);

    float cs = compute_cs_fp16(output, reference, size);
    printf("The cos sim is %f.\n", cs);

}
#endif

void result_verify_bool(bool *reference, bool *output, float *input, float gap, int size, bool save)
{
    int i;
    float error, snr;

    for (i = 0; i < size; i++) {
        error = fabs(reference[i] - output[i]);
        if(error > gap) {
            error = fabs(reference[i] - output[i])/fabs(reference[i] + 1e-9);
        }

        test_number++;
        if (error > gap) {
            failures++;
        }
#ifdef BASIC_DEBUG
        if (error > gap)
        {
          printf("i = %d, %d, %.6f\n", i, reference[i], output[i], input[i]);
        }
#endif
    }
}

void result_verify_8(float *reference, struct csi_tensor *output, int8_t *input, float gap, int size, bool save)
{
    int i;
    float error;
    float *output_tmp = malloc(size * sizeof(float));
    void *output_data = output->data;

    float max_error = 0;

    for (i = 0; i < size; i++) {
        if (output->dtype == CSINN_DTYPE_UINT8) {
            output_tmp[i] = csi_ref_dequantize_u8_to_f32(*((uint8_t *)output_data + i), output->qinfo);
        } else if (output->dtype == CSINN_DTYPE_INT8) {
            output_tmp[i] = csi_ref_dequantize_i8_to_f32(*((int8_t *)output_data + i), output->qinfo);
        }
        if(isinf(reference[i]) || isnan(reference[i])){
            error = 0;
        } else {
            error = fabs(reference[i] - output_tmp[i]);
            if(error > gap) {
                error = fabs(reference[i] - output_tmp[i])/fabs(reference[i] + 1e-9);
            }
        }
        if(error > max_error) {
            max_error = error;
        }

        test_number++;
#ifdef BASIC_DEBUG
        if (error > gap)
        {
            printf("i = %d :%.6f, %.6f, %.6f\n", i, reference[i], output_tmp[i], input[i]);
        }
#endif
    }
    printf("The max error is %f\n", max_error);

    float kl = compute_kl(output_tmp, reference, size);
    printf("The kl diver is %f.\n", kl);

    float cs = compute_cs(output_tmp, reference, size);
    printf("The cos sim is %f.\n", cs);
    if (cs < gap) {
        failures++;
    }
    free(output_tmp);
}


void result_verify_q7(int8_t *reference, int8_t *output, int8_t *input, float gap, int size, bool save)
{
    int i;
    float error, snr;

    for (i = 0; i < size; i++) {
        error = abs(reference[i] - output[i]);

        test_number++;
        if (error > gap) {
            failures++;
        }
#ifdef BASIC_DEBUG
        if (error > gap)
        {
          printf("i = %d :%#x, %#x, %#x\n", i, reference[i], output[i], input[i]);
        }
#endif
    }
    printf("/====== total = %6d(size=%5d) || error = %5d =======/\n", test_number, size, failures);
}


void result_verify_q15(int16_t *reference, int16_t *output, int16_t *input, float gap, int size, bool save)
{
    int i;
    float error, snr;

    for (i = 0; i < size; i++) {
        error = abs(reference[i] - output[i]);

        test_number++;
        if (error > gap) {
            failures++;
        }
#ifdef BASIC_DEBUG
        if (error > gap)
        {
          printf("i = %d :%d, %d, %d\n", i, reference[i], output[i], input[i]);
        }
#endif
    }
    printf("/====== total = %6d(size=%5d) || error = %5d =======/\n", test_number, size, failures);
}


void get_scale_and_zp(float max_value, float min_value, float *scale, int *zp)
{
    int valid_range = 255;
    float scale_tmp, zp_tmp;

    max_value = max_value > 0.0f ? max_value : 0.0f;
    min_value = min_value < 0.0f ? min_value : 0.0f;

    scale_tmp = (max_value - min_value) / (float)valid_range;

    if (scale_tmp){
        zp_tmp = 0 - min_value / scale_tmp;
    } else {
        scale_tmp = 1;
        zp_tmp   = max_value;
    }
    zp_tmp = zp_tmp > 255 ? 255 : zp_tmp;
    zp_tmp = zp_tmp < 0 ? 0 : zp_tmp;

    *zp = (int)round(zp_tmp);
    *scale = scale_tmp;
}

void get_scale_and_zp_i8_asym(float max_value, float min_value, float *scale, int *zp)
{
    int valid_range = 255;
    float scale_tmp, zp_tmp;

    max_value = max_value > 0.0f ? max_value : 0.0f;
    min_value = min_value < 0.0f ? min_value : 0.0f;

    scale_tmp = (max_value - min_value) / (float)valid_range;

    if (scale_tmp){
        zp_tmp = -128 - min_value / scale_tmp;
    } else {
        scale_tmp = 1;
        zp_tmp   = 0;
    }

    *zp = (int)round(zp_tmp);
    *scale = scale_tmp;
}

void get_scale_and_zp_i8(float max_value, float min_value, float *scale, int *zp)
{
    int valid_range = 255;
    float scale_tmp, zp_tmp, max_tmp;

    if (fabs(max_value) >= fabs(min_value)){
        max_tmp = fabs(max_value);
    } else{
        max_tmp = fabs(min_value);
    }
    scale_tmp = 2 * max_tmp / (float)valid_range;
    zp_tmp = 0;

    if (scale_tmp == 0){
        scale_tmp = 1;
    }

    *zp = (int)zp_tmp;
    *scale = scale_tmp;
}

void get_scale_and_zp_power2_i8(float max_value, float min_value, float *scale, int *zp)
{
    int valid_range = 255;
    float abs_max = fmax(fabs(min_value), fabs(max_value));
    float ascale = valid_range / abs_max;
    int exponent;
    frexp(ascale, &exponent);

    *zp = (int)0;
    *scale = 1.0f / pow(2, exponent - 1);
}

void get_scale_and_zp_power2_i16(float max_value, float min_value, float *scale, int *zp)
{
    int valid_range = 65535;
    float abs_max = fmax(fabs(min_value), fabs(max_value));
    float ascale = valid_range / abs_max;
    int exponent;
    frexp(ascale, &exponent);

    *zp = (int)0;
    *scale = 1.0f / pow(2, exponent - 1);
}

void find_min_max(float *input, float *max_value, float *min_value, int size)
{
    int i;
    float max_tmp = -FLT_MAX;
    float min_tmp = FLT_MAX;

    for(i = 0; i < size; i++) {
        if(input[i] != -FLT_MAX && input[i] != FLT_MAX) {
            if(input[i] > max_tmp) {
                max_tmp = input[i];
            }
            if(input[i] < min_tmp) {
                min_tmp = input[i];
            }
        }
    }

    *max_value = max_tmp;
    *min_value = min_tmp;
}

void set_quant_info(struct csi_tensor *tensor, enum csinn_quant_enum qtype, enum csinn_api_enum api)
{
    float max, min, scale;
    int zp, quantized_multiplier, shift;
    if (tensor->qinfo == NULL) {
        tensor->qinfo = malloc(sizeof(struct csi_quant_info));
    }
    int size = csi_tensor_size(tensor);
    find_min_max(tensor->data, &max, &min, size);

    if (qtype == CSINN_QUANT_INT8_SYM) {
        if (api == CSINN_LIGHT) {
            get_scale_and_zp_power2_i8(max, min, &scale, &zp);
            if (min >= 0 && max > 0) {
                min = -max;
            } else {
                min = min;
            }
        } else {
            get_scale_and_zp_i8(max, min, &scale, &zp);
        }
    } else if (qtype == CSINN_QUANT_UINT8_ASYM) {
        get_scale_and_zp(max, min, &scale, &zp);
    } else if (qtype == CSINN_QUANT_INT8_ASYM) {
        get_scale_and_zp_i8_asym(max, min, &scale, &zp);
    } else if (qtype == CSINN_QUANT_INT16_SYM) {
        if (api == CSINN_LIGHT) {
            get_scale_and_zp_power2_i16(max, min, &scale, &zp);
            if (min >= 0 && max > 0) {
                min = -max;
            } else {
                min = min;
            }
        } else {
            printf("unsupport qinfo\n");
        }
    } else if (qtype == CSINN_QUANT_FLOAT16) {
        scale = 1;
        zp = 0;
    } else if (qtype == CSINN_QUANT_FLOAT32) {
        scale = 1;
        zp = 0;
    } else {
        printf("unsupport qinfo\n");
    }

    tensor->qinfo->max = max;
    tensor->qinfo->min = min;
    csi_quantize_multiplier(scale, &quantized_multiplier, &shift);
    tensor->qinfo->scale = scale;
    tensor->qinfo->zero_point = zp;
    tensor->qinfo->multiplier = quantized_multiplier;
    tensor->qinfo->shift = shift;
}

void get_quant_info(struct csi_tensor *tensor)
{
    float max, min, scale;
    int zp, quantized_multiplier, shift;
    if (tensor->qinfo == NULL) {
        tensor->qinfo = malloc(sizeof(struct csi_quant_info));
    }
    int size = csi_tensor_size(tensor);
    find_min_max(tensor->data, &max, &min, size);
    if (tensor->sess->base_api == CSINN_LIGHT) {
        get_scale_and_zp_power2_i8(max, min, &scale, &zp);
        tensor->qinfo->max = max;
        if (min >= 0 && max > 0) {
            tensor->qinfo->min = -max;
        } else {
            tensor->qinfo->min = min;
        }
    } else {
        if (tensor->dtype == CSINN_DTYPE_UINT8) {
            get_scale_and_zp(max, min, &scale, &zp);
        } else if (tensor->dtype == CSINN_DTYPE_INT8) {
            get_scale_and_zp_i8(max, min, &scale, &zp);
        }
        tensor->qinfo->max = max;
        tensor->qinfo->min = min;
    }

    csi_quantize_multiplier(scale, &quantized_multiplier, &shift);
    tensor->qinfo->scale = scale;
    tensor->qinfo->zero_point = zp;
    tensor->qinfo->multiplier = quantized_multiplier;
    tensor->qinfo->shift = shift;
}

struct csi_tensor *convert_input(struct csi_tensor *tensor, int dtype)
{
    struct csi_tensor *ret = csi_alloc_tensor(tensor->sess);
    csi_tensor_copy(ret, tensor);
    ret->dtype = dtype;
    ret->data = malloc(csi_tensor_byte_size(ret));
    csi_tensor_data_convert(ret, tensor);

    return ret;
}

struct csi_tensor *convert_f32_input(struct csi_tensor *tensor, int dtype, struct csi_session *sess)
{
    set_quant_info(tensor, sess->base_quant_type, sess->base_api);
    struct csi_tensor *ret = csi_alloc_tensor(sess);
    csi_tensor_copy(ret, tensor);
    ret->sess = sess;
    ret->dtype = dtype;
    ret->data = malloc(csi_tensor_byte_size(ret));
    csi_tensor_data_convert(ret, tensor);

    return ret;
}

void free_input(struct csi_tensor *tensor)
{
    csi_mem_free(tensor->data);
    csi_free_tensor(tensor);
}
