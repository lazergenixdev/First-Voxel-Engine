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

#define LOBATOMY 0

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

struct LOD_Tree {
	LOD_Tree* octant[8] = {};
	r3i d = {};
	int lod = 0; // LOD of 0 means that there is no chunk here, we are a parent.

	LOD_Tree(int lod) : lod(lod) {}
	LOD_Tree(int lod, r3i d) : lod(lod), d(d) {}
};

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

	Vertex(float x, float y, float z): position(x,y,z), palette(color_from_position(x,y,z)) {}
	Vertex(float x, float y, float z, float p): position(x,y,z), palette(p) {}

	static float color_from_position(float x, float y, float z) {
	//	return stb_perlin_noise3(x / d, y / d, z / d, 0, 0, 0) + 0.6f;
		return stb_perlin_noise3(2.0f*x / d, 2.0f*y / d, 2.0f*z / d, 0, 0, 0) + 0.6f;
	//	return y/256.0f;
	}
};

inline int max_vertex_count = 0;

struct Vertex_Buffer {
	VmaAllocation allocation;
	VkBuffer      buffer;
	void*         gpu_data;
	void*         cpu_data;
	VkDeviceSize  dirty_size = 0;
	unsigned int  index_count = 0;

	Vertex_Buffer() {
		VmaAllocationCreateInfo allocInfo = {};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = (1 << 15) * sizeof(Vertex);
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		auto vkr = vmaCreateBuffer(engine.graphics.allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
		if (vkr) __debugbreak();
		vmaMapMemory(engine.graphics.allocator, allocation, &gpu_data);
		cpu_data = new fs::byte[bufferInfo.size];
		total_vertex_gpu_memory += bufferInfo.size;
	}
	~Vertex_Buffer() {
		if (allocation) {
			vmaUnmapMemory(engine.graphics.allocator, allocation);
			vmaDestroyBuffer(engine.graphics.allocator, buffer, allocation);
		}
	}

	Vertex_Buffer(Vertex_Buffer const&) = delete;
	Vertex_Buffer(Vertex_Buffer&& v) noexcept {
		allocation = v.allocation;
		buffer = v.buffer;
		v.allocation = nullptr;
		v.buffer     = nullptr;
	}

	void send(Vertex* data, uint32_t count) {
		dirty_size = sizeof(Vertex) * count;
		memcpy(cpu_data, data, dirty_size);
	}
	
	VkBuffer* get() {
		if (dirty_size) {
			memcpy(gpu_data, cpu_data, dirty_size);
			vmaFlushAllocation(engine.graphics.allocator, allocation, 0, dirty_size);
			auto c = dirty_size / sizeof(Vertex);
			if (c > max_vertex_count) max_vertex_count = c;
			dirty_size = 0;
		}
		return &buffer;
	}
};

struct Chunk_ID {
	fs::v3s32 pos;
	int lod;
	
