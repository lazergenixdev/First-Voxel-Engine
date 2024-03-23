#define RAIN 0

#ifdef __cplusplus // dont want this stuff in shaders
#pragma once
#include <cstdint>

#define SQ(X) (X*X)
#define MAP2D(X,Y,WIDTH) (Y * WIDTH + X)

inline int64_t total_vertex_gpu_memory = 0;
inline int64_t used_vertex_gpu_memory  = 0;
inline int64_t total_number_of_quads   = 0;

#define SIZE_MB(X) double(X)/double(1024*1024)

#define CHUNK_SIZE  64
#define THREAD_COUNT 8

#define CPU_SIDE_BACKFACE_CULLING 1

#define FREE_FLY 1

#endif
