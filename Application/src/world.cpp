#include "world.hpp"
#include <Fission/Base/Time.hpp>
#include <intrin.h>

#define CHUNK_128   0

using Mask_Type = fs::if_t<CHUNK_128, __m128i, fs::u8>;

volatile fs::s64 World::total_quad_memory = 0;

auto height_from_xz(int x, int z) -> float {
	float h =
//	36.0f + 32.0f * stb_perlin_noise3(float(x)/2e2f, 0.0f, float(z)/2e2f, 0, 0, 0)
//	64.0f + 80.0f * stb_perlin_noise3(float(x)/5e2f, 0.0f, float(z)/5e2f, 0, 0, 0)
//	128.0f + 80.0f * stb_perlin_fbm_noise3(float(x)/1e3f, 0.0f, float(z)/1e3f, 2.0f, 0.5f, 6)
//	64.0f - 80.0f * stb_perlin_turbulence_noise3(float(x)/5e2f, 0.0f, float(z)/5e2f, 2.0f, 0.5f, 6)
	64.0f - 256.0f * stb_perlin_turbulence_noise3(float(x)/3e3f, 0.0f, float(z)/3e3f, 2.0f, 0.5f, 6)

//	+ 5.e3f * stb_perlin_turbulence_noise3(float(x)/1e5f, 0.74352f, float(z)/1e5f, 2.0f, 0.5f, 4)
//	- 2.4e3f

	+ 5.e4f * stb_perlin_turbulence_noise3(float(x)/6e5f, -0.4352f, float(z)/6e5f, 2.0f, 0.5f, 10)
	- 2.5e4f
	;
//	if (h < -3600.0f) return -3600.0f;
	return h;
}

namespace mesher {
	static constexpr int S = CHUNK_SIZE;

	auto bit_set(__m128i const& m, fs::u64 i) {
		return m.m128i_u64[i >> 6] & (1ui64 << (i & 63));
	}
	auto set_bit(__m128i & m, int i, fs::u64 value = 1) {
		return m.m128i_u64[i >> 6] |= (value << (i&63));
	}

	inline auto lzcnt_u128(__m128i const& u) -> int {
		uint64_t hi = u.m128i_u64[1];
		uint64_t lo = u.m128i_u64[0];
		lo = (hi == 0) ? lo : -1ULL;
		return _lzcnt_u64(hi) + _lzcnt_u64(lo);
	}

	auto scan_quad(Mask_Type m[], int x0, int y0) -> fs::v2s32 {
		int w = 0;
		int h = 0;
		
#if CHUNK_128
		for (int x = x0 + 1; x < S; ++x) {
			if (!bit_set(m[y0], x)) break;
			++w;
		}
		for (int y = y0 + 1; y < S; ++y) {
			for (int x = x0; x <= x0 + w; ++x) {
				if (!bit_set(m[y], x)) goto done;
			}
			++h;
		}
#else
		for (int x = x0 + 1; x < S; ++x) {
			if (!m[y0*S + x]) break;
			++w;
		}
		for (int y = y0 + 1; y < S; ++y) {
			for (int x = x0; x <= x0 + w; ++x) {
				if (!m[y*S + x]) goto done;
			}
			++h;
		}
#endif
		done:
		return {x0 + w, y0 + h};
	}
		