	constexpr size_t hash() const {
		return 8*(pos.z * 1024*1024 + pos.y*1024 + pos.x) + lod;
	}
	constexpr bool operator==(Chunk_ID const&) const = default;
};

struct Chunk {
	int vertex_buffer = -1;
	int status = 0;
};

template <>
struct std::hash<Chunk_ID> {
	_NODISCARD size_t operator()(Chunk_ID const& _Keyval) const noexcept {
		return _Keyval.hash();
	}
};

struct Chunk_Gen_Info {
	r3i rect;
	int lod;
	Chunk chunk;
};

void gen_chunk(Chunk_Gen_Info& i);

namespace {
	inline std::unordered_map<Chunk_ID, Chunk> loaded_chunks;
	inline std::vector<Vertex_Buffer>          vertex_buffers;
	inline std::vector<int>                    free_vertex_buffers;
	inline job_queue<Chunk_Gen_Info>           job_q(8, gen_chunk);
}

void generate_mesh(r3i r, int lod, Vertex_Buffer& vb);

void gen_chunk(Chunk_Gen_Info& i) {
	generate_mesh(i.rect, i.lod, vertex_buffers[i.chunk.vertex_buffer]);
}

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

void generate_mesh(r3i r, int lod, Vertex_Buffer& vb) {
	static constexpr int L = 16;
	//
	//	auto L = (8 << 5) / (1 << node->lod);
	//
	uint8_t blocks[L*L*L];
	for (int z = 0; z < L; ++z)
	for (int y = 0; y < L; ++y)
	for (int x = 0; x < L; ++x) {
		float x0 = fs::lerp(float(r.x.low), float(r.x.high), (float(x)+0.5f)/L);
		float y0 = fs::lerp(float(r.y.low), float(r.y.high), (float(y)+0.5f)/L);
		float z0 = fs::lerp(float(r.z.low), float(r.z.high), (float(z)+0.5f)/L);
			
		auto w = stb_perlin_noise3(x0/Vertex::d,-y0/Vertex::d, z0/Vertex::d, 0, 0, 0);
		blocks[z*L*L+y*L+x] = uint8_t(w < 0.1f);
	//	auto h = stb_perlin_noise3(0.5f*x0/Vertex::d, 0.0f,-0.5f*z0/Vertex::d, 0, 0, 0) * 256.0f - 3000.0f;
	//	blocks[z*L*L+y*L+x] = y0 < h;
	//	auto w = stb_perlin_ridge_noise3(x0/Vertex::d,-y0/Vertex::d, z0/Vertex::d, 2.0f, 0.5f, 1.0f, 2);
	//	blocks[z*L*L+y*L+x] = uint8_t(w < 0.4f);
	}

	auto V = reinterpret_cast<Vertex*>(vb.cpu_data);
	uint32_t count = 0;

	for (int z = 0; z < L; ++z)
	for (int y = 0; y < L; ++y)
	for (int x = 0; x < L; ++x) {
		uint8_t b = blocks[z*L*L+y*L+x];
		if (b) {
			float x0 = fs::lerp(float(r.x.low), float(r.x.high), float(x  )/L);
			float x1 = fs::lerp(float(r.x.low), float(r.x.high), float(x+1)/L);
			float y0 = fs::lerp(float(r.y.low), float(r.y.high), float(y  )/L);
			float y1 = fs::lerp(float(r.y.low), float(r.y.high), float(y+1)/L);
			float z0 = fs::lerp(float(r.z.low), float(r.z.high), float(z  )/L);
			float z1 = fs::lerp(float(r.z.low), float(r.z.high), float(z+1)/L);

			float p = Vertex::color_from_position(0.5f*(x0 + x1),0.5f*(y0 + y1),0.5f*(z0 + z1));

			if (x == 0 || (x > 0 && !blocks[z*L*L+y*L+(x-1)])) {
				V[count++] = {x0, y0, z0, p};
				V[count++] = {x0, y0, z1, p};
				V[count++] = {x0, y1, z0, p};
				V[count++] = {x0, y1, z1, p};
			}
			if (x == L-1 || (x < L-1 && !blocks[z*L*L+y*L+(x+1)])) {
				V[count++] = {x1, y0, z0, p};
				V[count++] = {x1, y0, z1, p};
				V[count++] = {x1, y1, z0, p};
				V[count++] = {x1, y1, z1, p};
			}
			if (y == 0 || (y > 0 && !blocks[z*L*L+(y-1)*L+x])) {
				V[count++] = {x0, y0, z0, p};
				V[count++] = {x0, y0, z1, p};
				V[count++] = {x1, y0, z0, p};
				V[count++] = {x1, y0, z1, p};
			}
			if (y == L-1 || (y < L-1 && !blocks[z*L*L+(y+1)*L+x])) {
				V[count++] = {x0, y1, z0, p};
				V[count++] = {x0, y1, z1, p};
				V[count++] = {x1, y1, z0, p};
				V[count++] = {x1, y1, z1, p};
			}
			if (z == 0 || (z > 0 && !blocks[(z-1)*L*L+y*L+x])) {
				V[count++] = {x0, y0, z0, p};
				V[count++] = {x0, y1, z0, p};
				V[count++] = {x1, y0, z0, p};
				V[count++] = {x1, y1, z0, p};
			}
			if (z == L-1 || (z < L-1 && !blocks[(z+1)*L*L+y*L+x])) {
				V[count++] = {x0, y0, z1, p};
				V[count++] = {x0, y1, z1, p};
				V[count++] = {x1, y0, z1, p};
				V[count++] = {x1, y1, z1, p};
			}
		}
	}

	if (count) {
		vb.dirty_size = count * sizeof(Vertex);
	//	vb.send(V.data(), (uint32_t)V.size());
		int quad_count = uint32_t(count / 4);
		vb.index_count = quad_count*6;
	}
	else vb.index_count = 0;
}

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

struct World_Renderer {
	static constexpr VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

