#include <Fission/Core/Engine.hh>
#include "gfx.h"
#include "Camera_Controller.h"
#include <stb_perlin.h>
#include <unordered_map>
#include <glm/ext/vector_common.hpp>
#include "blend_technique.hpp"
#include "Skybox.h"
#include "job_queue.hpp"
#include "config.hpp"

static constexpr int max_vertex_count_per_chunk = 1 << 14;

template <typename T>
struct r3 {
	using type = T;
	using vec  = fs::v3<type>;

	fs::range<type> x, y, z;

	static constexpr vec oct_mask[] = {
		{0,0,0},
		{0,0,1},
		{0,1,0},
		{0,1,1},
		{1,0,0},
		{1,0,1},
		{1,1,0},
		{1,1,1},
	};

	vec constexpr size() const { return vec(x.high - x.low, y.high - y.low, z.high - z.low); }

	static r3 constexpr from_point_size(vec point, vec size) {
		return r3{ .x = {point.x,point.x + size.x}, .y = {point.y,point.y+size.y}, .z = {point.z,point.z+size.z} };
	}
	static r3 constexpr from_center(vec h) {
		return r3{ .x = {-h.x,h.x}, .y = {-h.y,h.y}, .z = {-h.z,h.z} };
	}

	vec corner(int index) const { return vec(x.low, y.low, z.low) + size() * oct_mask[index]; }
	vec pos() const { return vec(x.low, y.low, z.low); }
};

using r3i = r3<int>;

template <typename T>
r3<T> octant(r3<T> const& r, int index) {
	auto half = r.size() / T(2);

	return r3<T>::from_point_size(
		r3<T>::vec(r.x.low, r.y.low, r.z.low) + half * r3<T>::oct_mask[index],
		half
	);
}

struct Vertex {
	fs::v3f32 position;
	float     palette;

	static constexpr float d = (1 << 8);

	Vertex() = default;
	Vertex(float x, float y, float z): position(x,y,z), palette(color_from_position(x,y,z)) {}
	Vertex(float x, float y, float z, float p): position(x,y,z), palette(p) {}
	
	Vertex(int x, int y, int z, fs::u8 p) : position(float(x), float(y), float(z)), palette(float(p)/255.0f) {}

	static float color_from_position(float x, float y, float z) {
	//	return stb_perlin_noise3(x / d, y / d, z / d, 0, 0, 0) + 0.6f;
		return stb_perlin_noise3(2.0f*x / d, 2.0f*y / d, 2.0f*z / d, 0, 0, 0) + 0.6f;
	//	return y/256.0f;
	}
};

struct _Quad {
	fs::u8 w[4];
};

struct Quad {
	fs::u8 x, y, z; // normal is in top bit
	fs::u8 l0, l1;
	fs::u8 palette;
};

static constexpr auto normal_index(Quad const& q) -> int {
	return ((q.x >> 5)&0b100) | ((q.y >> 6)&0b10) | (q.z >> 7);
}

namespace Normal {
	enum Normal {
		Pos_X = 3,
		Neg_X = 4,
		Pos_Y = 5,
		Neg_Y = 2,
		Pos_Z = 6,
		Neg_Z = 1,
	};

	static constexpr Quad quad_px = { .x = 0x00, .y = 0x80, .z = 0x80, .l0 = 0, .l1 = 0, .palette = 0 };
	static constexpr Quad quad_nx = { .x = 0x80, .y = 0x00, .z = 0x00, .l0 = 0, .l1 = 0, .palette = 0 };
	static constexpr Quad quad_py = { .x = 0x80, .y = 0x00, .z = 0x80, .l0 = 0, .l1 = 0, .palette = 0 };
	static constexpr Quad quad_ny = { .x = 0x00, .y = 0x80, .z = 0x00, .l0 = 0, .l1 = 0, .palette = 0 };
	static constexpr Quad quad_pz = { .x = 0x80, .y = 0x80, .z = 0x00, .l0 = 0, .l1 = 0, .palette = 0 };
	static constexpr Quad quad_nz = { .x = 0x00, .y = 0x00, .z = 0x80, .l0 = 0, .l1 = 0, .palette = 0 };
}

