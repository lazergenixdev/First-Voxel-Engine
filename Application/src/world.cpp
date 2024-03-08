#include "world.hpp"

static constexpr int CHUNK_SIZE = 128;

template <typename T>
bump_allocator<T>::bump_allocator(int max_count) {
	base = new T[max_count];
	next = base;
	end  = base + max_count;
}

template <typename T>
bump_allocator<T>::~bump_allocator() {
	delete [] base;
	base = end = next = nullptr;
}

template <typename T>
auto bump_allocator<T>::allocate() -> T* {
	return next++;
}

auto height_from_xz(int x, int z) -> float {
	float h =
//	36.0f + 32.0f * stb_perlin_noise3(float(x)/2e2f, 0.0f, float(z)/2e2f, 0, 0, 0)
//	64.0f + 80.0f * stb_perlin_noise3(float(x)/5e2f, 0.0f, float(z)/5e2f, 0, 0, 0)
//	128.0f + 80.0f * stb_perlin_fbm_noise3(float(x)/1e3f, 0.0f, float(z)/1e3f, 2.0f, 0.5f, 6)
//	64.0f - 80.0f * stb_perlin_turbulence_noise3(float(x)/5e2f, 0.0f, float(z)/5e2f, 2.0f, 0.5f, 6)
	64.0f - 256.0f * stb_perlin_turbulence_noise3(float(x)/3e3f, 0.0f, float(z)/3e3f, 2.0f, 0.5f, 6)
	;
	return h;
}

namespace mesher {
	static constexpr int S = CHUNK_SIZE;
	auto scan_quad(fs::u8 m[], int x0, int y0) -> fs::v2s32 {
		int w = 0;
		for (int x = x0 + 1; x < S; ++x) {
			if (!m[y0*S + x]) break;
			++w;
		}

		int h = 0;
		for (int y = y0 + 1; y < S; ++y) {
			for (int x = x0; x <= x0 + w; ++x) {
				if (!m[y*S + x]) goto done;
			}
			++h;
		}
		done:
		return {x0 + w, y0 + h};
	};
		
	auto generate_quads_for_slice(fs::u8 mask[], std::vector<Quad>& quads, Quad normal, char i, char j) -> void {
		for (int y0 = 0; y0 < S; ++y0)
		for (int x0 = 0; x0 < S; ) {
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

			if (q.l0 == 0 || q.l1 == 0)
				__debugbreak();

			quads.emplace_back(q);

			for (int x = x0; x <= x1; ++x)
			for (int y = y0; y <= y1; ++y) {
				mask[y*S + x] = false;
			}

			x0 = x1 + 1;
		}
	};
}

static float* height_field;
static fs::u8* mask;

static float* height_field_pz;
static float* height_field_nx;
static float* height_field_nz;
static float* height_field_px;