	fs::Render_Pass  render_pass;
	VkPipeline       pipeline;
	VkPipeline       depth_pipeline;
	VkPipeline       wireframe_pipeline;
	VkPipelineLayout pipeline_layout;

	VmaAllocation index_allocation;
	VkBuffer      index_buffer;

	VkImageView   depth_view;
	gfx::Image    depth_image;

	VkImageView   color_view;
	gfx::Image    color_image;

	VkFramebuffer frame_buffers[fs::Graphics::max_sc_images];

	Blend_Technique blend;

	Skybox skybox;

	struct vert_shader {
#include "../shaders/color.vert.inl"
	};
	struct frag_shader {
#include "../shaders/color.frag.inl"
	};

	struct transform_data {
		glm::mat4 view_projection;
		float normal = 1.0f;
	};

	void create() {
		auto& gfx = engine.graphics;

		Pipeline_Layout_Creator{}
			.add_push_range(VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(transform_data))
			.create(&pipeline_layout);

		Render_Pass_Creator{3}
			.add_attachment(engine.graphics.sc_format, VK_SAMPLE_COUNT_8_BIT)
			.add_attachment(depth_format, VK_SAMPLE_COUNT_8_BIT)
			.add_attachment(engine.graphics.sc_format)
			.add_subpass({ {0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} }, { 1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL }, { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL })
			.add_external_subpass_dependency(0)
			.create(&render_pass.handle);

		Image_Creator ic{ depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, gfx.sc_extent };
		ic.image_info.samples = VK_SAMPLE_COUNT_8_BIT;
		ic.create(depth_image);

		auto image_view_info = vk::image_view_2d(depth_image.image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
		vkCreateImageView(gfx.device, &image_view_info, nullptr, &depth_view);

		ic = Image_Creator{ engine.graphics.sc_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, gfx.sc_extent };
		ic.image_info.samples = VK_SAMPLE_COUNT_8_BIT;
		ic.create(color_image);

		image_view_info = vk::image_view_2d(color_image.image, engine.graphics.sc_format, VK_IMAGE_ASPECT_COLOR_BIT);
		vkCreateImageView(gfx.device, &image_view_info, nullptr, &color_view);

		{
			VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			framebufferInfo.renderPass = render_pass;
			framebufferInfo.width = gfx.sc_extent.width;
			framebufferInfo.height = gfx.sc_extent.height;
			framebufferInfo.layers = 1;
			VkImageView attachments[] = { color_view, depth_view, VK_NULL_HANDLE };
			framebufferInfo.attachmentCount = vk::count(attachments);
			framebufferInfo.pAttachments = attachments;
			FS_FOR(gfx.sc_image_count) {
				attachments[2] = gfx.sc_image_views[i];
				vkCreateFramebuffer(gfx.device, &framebufferInfo, nullptr, frame_buffers + i);
			}
		}

		auto vs = fs::create_shader(gfx.device, vert_shader::size, vert_shader::data);
		auto fs = fs::create_shader(gfx.device, frag_shader::size, frag_shader::data);

		auto rp = render_pass.handle;
#if LOBATOMY
		blend.create();
		rp = blend.render_pass;
#endif

		skybox.create(engine.graphics, rp);

		vk::Basic_Vertex_Input<fs::v4f32> vi;
		auto pipeline_creator = Pipeline_Creator{ rp, pipeline_layout }
			.add_shader(VK_SHADER_STAGE_VERTEX_BIT, vs)
			.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, fs)
			.vertex_input(&vi)
			.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
			.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);

		pipeline_creator.multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
		pipeline_creator.create(&pipeline);

		pipeline_creator.rasterization_state.depthBiasEnable = VK_TRUE;
		pipeline_creator.rasterization_state.depthBiasConstantFactor = 0.01f;
		pipeline_creator.rasterization_state.depthBiasSlopeFactor = 1.0f;
		pipeline_creator.create_no_fragment(&depth_pipeline);
	
		pipeline_creator.rasterization_state.depthBiasEnable = VK_FALSE;
		pipeline_creator.rasterization_state.polygonMode = VK_POLYGON_MODE_LINE;
		pipeline_creator.create_and_destroy_shaders(&wireframe_pipeline);

