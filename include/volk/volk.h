#pragma once

#include <cstddef>
#include <malloc.h>

#define VOLK_VERSION 0

typedef struct {
    float re;
    float im;
} lv_32fc_t;

static inline lv_32fc_t lv_cmake(float re, float im) {
    lv_32fc_t out;
    out.re = re;
    out.im = im;
    return out;
}

static inline std::size_t volk_get_alignment() {
    return 32;
}

static inline void* volk_malloc(std::size_t size, std::size_t align) {
    return _aligned_malloc(size, align);
}

static inline void volk_free(void* ptr) {
    _aligned_free(ptr);
}

extern "C" {
void volk_8i_s32f_convert_32f(float* out, const signed char* in, float scalar, unsigned int count);
void volk_16i_s32f_convert_32f(float* out, const short* in, float scalar, unsigned int count);
void volk_32f_s32f_convert_8i(signed char* out, const float* in, float scalar, unsigned int count);
void volk_32f_s32f_convert_16i(short* out, const float* in, float scalar, unsigned int count);
void volk_32f_s32f_convert_32i(int* out, const float* in, float scalar, unsigned int count);
void volk_32f_index_max_32u(unsigned int* index, const float* in, unsigned int count);
void volk_32f_accumulator_s32f(float* result, const float* in, unsigned int count);
void volk_32f_s32f_multiply_32f(float* out, const float* in, float scalar, unsigned int count);
void volk_32f_x2_add_32f(float* out, const float* a, const float* b, unsigned int count);
void volk_32f_x2_subtract_32f(float* out, const float* a, const float* b, unsigned int count);
void volk_32f_x2_multiply_32f(float* out, const float* a, const float* b, unsigned int count);
void volk_32f_x2_dot_prod_32f(float* result, const float* input, const float* taps, unsigned int count);
void volk_32f_x2_interleave_32fc(lv_32fc_t* out, const float* i, const float* q, unsigned int count);
void volk_32fc_conjugate_32fc(lv_32fc_t* out, const lv_32fc_t* in, unsigned int count);
void volk_32fc_deinterleave_real_32f(float* out, const lv_32fc_t* in, unsigned int count);
void volk_32fc_magnitude_32f(float* out, const lv_32fc_t* in, unsigned int count);
void volk_32fc_x2_multiply_32fc(lv_32fc_t* out, const lv_32fc_t* a, const lv_32fc_t* b, unsigned int count);
void volk_32fc_32f_multiply_32fc(lv_32fc_t* out, const lv_32fc_t* in, const float* scale, unsigned int count);
void volk_32fc_s32f_power_spectrum_32f(float* out, const lv_32fc_t* in, float normalizationFactor, unsigned int count);
void volk_32fc_s32fc_x2_rotator_32fc(lv_32fc_t* out, const lv_32fc_t* in, lv_32fc_t phase_inc, lv_32fc_t* phase, unsigned int count);
void volk_32fc_s32fc_x2_rotator2_32fc(lv_32fc_t* out, const lv_32fc_t* in, const lv_32fc_t* phase_inc, lv_32fc_t* phase, unsigned int count);
void volk_32fc_32f_dot_prod_32fc(lv_32fc_t* result, const lv_32fc_t* input, const float* taps, unsigned int count);
void volk_32fc_x2_dot_prod_32fc(lv_32fc_t* result, const lv_32fc_t* input, const lv_32fc_t* taps, unsigned int count);
}
