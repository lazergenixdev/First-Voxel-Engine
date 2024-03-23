#include <Chunk.hpp>

struct LOD_Node {
	fs::u16 q[4];
	fs::v2s32 position;
	fs::u8 lod;
	fs::u16 parent = (fs::u16)-1;
};
static constexpr int __sizeof_LOD = sizeof(LOD_Node);
static constexpr int __total = sizeof(LOD_Node) * (1 << 16);

template <typename T>
struct bump_allocator {
	T* base = nullptr;
	T* end  = nullptr;
	int count = 0;

	bump_allocator(int max_count) {
		base = new T[max_count];
		end = base + max_count;
	}
	~bump_allocator() {
		delete[] base;
		base = end = nullptr;
		count = 0;
	}

	auto allocate() -> T* {
		int n = count;
		++count;
		return base + n;
	}
	auto inline reset() -> void {
		count = 0;
	}
};


template <>
struct std::hash<fs::v3s32> {
	static fs::u64 mix(fs::s64 a, fs::s64 b, fs::s64 c)
	{
		a = a - b;  a = a - c;  a = a ^ RotateRight64(c, 13);
		b = b - c;  b = b - a;  b = b ^ (a << 8);
		c = c - a;  c = c - b;  c = c ^ RotateRight64(b, 13);
		a = a - b;  a = a - c;  a = a ^ RotateRight64(c, 12);
		b = b - c;  b = b - a;  b = b ^ (a << 16);
		c = c - a;  c = c - b;  c = c ^ RotateRight64(b, 5);
		a = a - b;  a = a - c;  a = a ^ RotateRight64(c, 3);
		b = b - c;  b = b - a;  b = b ^ (a << 10);
		c = c - a;  c = c - b;  c = c ^ RotateRight64(b, 15);
		return reinterpret_cast<fs::u64&>(c);
	}
	_NODISCARD size_t operator()(fs::v3s32 const& pos) const noexcept {
		return mix(pos.x, pos.y, pos.z);
	}
};

static auto position_from_quad_tree(LOD_Node* node) -> fs::v3s32 {
	return fs::v3s32(node->position.x, 0, node->position.y);
}

struct Chunk_Ref {

};

struct World {
	static constexpr int world_size = 15;

	LOD_Node root;
	bump_allocator<LOD_Node> node_allocator{ 1 << 16 };
	
	glm::vec3 center_position;
	static volatile fs::s64 total_quad_memory;

	std::unordered_map<fs::v3s32, Chunk> loaded_chunks;
	std::vector<Chunk*>                  loading_chunks;
	job_queue<Chunk*>                    jobs{THREAD_COUNT, build_chunk};

	// optimization: keep all active chunks here:
	std::vector<Chunk_Ref> active_chunks;

	static auto build_chunk(Chunk* chunk, int) -> void;

	auto init() -> void;
	auto uninit() -> void;

	auto generate_lod_tree(LOD_Node* parent, int max_lod, int depth = 0) -> void;
	auto generate_chunks() -> void;

	auto add_new_chunks() -> bool; // TODO: this is a bad name, rename pls
	auto pgenerate(LOD_Node* node) -> void;

	auto generate(LOD_Node* node) -> void;

	template <typename F>
	auto for_each_chunk(LOD_Node* node, F&& f) -> void {
		if (node->lod != (fs::u8)-1) {
			fs::v3s32 p = position_from_quad_tree(node);
			auto it = loaded_chunks.find(p);
			if (it != loaded_chunks.end()) {
				f(node, &it->second);
			}
		}
		else {
			FS_FOR(4) {
				for_each_chunk(node_allocator.base + node->q[i], f);
			}
		}
	}
};