// chunk size: 128x128x128
struct Chunk {
	fs::u32 index_count;
	fs::u32 vertex_offset;
	fs::v3s32 position;
	int lod = 0;
	std::vector<Quad> quads;
};

/*
inline int max_vertex_count = 0;

struct Chunk_ID {
	fs::v3s32 pos;
	int lod;
	
	constexpr size_t hash() const {
		return 8*(pos.z * 1024*1024 + pos.y*1024 + pos.x) + lod;
	}
	constexpr bool operator==(Chunk_ID const&) const = default;
};

template <>
struct std::hash<Chunk_ID> {
	_NODISCARD size_t operator()(Chunk_ID const& _Keyval) const noexcept {
		return _Keyval.hash();
	}
};

float sdBox(glm::vec3 p, glm::vec3 b) {
	glm::vec3 q = abs(p) - b;
	return glm::length(glm::max(q, 0.0f)) + glm::min(glm::max(q.x, glm::max(q.y, q.z)), 0.0f);
}

struct World {
	using Node = LOD_Tree;

	int max_lod = 12;
	float render_distance = float((1) << max_lod);
	Node* root;

	fs::v3f32 center = {};

	Node* start;
	Node* next;
	Node* end;

	auto build_lod_tree(r3i d, int height) -> Node* {
		auto node = new(next++) Node(0);
		node->d = d;
		float x0 = fs::lerp(float(d.x.low), float(d.x.high), 0.5f);
		float y0 = fs::lerp(float(d.y.low), float(d.y.high), 0.5f);
		float z0 = fs::lerp(float(d.z.low), float(d.z.high), 0.5f);
		float dist = sdBox(
			glm::vec3(center.x,center.y,center.z)
		-	glm::vec3(x0,y0,z0)
			, glm::vec3(
			float(d.x.difference()) * 0.5f,
			float(d.y.difference()) * 0.5f,
			float(d.z.difference()) * 0.5f
		));

		if ((lod_from_distance(dist) > height && height < max_lod)
			//	|| height < 3
			) {
			FS_FOR(8) {
				auto g = octant(d, i);
				node->octant[i] = build_lod_tree(g, height + 1);
			}
			//	node->lod = height;
		}
		else node->lod = height;
		return node;
	};

	World() {
		auto N = 1 << 16;
		start = (Node*)malloc(sizeof(Node) * N);
		end = start + N;
	}

	void generate_lod_tree() {
		next = start;
		auto rd = int(render_distance);
		auto r = r3i::from_center(r3i::vec(rd));
		root = new(next++) Node(0);
		FS_FOR(8) root->octant[i] = build_lod_tree(octant(r,i), 1);
	}

	auto lod_from_distance(float dist) -> int {
	//	return std::clamp(int(8 - (dist - 64.0f)/300.0f), 2, 8);
	//	return std::clamp(int(7 - (dist - 64.0f)/600.0f), 2, 9);

		float s = 64.0f, d = 256.0f;
		if (dist < s)      return 8;
		if (dist < s+d)    return 7;
		if (dist < s+d*2)  return 6;
		if (dist < s+d*4)  return 5;
		if (dist < s+d*8)  return 4;
		if (dist < s+d*16) return 3;
		if (dist < s+d*32) return 2;
		                   return 1;
	}
};

int cache_misses = 0;

void recursive_gen(World::Node* node, int height) {
	if (node == nullptr) return;
	if (node->lod != 0) {
		auto& chunk = loaded_chunks[Chunk_ID(node->d.pos(), node->lod)];
		chunk.status = 1;
		if (chunk.vertex_buffer == -1) {
			if (free_vertex_buffers.empty()) return;

			auto next_vertex_buffer = free_vertex_buffers.back();
			free_vertex_buffers.pop_back();
			chunk.vertex_buffer = next_vertex_buffer;
			vertex_buffers[next_vertex_buffer].index_count = 0;

			Chunk_Gen_Info info;
			info.chunk = chunk;
			info.lod = node->lod;
			info.rect = node->d;
			job_q.add_work(std::move(info));

			cache_misses += 1;
		}
	}
	else {
		FS_FOR(8)
			recursive_gen(node->octant[i], height + 1);
	}
}

void generate_meshes_from_world(World const& w) {
	cache_misses = 0;
	recursive_gen(w.root, 1);
}
*/