	auto generate_quads_for_slice(Mask_Type mask[], std::vector<Quad>& quads, Quad normal, char i, char j) -> void {
		for (int y0 = 0; y0 < S; ++y0)
		for (int x0 = 0; x0 < S; ) {
#if CHUNK_128
			if (!bit_set(mask[y0], x0)) {
				++x0;
				continue;
			}

			auto [x1, y1] = scan_quad(mask, x0, y0);

			Quad q = normal;
			{
				_Quad& _q = reinterpret_cast<_Quad&>(q);
				_q.w[i] |= x0;
				_q.w[j] |= y0;
			}
			q.l0 = x1 - x0 + 1;
			q.l1 = y1 - y0 + 1;

			quads.emplace_back(q);
			
			__m128i xmask = _mm_setzero_si128();
			for (int x = x0; x <= x1; ++x) {
				set_bit(xmask, x);
			}
			for (int y = y0; y <= y1; ++y) {
				mask[y] = _mm_andnot_si128(xmask, mask[y]);
			}

			x0 = x1 + 1;
#else
			if (!mask[y0*S + x0]) {
				++x0;
				continue;
			}

			auto [x1, y1] = scan_quad(mask, x0, y0);

			Quad q = normal;
			{
				_Quad& _q = reinterpret_cast<_Quad&>(q);
				_q.w[i] |= x0;
				_q.w[j] |= y0;
			}
			q.l0 = x1 - x0 + 1;
			q.l1 = y1 - y0 + 1;

			quads.emplace_back(q);

			for (int x = x0; x <= x1; ++x)
			for (int y = y0; y <= y1; ++y) {
				mask[y*S + x] = false;
			}

			x0 = x1 + 1;
#endif
		}
	}
}


static Mask_Type* mask       [THREAD_COUNT];
static float* height_field   [THREAD_COUNT];
static float* height_field_pz[THREAD_COUNT];
static float* height_field_nx[THREAD_COUNT];
static float* height_field_nz[THREAD_COUNT];
static float* height_field_px[THREAD_COUNT];

static double world_gen_time = 0.0;

#define Mask            (mask           [thread_index])
#define Height_field    (height_field   [thread_index])
#define Height_field_px (height_field_px[thread_index])
#define Height_field_nx (height_field_nx[thread_index])
#define Height_field_pz (height_field_pz[thread_index])
#define Height_field_nz (height_field_nz[thread_index])

