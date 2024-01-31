#include <Fission/Base/Math/Vector.hpp>
#include <vector>

struct quad {
	char i0, i1, j0, j1;
	char slice;
	char normal_axis;
};
static constexpr int sizeof_quad = sizeof(quad);

auto scan_quad(fs::u8 m[], int x0, int y0) -> fs::v2s32 {
	int w = 0;
	for (int x = x0 + 1; x < 8; ++x) {
		if (!m[y0 * 8 + x]) break;
		++w;
	}

	int h = 0;
	for (int y = y0 + 1; y < 8; ++y) {
		for (int x = x0; x <= x0 + w; ++x) {
			if (!m[y * 8 + x]) goto done;
		}
		++h;
	}
	done:
	return {x0 + w, y0 + h};
};
		
auto generate_quads_for_slice(fs::u8 mask[], int slice, int normal_axis, std::vector<quad>& quads) -> void {
	for (int y0 = 0; y0 < 8; ++y0)
	for (int x0 = 0; x0 < 8; ) {
		if (!mask[y0 * 8 + x0]) {
			++x0;
			continue;
		}

		auto [x1, y1] = scan_quad(mask, x0, y0);

		quads.emplace_back(x0, x1 + 1, y0, y1 + 1, slice, normal_axis);

		for (int x = x0; x <= x1; ++x)
		for (int y = y0; y <= y1; ++y) {
			mask[y * 8 + x] = false;
		}

		x0 = x1 + 1;
	}
};

#define for_n(VAR,COUNT) for (std::remove_const_t<decltype(COUNT)> VAR = 0; VAR < COUNT; ++VAR)

struct adjacent_chunks {
	fs::u8* pos[3];
	fs::u8* neg[3];
};

auto generate_quads_for_chunk(fs::u8* chunk_mask, adjacent_chunks const* adjacent, std::vector<quad>& quads) -> void {
	union vec3 {
		char comp[4];
		struct {
			char x, y, z, _;
		};
	};

	fs::u8 mask[8*8];

	for_n (normal_axis, 3) {
		int i_axis = (normal_axis+1)%3;
		int j_axis = (normal_axis+2)%3;
		auto adjacent_chunk_mask = adjacent->pos[normal_axis];
		for_n(slice, (char)8) {
			// generate mask
			vec3 pos;
			for_n (i, (char)8)
			for_n (j, (char)8) {
				pos.comp[normal_axis] = slice;
				pos.comp[i_axis] = i;
				pos.comp[j_axis] = j;
				mask[j*8 + i] = chunk_mask[pos.y*8 + pos.z] & (1 << pos.x);
				int next = slice - 1;
				if (next >= 0) {
					pos.comp[normal_axis] = next;
					if(chunk_mask[pos.y*8 + pos.z] & (1 << pos.x))
						mask[j*8 + i] = 0;
				}
				else {
					pos.comp[normal_axis] = 7;
					if(adjacent_chunk_mask[pos.y*8 + pos.z] & (1 << pos.x))
						mask[j*8 + i] = 0;
				}
			}

			generate_quads_for_slice(mask, slice, normal_axis, quads);
		}
	}
	
	for_n (normal_axis, 3) {
		int i_axis = (normal_axis+1)%3;
		int j_axis = (normal_axis+2)%3;
		auto adjacent_chunk_mask = adjacent->neg[normal_axis];
		for_n(slice, (char)8) {
			// generate mask
			vec3 pos;
			for_n (i, (char)8)
			for_n (j, (char)8) {
				pos.comp[normal_axis] = slice;
				pos.comp[i_axis] = i;
				pos.comp[j_axis] = j;
				mask[j*8 + i] = chunk_mask[pos.y*8 + pos.z] & (1 << pos.x);
				int next = slice + 1;
				if (next < 8) {
					pos.comp[normal_axis] = next;
					if(chunk_mask[pos.y*8 + pos.z] & (1 << pos.x))
						mask[j*8 + i] = 0;
				}
				else {
					pos.comp[normal_axis] = 0;
					if(adjacent_chunk_mask[pos.y*8 + pos.z] & (1 << pos.x))
						mask[j*8 + i] = 0;
				}
			}

			generate_quads_for_slice(mask, slice + 1, normal_axis - 3, quads);
		}
	}
}