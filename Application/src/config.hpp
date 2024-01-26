#define RAIN 0

#ifdef __cplusplus // dont want this stuff in shaders
#pragma once
#include <cstdint>

#define SQ(X) (X*X)
#define MAP2D(X,Y,WIDTH) (Y * WIDTH + X)

static constexpr int world_chunk_height = 16;

inline int64_t total_vertex_gpu_memory = 0;
inline int64_t used_vertex_gpu_memory  = 0;
inline int64_t total_number_of_quads   = 0;

inline int render_chunk_radius = RAIN?12:48;
#endif