auto generate_chunk(Chunk& chunk, int size, int thread_index) -> void {
	auto const cx = chunk.position.x;
	auto const cz = chunk.position.z;
	auto const scale = 1 << chunk.lod;
	
//	auto _start = fs::timestamp();
	float min = 1e10f, max = -1e10f;
	for (int z = 0; z < size; ++z)
	for (int x = 0; x < size; ++x) {
		float y = height_from_xz(x*scale + cx, z*scale + cz);
		if (y < min) min = y;
		if (y > max) max = y;
		Height_field[z*size + x] = y;
	}
	
	for (int z = 0; z < size; ++z) {
		int x = -1;
		float y = height_from_xz(x*scale + cx, z*scale + cz);
		if (y < min) min = y;
		if (y > max) max = y;
		Height_field_nx[z] = y;
	}
	for (int z = 0; z < size; ++z) {
		int x = size;
		float y = height_from_xz(x*scale + cx, z*scale + cz);
		if (y < min) min = y;
		if (y > max) max = y;
		Height_field_px[z] = y;
	}
	for (int x = 0; x < size; ++x) {
		int z = size;
		float y = height_from_xz(x*scale + cx, z*scale + cz);
		if (y < min) min = y;
		if (y > max) max = y;
		Height_field_pz[x] = y;
	}
	for (int x = 0; x < size; ++x) {
		int z = -1;
		float y = height_from_xz(x*scale + cx, z*scale + cz);
		if (y < min) min = y;
		if (y > max) max = y;
		Height_field_nz[x] = y;
	}

	int cy = int(min);
	if (scale != 1) {
		cy = (cy/scale)*scale;	
	}
	chunk.position.y = cy;

	int ycount = (int(max)/scale - int(min)/scale) + 1;
	if (ycount >= 128) {
		__debugbreak();
	}

	int Min = 10000, Max = -10000;
	for (int z = 0; z < size; ++z)
	for (int x = 0; x < size; ++x) {
		float y = Height_field[z*size + x];
		int iy = (int(y)/scale)*scale; 
		int g = (iy-cy)/scale;
		if (g < Min) Min = g;
		if (g > Max) Max = g;
	}
//	world_gen_time = fs::seconds_elasped(_start, fs::timestamp());
	for (int slice = Min; slice <= Max; ++slice) {
		for (int z = 0; z < size; ++z)
		for (int x = 0; x < size; ++x) {
			float y = Height_field[z*size + x];
			int iy = (int(y)/scale)*scale;
#if CHUNK_128
			mesher::set_bit(Mask[z], x, slice == (iy-cy)/scale);
#else
			Mask[z * size + x] = (slice == (iy-cy)/scale);
#endif
		}
		
		Quad q = Normal::quad_py;
		q.y |= slice;
		q.palette = 255;
		mesher::generate_quads_for_slice(Mask, chunk.quads, q, 0, 2);
	}
#if 1
	for (int x = 0; x < size; ++x) {
		for (int z = 0; z < size; ++z) {
			int h = int(Height_field[z*size + x]);
			h = (h/scale)*scale;
			for (int y = 0; y < ycount; ++y) {
				float h2 = (x > 0)? Height_field[z*size+x-1] : Height_field_nx[z];
				int yy = y;
				if (!(x > 0) && chunk.lod) yy += 1;
				bool const b = !(yy*scale+cy < (int(h2)/scale)*scale);
#if CHUNK_128
				mesher::set_bit(Mask[z], y, (y * scale + cy < h) & b);
#else
				Mask[z * size + y] = ((y * scale + cy < h) & b);
#endif
			}
		}
		
		Quad q = Normal::quad_nx;
		q.x |= x;
		q.palette = 50;
		mesher::generate_quads_for_slice(Mask, chunk.quads, q, 1, 2);
	}
	for (int x = 0; x < size; ++x) {
		for (int z = 0; z < size; ++z) {
			int h = int(Height_field[z*size + x]);
			h = (h/scale)*scale;
			for (int y = 0; y < ycount; ++y) {
				float h2 = (x < size-1)? Height_field[z*size+x+1] : Height_field_px[z];
				int yy = y;
				if (!(x < size - 1) && chunk.lod) yy += 1;
				bool const b = !(yy*scale+cy < (int(h2)/scale)*scale);
#if CHUNK_128
				mesher::set_bit(Mask[z], y, (y* scale + cy < h)& b);
#else
				Mask[z * size + y] = ((y * scale + cy < h) & b);
#endif
			}
		}
		
		Quad q = Normal::quad_px;
		q.x |= x;
		q.palette = 100;
		mesher::generate_quads_for_slice(Mask, chunk.quads, q, 1, 2);
	}
	
	for (int z = 0; z < size; ++z) {
		for (int x = 0; x < size; ++x) {
			int h = int(Height_field[z*size + x]);
			h = (h/scale)*scale;
			for (int y = 0; y < ycount; ++y) {
				float h2 = (z > 0)? Height_field[(z-1)*size+x] : Height_field_nz[x];
				int yy = y;
				if (!(z > 0) && chunk.lod) yy += 1;
				bool const b = !(yy*scale+cy < (int(h2)/scale)*scale);
#if CHUNK_128
				mesher::set_bit(Mask[y], x, (y* scale + cy < h)& b);
#else
				Mask[y * size + x] = ((y * scale + cy < h) & b);
#endif
			}
		}
		
		Quad q = Normal::quad_nz;
		q.z |= z;
		q.palette = 150;
		mesher::generate_quads_for_slice(Mask, chunk.quads, q, 0, 1);
	}
	for (int z = 0; z < size; ++z) {
		for (int x = 0; x < size; ++x) {
			int h = int(Height_field[z*size + x]);
			h = (h/scale)*scale;
			for (int y = 0; y < ycount; ++y) {
				float h2 = (z < size-1)? Height_field[(z+1)*size+x] : Height_field_pz[x];
				int yy = y;
				if (!(z < size - 1) && chunk.lod) yy += 1;
				bool const b = !(yy*scale+cy < (int(h2)/scale)*scale);
#if CHUNK_128
				mesher::set_bit(Mask[y], x, (y* scale + cy < h)& b);
#else
				Mask[y * size + x] = ((y * scale + cy < h) & b);
#endif
			}
		}
		
		Quad q = Normal::quad_pz;
		q.z |= z;
		q.palette = 200;
		mesher::generate_quads_for_slice(Mask, chunk.quads, q, 0, 1);
	}
#endif
}