		uint32_t constexpr index_count = (1 << 15) * 3;
		{
			VmaAllocationCreateInfo allocInfo = {};
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
			bufferInfo.size = index_count * sizeof(fs::u16);
			bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			vmaCreateBuffer(gfx.allocator, &bufferInfo, &allocInfo, &index_buffer, &index_allocation, nullptr);
		}
		{
			fs::u16 index_array[] = { 0, 1, 2, 2, 3, 1 };
			uint16_t* gpu_index_buffer;
			vmaMapMemory(engine.graphics.allocator, index_allocation, (void**)&gpu_index_buffer);
			FS_FOR(index_count / 6) for (int k = 0; k < 6; ++k) gpu_index_buffer[i*6+k] = index_array[k] + i*4;
			vmaUnmapMemory(engine.graphics.allocator, index_allocation);
			vmaFlushAllocation(engine.graphics.allocator, index_allocation, 0, sizeof(fs::u16) * index_count);
		}
		vertex_buffers.resize(1 << 12);
		FS_FOR(vertex_buffers.size())
			free_vertex_buffers.emplace_back(int(i));
	}
	void destroy() {
		vmaDestroyBuffer(engine.graphics.allocator, index_buffer, index_allocation);
		vkDestroyPipelineLayout(engine.graphics.device, pipeline_layout, nullptr);
		vkDestroyPipeline(engine.graphics.device, pipeline, nullptr);
		vkDestroyPipeline(engine.graphics.device, depth_pipeline, nullptr);
		vkDestroyPipeline(engine.graphics.device, wireframe_pipeline, nullptr);
		vkDestroyImageView(engine.graphics.device, depth_view, nullptr);
		vkDestroyImageView(engine.graphics.device, color_view, nullptr);
		FS_FOR(engine.graphics.sc_image_count)
			vkDestroyFramebuffer(engine.graphics.device, frame_buffers[i], nullptr);
		render_pass.destroy();
		vertex_buffers.clear();
#if LOBATOMY
		blend.destroy();
#endif
		skybox.destroy(engine.graphics);
	}
	void resize_frame_buffers() {
		auto& gfx = engine.graphics;
		depth_image.~Image();
		color_image.~Image();
		vkDestroyImageView(gfx.device, depth_view, nullptr);
		vkDestroyImageView(gfx.device, color_view, nullptr);
		FS_FOR(gfx.sc_image_count)
			vkDestroyFramebuffer(gfx.device, frame_buffers[i], nullptr);
		
		Image_Creator ic{depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, gfx.sc_extent};
		ic.image_info.samples = VK_SAMPLE_COUNT_8_BIT;
		ic.create(depth_image);

		auto image_view_info = vk::image_view_2d(depth_image.image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
		vkCreateImageView(gfx.device, &image_view_info, nullptr, &depth_view);

		ic = Image_Creator{engine.graphics.sc_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, gfx.sc_extent};
		ic.image_info.samples = VK_SAMPLE_COUNT_8_BIT;
		ic.create(color_image);

		image_view_info = vk::image_view_2d(color_image.image, engine.graphics.sc_format, VK_IMAGE_ASPECT_COLOR_BIT);
		vkCreateImageView(gfx.device, &image_view_info, nullptr, &color_view);

		{
			VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			framebufferInfo.renderPass = render_pass;
			framebufferInfo.width  = gfx.sc_extent.width;
			framebufferInfo.height = gfx.sc_extent.height;
			framebufferInfo.layers = 1;
			VkImageView attachments[] = { color_view, depth_view, VK_NULL_HANDLE };
			framebufferInfo.attachmentCount = vk::count(attachments);
			framebufferInfo.pAttachments = attachments;
			FS_FOR(gfx.sc_image_count) {
				attachments[2] = gfx.sc_image_views[i];
				vkCreateFramebuffer(gfx.device, &framebufferInfo, nullptr, frame_buffers + i);
			}
		}

#if LOBATOMY
		blend.resize();
#endif
	}

	void draw(fs::Render_Context* ctx, Camera_Controller const& cc, float dt, bool wireframe, bool wireframe_depth) {
#if LOBATOMY
		blend.begin(ctx);
#else
		VkClearValue clear_values[3];
		clear_values[0].color = {};
		clear_values[1].depthStencil.depth = 1.0f;
		clear_values[2].color = {};
		VkRenderPassBeginInfo render_pass_begin_info{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		render_pass_begin_info.clearValueCount = (fs::u32)std::size(clear_values);
		render_pass_begin_info.pClearValues = clear_values;
		render_pass_begin_info.framebuffer = frame_buffers[ctx->image_index];
		render_pass_begin_info.renderArea = { .offset = {}, .extent = ctx->gfx->sc_extent };
		render_pass_begin_info.renderPass = render_pass;
		vkCmdBeginRenderPass(ctx->command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
#endif
		vkCmdBindIndexBuffer(ctx->command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16);

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(ctx->gfx->sc_extent.width);
		viewport.height = static_cast<float>(ctx->gfx->sc_extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(ctx->command_buffer, 0, 1, &viewport);
		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = ctx->gfx->sc_extent;
		vkCmdSetScissor(ctx->command_buffer, 0, 1, &scissor);

		transform_data td;
		td.view_projection = cc.get_transform();
		td.normal = wireframe ? 1.0f : 1.0f;
		vkCmdPushConstants(ctx->command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(td), &td);

		auto p = (fs::v3s32)fs::v3f32::from(cc.position);

		static std::vector<Chunk> chunks;
		chunks.clear();

		auto compare = [&](Chunk const& l, Chunk const& r) {
			return l.status < r.status;
		};

#if 0
		for (auto&& [id, chunk] : loaded_chunks) {
			if (chunk.vertex_buffer >= 0 && vertex_buffers[chunk.vertex_buffer].index_count == 0) {
				free_vertex_buffers.emplace_back(chunk.vertex_buffer);
				chunk.vertex_buffer = -2;
			}
		}
#endif

		for (auto&& [id, chunk] : loaded_chunks) {
			if (chunk.vertex_buffer < 0) continue;
			Chunk c;
			c.status = (id.pos - p).lensq();
			c.vertex_buffer = chunk.vertex_buffer;
			auto it = std::lower_bound(chunks.begin(), chunks.end(), c, compare);
			chunks.insert(it, c);
		}
	//	std::sort(loaded_chunks.begin(), loaded_chunks.end(), [&](Chunk const& l, Chunk const& r) {
	//		auto dl = (l.pos - p).lensq();
	//		auto dr = (r.pos - p).lensq();
	//		return dl < dr;
	//	});

		VkDeviceSize offset = 0;
		used_vertex_gpu_memory = 0;
		if (wireframe) {
			if (wireframe_depth) {
				vkCmdBindPipeline(ctx->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_pipeline);
				for (auto&& chunk: chunks) {
					auto& vertex_buffer = vertex_buffers[chunk.vertex_buffer];
					if (vertex_buffer.index_count) {
						vkCmdBindVertexBuffers(ctx->command_buffer, 0, 1, vertex_buffer.get(), &offset);
						vkCmdDrawIndexed(ctx->command_buffer, vertex_buffer.index_count, 1, 0, 0, 0);
					}
				}
			}
			
			vkCmdBindPipeline(ctx->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe_pipeline);
			for (auto&& chunk: chunks) {
				auto& vertex_buffer = vertex_buffers[chunk.vertex_buffer];
				if (vertex_buffer.index_count) {
					vkCmdBindVertexBuffers(ctx->command_buffer, 0, 1, vertex_buffer.get(), &offset);
					vkCmdDrawIndexed(ctx->command_buffer, vertex_buffer.index_count, 1, 0, 0, 0);
					used_vertex_gpu_memory += (vertex_buffer.index_count/3)*2;
				}
			}
		}
		else
		{
			vkCmdBindPipeline(ctx->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			for (auto&& chunk: chunks) {
				auto& vertex_buffer = vertex_buffers[chunk.vertex_buffer];
				if (vertex_buffer.index_count) {
					vkCmdBindVertexBuffers(ctx->command_buffer, 0, 1, vertex_buffer.get(), &offset);
					vkCmdDrawIndexed(ctx->command_buffer, vertex_buffer.index_count, 1, 0, 0, 0);
					used_vertex_gpu_memory += (vertex_buffer.index_count/3)*2;
				}
			}
		}

#if !LOBATOMY
	//	auto view_projection = cc.get_skybox_transform();
	//	skybox.draw(ctx, &view_projection);
#endif

		engine.debug_layer.add("rendered chunk count: %i", chunks.size());
		engine.debug_layer.add("chunk count: %i", loaded_chunks.size());
		engine.debug_layer.add("cache misses: %i", cache_misses);
		engine.debug_layer.add("max vertex count: %i", max_vertex_count);

#if LOBATOMY
		blend.end(dt, ctx, 0.9f);
#else
		render_pass.end(ctx);
#endif
	}
};
