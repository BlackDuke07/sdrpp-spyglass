#pragma once

#include <cstddef>

#define FFTW_FORWARD (-1)
#define FFTW_BACKWARD (+1)
#define FFTW_ESTIMATE (1U << 6)

typedef float fftwf_complex[2];
typedef struct fftwf_plan_s* fftwf_plan;

extern "C" __declspec(dllimport) void* fftwf_malloc(std::size_t n);
extern "C" __declspec(dllimport) void fftwf_free(void* p);
extern "C" __declspec(dllimport) fftwf_plan fftwf_plan_dft_1d(int n, fftwf_complex* in, fftwf_complex* out, int sign, unsigned flags);
extern "C" __declspec(dllimport) void fftwf_execute(const fftwf_plan p);
extern "C" __declspec(dllimport) void fftwf_destroy_plan(fftwf_plan p);
