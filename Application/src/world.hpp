#include <Chunk.hpp>

struct LOD_Node {
	fs::u16 q[4];
	fs::v2s32 position;
	fs::u8 lod;
};
static constexpr int __sizeof_LOD = sizeof(LOD_Node);
static constexpr int __total = sizeof(LOD_Node) * (1 << 16);

template <typename T>
struct bump_allocator {
	T* base = nullptr;
	T* end  = nullptr;
	T* next = nullptr;

	bump_allocator(int max_count);
	~bump_allocator();

	auto allocate() -> T*;
};

struct World {
	LOD_Node root;
	std::vector<Chunk> chunks;

	auto generate_chunks() -> void;
};