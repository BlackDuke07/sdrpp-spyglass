#pragma once

#include <cstddef>
#include <cmath>
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

static inline void volk_32fc_s32fc_x2_rotator_32fc(
    lv_32fc_t* out,
    const lv_32fc_t* in,
    lv_32fc_t phase_inc,
    lv_32fc_t* phase,
    unsigned int count
) {
    lv_32fc_t current = *phase;
    for (unsigned int i = 0; i < count; ++i) {
        out[i].re = (in[i].re * current.re) - (in[i].im * current.im);
        out[i].im = (in[i].re * current.im) + (in[i].im * current.re);

        const float next_re = (current.re * phase_inc.re) - (current.im * phase_inc.im);
        const float next_im = (current.re * phase_inc.im) + (current.im * phase_inc.re);
        const float mag = std::sqrt((next_re * next_re) + (next_im * next_im));
        if (mag > 0.0f) {
            current.re = next_re / mag;
            current.im = next_im / mag;
        }
        else {
            current = lv_cmake(1.0f, 0.0f);
        }
    }
    *phase = current;
}

static inline void volk_32f_x2_dot_prod_32f(
    float* result,
    const float* input,
    const float* taps,
    unsigned int count
) {
    float acc = 0.0f;
    for (unsigned int i = 0; i < count; ++i) {
        acc += input[i] * taps[i];
    }
    *result = acc;
}

static inline void volk_32fc_32f_dot_prod_32fc(
    lv_32fc_t* result,
    const lv_32fc_t* input,
    const float* taps,
    unsigned int count
) {
    float acc_re = 0.0f;
    float acc_im = 0.0f;
    for (unsigned int i = 0; i < count; ++i) {
        acc_re += input[i].re * taps[i];
        acc_im += input[i].im * taps[i];
    }
    result->re = acc_re;
    result->im = acc_im;
}

static inline void volk_32fc_x2_dot_prod_32fc(
    lv_32fc_t* result,
    const lv_32fc_t* input,
    const lv_32fc_t* taps,
    unsigned int count
) {
    float acc_re = 0.0f;
    float acc_im = 0.0f;
    for (unsigned int i = 0; i < count; ++i) {
        acc_re += (input[i].re * taps[i].re) - (input[i].im * taps[i].im);
        acc_im += (input[i].re * taps[i].im) + (input[i].im * taps[i].re);
    }
    result->re = acc_re;
    result->im = acc_im;
}