auto generate_chunk(Chunk& chunk, int size) -> void {
	auto const cx = chunk.position.x;
	auto const cz = chunk.position.z;
	auto const scale = 1 << chunk.lod;
	
	float min = 1e10f, max = -1e10f;
	for (int z = 0; z < size; ++z)
	for (int x = 0; x < size; ++x) {
		float y = height_from_xz(x*scale + cx, z*scale + cz);
		if (y < min) min = y;
		if (y > max) max = y;
		height_field[z*size + x] = y;
	}
	
	for (int z = 0; z < size; ++z) {
		int x = -1;
		float y = height_from_xz(x*scale + cx, z*scale + cz);
		if (y < min) min = y;
		if (y > max) max = y;
		height_field_nx[z] = y;
	}
	for (int z = 0; z < size; ++z) {
		int x = size;
		float y = height_from_xz(x*scale + cx, z*scale + cz);
		if (y < min) min = y;
		if (y > max) max = y;
		height_field_px[z] = y;
	}
	for (int x = 0; x < size; ++x) {
		int z = size;
		float y = height_from_xz(x*scale + cx, z*scale + cz);
		if (y < min) min = y;
		if (y > max) max = y;
		height_field_pz[x] = y;
	}
	for (int x = 0; x < size; ++x) {
		int z = -1;
		float y = height_from_xz(x*scale + cx, z*scale + cz);
		if (y < min) min = y;
		if (y > max) max = y;
		height_field_nz[x] = y;
	}

	int cy = int(min);
	if (scale != 1) {
		cy = (cy/scale)*scale;	
	}
	chunk.position.y = cy;

	int ycount = ((int(max)/scale)*scale - cy)/scale + 2;
//	int ycount = (int(max) - int(min) + 1)/scale + 1;
	if (ycount >= 128) {
		__debugbreak();
	}

	for (int slice = 0; slice < ycount; ++slice) {
		for (int z = 0; z < size; ++z)
		for (int x = 0; x < size; ++x) {
			float y = height_field[z*size + x];
			mask[z*size + x] = (scale*(slice-1)+cy < int(y)) && (scale*slice+cy >= int(y));
		//	mask[z*size + x] = (scale*(slice-1)+cy < int(y));
		}
		
		Quad q = Normal::quad_py;
		q.y |= slice;
		q.palette = 255;
		mesher::generate_quads_for_slice(mask, chunk.quads, q, 0, 2);
	}
#if 1
	for (int x = 0; x < size; ++x) {
		for (int z = 0; z < size; ++z) {
			int h = int(height_field[z*size + x]);
			for (int y = 0; y < ycount; ++y) {
				float h2 = (x > 0)? height_field[z*size+x-1] : height_field_nx[z];
				bool const b = !(y*scale+cy < int(h2));
				mask[z*size + y] = (y*scale+cy < h) & b;
			}
		}
		
		Quad q = Normal::quad_nx;
		q.x |= x;
		q.palette = 50;
		mesher::generate_quads_for_slice(mask, chunk.quads, q, 1, 2);
	}
	for (int x = 0; x < size; ++x) {
		for (int z = 0; z < size; ++z) {
			int h = int(height_field[z*size + x]);
			for (int y = 0; y < ycount; ++y) {
				float h2 = (x < size-1)? height_field[z*size+x+1] : height_field_px[z];
				bool const b = !(y*scale+cy < int(h2));
				mask[z*size + y] = (y*scale+cy < h) & b;
			}
		}
		
		Quad q = Normal::quad_px;
		q.x |= x;
		q.palette = 100;
		mesher::generate_quads_for_slice(mask, chunk.quads, q, 1, 2);
	}
	
	for (int z = 0; z < size; ++z) {
		for (int x = 0; x < size; ++x) {
			int h = int(height_field[z*size + x]);
			for (int y = 0; y < ycount; ++y) {
				float h2 = (z > 0)? height_field[(z-1)*size+x] : height_field_nz[x];
				bool const b = !(y*scale+cy < int(h2));
				mask[y*size + x] = (y*scale+cy < h) & b;
			}
		}
		
		Quad q = Normal::quad_nz;
		q.z |= z;
		q.palette = 150;
		mesher::generate_quads_for_slice(mask, chunk.quads, q, 0, 1);
	}
	for (int z = 0; z < size; ++z) {
		for (int x = 0; x < size; ++x) {
			int h = int(height_field[z*size + x]);
			for (int y = 0; y < ycount; ++y) {
				float h2 = (z < size-1)? height_field[(z+1)*size+x] : height_field_pz[x];
				bool const b = !(y*scale+cy < int(h2));
				mask[y*size + x] = (y*scale+cy < h) & b;
			}
		}
		
		Quad q = Normal::quad_pz;
		q.z |= z;
		q.palette = 200;
		mesher::generate_quads_for_slice(mask, chunk.quads, q, 0, 1);
	}
#endif
}

auto World::generate_chunks() -> void {
	auto const size = CHUNK_SIZE;
	height_field = new float  [size*size];
	mask         = new fs::u8 [size*size];
	height_field_nx = new float [size];
	height_field_pz = new float [size];
	height_field_px = new float [size];
	height_field_nz = new float [size];

#if 1
	auto const N = 16;
	chunks.resize(4 * N * N);

	for (int z = 0; z < N*2; ++z)
	for (int x = 0; x < N*2; ++x) {
		auto& c = chunks[z*N*2+x];
		c.lod = 0;
		auto const scale = 1 << c.lod;
		c.position = { scale*size*(x-N), 0, scale*size*(z-N) };
	//	c.position = { scale*size*x, 0, scale*size*z };
		generate_chunk(c, size);
	}
#else
	for (int z = 0; z < 2; ++z)
	for (int x = 0; x < 2; ++x) {
		Chunk c;
		c.lod = 0;
		c.position = { size*x, 0, size*z };
		generate_chunk(c,size);
		chunks.emplace_back(std::move(c));
	}

	for (int k = 1; k <= 6; ++k)
	{
		Chunk c[3];
		auto const s = 1 << k;
		c[0].position = {0,0,size*s};
		c[1].position = {size*s,0,0};
		c[2].position = {size*s,0,size*s};
		FS_FOR(3) {
			c[i].lod = k;
			generate_chunk(c[i], size);
			chunks.emplace_back(std::move(c[i]));
		}
	}
#endif


	delete [] mask;
	delete [] height_field;
	delete [] height_field_nx;
	delete [] height_field_pz;
	delete [] height_field_px;
	delete [] height_field_nz;
}