auto lod_from_distance(float dist) -> int {
	float s = 512.0f, d = 256.0f;
#if CHUNK_SIZE == 128
	if (dist < s)          return 0;
	if (dist < s + d)      return 1;
	if (dist < s + d * 2)  return 2;
	if (dist < s + d * 4)  return 3;
	if (dist < s + d * 8)  return 3;
	if (dist < s + d * 16) return 4;
	if (dist < s + d * 32) return 5;
	if (dist < s + d * 64) return 6;
	if (dist < s + d *128) return 7;
	if (dist < s + d *256) return 8;
	return 9;
#else
#if 0
	if (dist < s)           return 0;
	if (dist < s + d)       return 1;
	if (dist < s + d * 2)   return 2;
	if (dist < s + d * 4)   return 3;
	if (dist < s + d * 8)   return 3;
	if (dist < s + d * 16)  return 3;
	if (dist < s + d * 32)  return 4;
	if (dist < s + d * 64)  return 5;
	if (dist < s + d * 128) return 6;
	if (dist < s + d * 256) return 7;
	if (dist < s + d * 512) return 8;
	return 9;
#else
	if (dist < s)           return 0;
	if (dist < s + d)       return 0;
	if (dist < s + d * 2)   return 1;
	if (dist < s + d * 4)   return 2;
	if (dist < s + d * 8)   return 3;
	if (dist < s + d * 16)  return 4;
	if (dist < s + d * 32)  return 5;
	if (dist < s + d * 64)  return 6;
	if (dist < s + d * 128) return 7;
	if (dist < s + d * 256) return 8;
	if (dist < s + d * 512) return 9;
	return 10;
#endif
#endif
}

auto World::generate_lod_tree(LOD_Node* parent, int max_lod, int depth) -> void {
	int quarter_size = (CHUNK_SIZE << max_lod)/4;
	auto center = parent->position;

	fs::v2s32 q[4];
	q[0] = center + fs::v2s32(-quarter_size, quarter_size);
	q[1] = center + fs::v2s32(-quarter_size,-quarter_size);
	q[2] = center + fs::v2s32( quarter_size,-quarter_size);
	q[3] = center + fs::v2s32( quarter_size, quarter_size);

	parent->lod = -1;
	glm::vec3 p{ float(center.x), height_from_xz(center.x, center.y), float(center.y)},
			  b = glm::vec3(float(CHUNK_SIZE << max_lod));
	auto dist = sdBox(p - center_position, b);

	if (max_lod <= 0 || max_lod <= lod_from_distance(dist)) {
		parent->lod = max_lod;
	}
	else {
		LOD_Node* node;

		FS_FOR(4) {
			parent->q[i] = node_allocator.count;
			node = node_allocator.allocate();
			node->position = q[i];
			node->parent = (parent != &root)? (parent - node_allocator.base) : (fs::u16)-1;
			generate_lod_tree(node, max_lod - 1);
		}
	}
}

auto World::build_chunk(Chunk* chunk, int thread_index) -> void
{
	::generate_chunk(*chunk, CHUNK_SIZE, thread_index);
	_interlockedadd64(&total_quad_memory, (long long)chunk->quads.capacity());
	chunk->active = true;
}

auto World::init() -> void {
	auto const size = CHUNK_SIZE;
	FS_FOR(THREAD_COUNT) {
		height_field    [i] = new float  [size*size];
		mask            [i] = new Mask_Type [CHUNK_128?size:size*size];
		height_field_nx [i] = new float [size];
		height_field_pz [i] = new float [size];
		height_field_px [i] = new float [size];
		height_field_nz [i] = new float [size];
	}
	loading_chunks.reserve(500);
}

auto World::uninit() -> void {
//	FS_FOR(4) {
//		delete[] mask;
//		delete[] height_field;
//		delete[] height_field_nx;
//		delete[] height_field_pz;
//		delete[] height_field_px;
//		delete[] height_field_nz;
//	}
	jobs.wait();
}

auto World::generate_chunks() -> void {
#if 0
	auto const N = 16;
	chunks.resize(4 * N * N);
	for (int z = 0; z < N * 2; ++z)
		for (int x = 0; x < N * 2; ++x) {
			auto& c = chunks[z * N * 2 + x];
			c.lod = 0;
			//	if (z - N < 0) c.lod = 0;
			auto const scale = 1 << c.lod;
			c.position = { scale * size * (x - N), 0, scale * size * (z - N) };
			generate_chunk(c, size);
		}
#elif 0
	for (int z = 0; z < 2; ++z)
		for (int x = 0; x < 2; ++x) {
			Chunk c;
			c.lod = 0;
			c.position = { size * x, 0, size * z };
			generate_chunk(c, size);
			chunks.emplace_back(std::move(c));
		}
	for (int k = 1; k <= 8; ++k)
	{
		Chunk c[3];
		auto const s = 1 << k;
		c[0].position = { 0,0,size * s };
		c[1].position = { size * s,0,0 };
		c[2].position = { size * s,0,size * s };
		FS_FOR(3) {
			c[i].lod = k;
			generate_chunk(c[i], size);
			chunks.emplace_back(std::move(c[i]));
		}
	}
#elif 1
	root.position = {};
	root.parent = (fs::u16)-1;
	node_allocator.reset();
	generate_lod_tree(&root, world_size);

	// mark all chunks for deletion
	for (auto&& [k, c] : loaded_chunks) {
		c.active = false;
	}

	generate(&root);

	// delete all chunks marked for deletion
//	for (auto&& [k,c]: loaded_chunks) {
//		if (!c.active) {
//			loaded_chunks.erase(k);
//		}
//	}
#endif
}

auto World::add_new_chunks() -> bool
{
	engine.debug_layer.add("loading: %i", loading_chunks.size());
	auto end = std::remove_if(loading_chunks.begin(), loading_chunks.end(),
		[&](Chunk* c) {
			auto active = c->active;
			if (active) {
				c->active = false;
				auto& H = loaded_chunks[c->lod_position()];
				H = *c;
				delete c;
			}
			return active;
		}
	);
	bool chunks_were_loaded = (end != loading_chunks.end());
	
	if (chunks_were_loaded) {
		loading_chunks.erase(end, loading_chunks.end());

		for (auto&& [k, c] : loaded_chunks) {
			c.active = false;
		}
		pgenerate(&root);
	}

	return chunks_were_loaded;
}

auto World::pgenerate(LOD_Node* node) -> void
{
	if (node->lod != (fs::u8)-1) {
		fs::v3s32 p = position_from_quad_tree(node);
		auto it = loaded_chunks.find(p);
		if (it != loaded_chunks.end()) {
			it->second.active = true;
			it->second.priority++;
		}
	}
	else {
		FS_FOR(4) {
			pgenerate(node_allocator.base + node->q[i]);
		}
	}
}

auto World::generate(LOD_Node* node) -> void {
	if (node->lod != (fs::u8)-1) {
		fs::v3s32 p = position_from_quad_tree(node);
		auto it = loaded_chunks.find(p);

		if (it == loaded_chunks.end()) {
			int size = CHUNK_SIZE << node->lod;
			Chunk c;
			c.position = fs::v3s32{ node->position.x - size / 2, 0, node->position.y - size / 2 };
			c.lod = node->lod;
			//	auto start = fs::timestamp();
			//	::generate_chunk(c, CHUNK_SIZE);
			//	auto time_took = fs::seconds_elasped(start, fs::timestamp());
			//	fs::console::printf("generated chunk -- total: %.1fms worldgen: %.5fms -- %i quads\n", time_took*1e3, world_gen_time, c.quads.size());
			c.place_holder = true;

			auto u = new Chunk;
			u->position = c.position;
			u->lod = c.lod;
			u->quads.reserve(5000);
			u->place_holder = false;
			u->active = false;

			loaded_chunks.emplace_hint(it, p, c);
			loading_chunks.emplace_back(u);

			jobs.add_work(u);
		}
		else it->second.active = true;
	}
	else {
		FS_FOR(4) {
			generate(node_allocator.base + node->q[i]);
		}
	}
}
