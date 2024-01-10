#define RADIUS 32
#include <Fission/Core/Engine.hh>
#include <Fission/Core/Input/Keys.hh>
#include <Fission/Base/Time.hpp>
#include <Fission/Base/Math/Vector.hpp>
#include <random>
#include <format>
#include <execution>
#include <fstream>
#include "Camera_Controller.h"
#include "Skybox.h"
#include "outline_technique.h"
#define STB_IMAGE_IMPLEMENTATION 1
#include <stb_image.h>
#define STB_PERLIN_IMPLEMENTATION 1
#include <stb_perlin.h>
#include "mesher.hpp"

#define SQ(X) (X*X)
#define MAP2D(X,Y,WIDTH) (Y * WIDTH + X)

extern void display_fatal_error(const char* title, const char* what);
extern void generateMipmaps(fs::Graphics& gfx, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

static constexpr int world_chunk_height = 16;
int quad_count = 0;
// in bytes:
int64_t total_gpu_memory = 0;
int64_t used_gpu_memory = 0;

struct vertex {
	fs::v3f32 position;
	fs::v3f32 texcoord;
};

#define TERRAIN_BLOBS     0
#define TERRAIN_MOUNTAINS 1
#define TERRAIN TERRAIN_MOUNTAINS
auto terrain_height_from_location(int x, int z) -> float {
//	return 1.0f * stb_perlin_ridge_noise3(float(x) / 64.0f, float(z) / 64.0f, 0.225f, 2.0f, 0.5f, 1.0f, 6); // mountains
	return 1.0f + 0.7f * stb_perlin_fbm_noise3(float(x) / 64.0f, float(z) / 64.0f, 0.435f, 2.0f, 0.5f, 8); // bumpy
//	return 0.5f + 0.2f * stb_perlin_fbm_noise3(float(x) / 64.0f, float(z) / 64.0f, 0.435f, 2.0f, 0.3f, 8); // flat
}

static std::mt19937 rng{ (unsigned int)time(nullptr) }; /* Use current time as seed for rng. */
static std::uniform_int_distribution<unsigned int> dist{ 0, UINT_MAX };
static std::uniform_int_distribution<unsigned int> dist_h{ 0, 8 };

struct Chunk_Mesh {
	VmaAllocation vertex_allocation;
	VkBuffer      vertex_buffer;

	static VmaAllocation index_allocation;
	static VkBuffer      index_buffer;
	
	int index_count;

	bool ready_to_render = false;

	int number_of_quads = 0;
	int bytes_used = 0;

	static constexpr fs::u32 max_vertex_count = 1 << 11;

	auto create(fs::Graphics& gfx) -> void {
		VmaAllocationCreateInfo ai = {};
		ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };

		bi.size = max_vertex_count * sizeof(vertex);
		bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		auto vkr = vmaCreateBuffer(gfx.allocator, &bi, &ai, &vertex_buffer, &vertex_allocation, nullptr);
		if (vkr) {
			VmaTotalStatistics stats;
			vmaCalculateStatistics(gfx.allocator, &stats);
			__debugbreak();
		}
		total_gpu_memory += bi.size;

		if (!index_allocation) {
			bi.size = (max_vertex_count * 6) / 4 * sizeof(fs::u16);
			bi.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			ai.flags = 0;
			vmaCreateBuffer(gfx.allocator, &bi, &ai, &index_buffer, &index_allocation, nullptr);

			static constexpr char index_list[] = {
				0, 1, 2, 2, 3, 0,
			};

			fs::u16* id = new fs::u16[bi.size];
			for_n (v, max_vertex_count/4)
			FS_FOR(6) id[index_count++] = v*4 + index_list[i];
			index_count = 0;
			gfx.upload_buffer(index_buffer, id, bi.size);
		}
	}
	auto destroy(fs::Graphics& gfx) -> void {
		vmaDestroyBuffer(gfx.allocator, vertex_buffer, vertex_allocation);
		if (index_allocation) {
			vmaDestroyBuffer(gfx.allocator, index_buffer, index_allocation);
			index_allocation = nullptr;
		}
	}

	struct Upload_Context {
		vertex* vertex_data;
		fs::u16* index_data;
		int vertex_count;
	};

	auto upload_begin(fs::Graphics& gfx) -> Upload_Context {
		ready_to_render = false;
		index_count = 0;
		vertex * vd;
		vmaMapMemory(gfx.allocator, vertex_allocation, (void**)&vd);
		used_gpu_memory -= bytes_used;
		quad_count -= number_of_quads;
		number_of_quads = 0;
		return { vd, nullptr, 0 };
	}
	auto upload_end(fs::Graphics& gfx, Upload_Context& ctx) -> void {
		vmaUnmapMemory(gfx.allocator, vertex_allocation);
		vmaFlushAllocation(gfx.allocator, vertex_allocation, 0, ctx.vertex_count * sizeof(vertex));
		bytes_used = ctx.vertex_count * sizeof(vertex);
		used_gpu_memory += bytes_used;
		quad_count += number_of_quads;
		ready_to_render = true;
	}

	auto add_chunk_quads (Upload_Context& ctx, std::vector<quad>& quads, int oy) {
		auto& [vd, id, vertex_count] = ctx;
		for (auto const& q : quads) {
			auto na = q.normal_axis;

			if (na < 0) {
				na += 3;
			}

			union vec3 {
				char comp[4];
				struct {
					char x, y, z, _;
				};
			};
			vec3 p00, p01, p10, p11;
			int i_axis = (na + 1) % 3;
			int j_axis = (na + 2) % 3;

			p00.comp[na] = q.slice;
			p00.comp[i_axis] = q.i0;
			p00.comp[j_axis] = q.j0;

			p01.comp[na] = q.slice;
			p01.comp[i_axis] = q.i0;
			p01.comp[j_axis] = q.j1;

			p10.comp[na] = q.slice;
			p10.comp[i_axis] = q.i1;
			p10.comp[j_axis] = q.j0;

			p11.comp[na] = q.slice;
			p11.comp[i_axis] = q.i1;
			p11.comp[j_axis] = q.j1;

			float normal = 0.5f + float(q.normal_axis + 3);
			float u1 = float(q.i1 - q.i0), v1 = float(q.j1 - q.j0);
			if (q.normal_axis >= 0) {
				vd[vertex_count++] = { fs::v3f32(float(p00.x), float(p00.y + oy), float(p00.z)), {0.0f, 0.0f, normal} };
				vd[vertex_count++] = { fs::v3f32(float(p01.x), float(p01.y + oy), float(p01.z)), {0.0f, v1  , normal} };
				vd[vertex_count++] = { fs::v3f32(float(p11.x), float(p11.y + oy), float(p11.z)), {u1  , v1  , normal} };
				vd[vertex_count++] = { fs::v3f32(float(p10.x), float(p10.y + oy), float(p10.z)), {u1  , 0.0f, normal} };
			} else {
				vd[vertex_count++] = { fs::v3f32(float(p01.x), float(p01.y + oy), float(p01.z)), {0.0f, v1  , normal} };
				vd[vertex_count++] = { fs::v3f32(float(p00.x), float(p00.y + oy), float(p00.z)), {0.0f, 0.0f, normal} };
				vd[vertex_count++] = { fs::v3f32(float(p10.x), float(p10.y + oy), float(p10.z)), {u1  , 0.0f, normal} };
				vd[vertex_count++] = { fs::v3f32(float(p11.x), float(p11.y + oy), float(p11.z)), {u1  , v1  , normal} };
			}
			index_count += 6;
		}
		number_of_quads += (int)quads.size();
	};

	auto draw(fs::Render_Context* ctx) -> void {
		VkDeviceSize offset = 0;
		vkCmdBindIndexBuffer(ctx->command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16);
		vkCmdBindVertexBuffers(ctx->command_buffer, 0, 1, &vertex_buffer, &offset);
		vkCmdDrawIndexed(ctx->command_buffer, index_count, 1, 0, 0, 0);
	}
};

VmaAllocation Chunk_Mesh::index_allocation;
VkBuffer      Chunk_Mesh::index_buffer;

struct World {
	using Chunk_Mask = fs::byte[8*8];

	int render_radius;

	fs::v3s32 chunk_offset;

	struct Chunk_Column {
		Chunk_Mask y[world_chunk_height];
	};

	std::vector<Chunk_Column> chunk_columns;

	std::vector<int> xz_map; // maps (x,z) location to chunk column index
	std::vector<Chunk_Mesh> meshes;

	World() {
		render_radius = RADIUS;

		int c_diameter = chunk_diameter();
		for (int z = 0; z < c_diameter; ++z)
		for (int x = 0; x < c_diameter; ++x) {
			chunk_columns.emplace_back();
			xz_map.emplace_back(MAP2D(x,z,c_diameter));
		}
	}

	auto create(fs::Graphics& gfx) -> void {
		meshes.reserve(SQ(render_diameter()));
		FS_FOR(SQ(render_diameter())) {
			meshes.emplace_back();
			meshes.back().create(gfx);
		}
	}
	auto destroy(fs::Graphics& gfx) -> void {
		for (auto& mesh : meshes)
			mesh.destroy(gfx);
	}

	auto render_diameter() -> int { return render_radius * 2 + 1; }
	auto chunk_diameter()  -> int { return render_radius * 2 + 3; }

	auto recenter_chunks(fs::Graphics& gfx, fs::v3s32 chunk_position) -> void {
		auto euclidean_remainder = [](int a, int b) -> int {
			int r = a % b;
			return r + b*(r < 0);
		};

		// 1. calculate new chunk offset
		auto new_chunk_offset = fs::v3s32(chunk_position.x - render_radius, 0, chunk_position.z - render_radius);
		
		// 2. move chunks to be consistant with the new chunk offset
		// this move vector describes how to move the chunks
		auto move = chunk_offset - new_chunk_offset;
		// this look vector describes the offset to the chunk that replaces any given chunk
		auto look = new_chunk_offset - chunk_offset;
		int c_diameter = chunk_diameter();
		
		// 3. calculate chunk data for all new chunks
		std::vector<int> new_xz_map;
		new_xz_map.reserve(xz_map.size());
		for (int z = 0; z < c_diameter; ++z)
		for (int x = 0; x < c_diameter; ++x) {
			int lx = x + look.x;
			int lz = z + look.z;

			bool good = (lx >= 0 && lx < c_diameter && lz >= 0 && lz < c_diameter);

			lx = euclidean_remainder(lx, c_diameter);
			lz = euclidean_remainder(lz, c_diameter);

			new_xz_map.emplace_back(xz_map[MAP2D(lx, lz, c_diameter)]);

			// need to regenerate chunk block mask
			if (!good) {
				auto column_index = new_xz_map.back();
				generate_chunk_column(chunk_columns[column_index], {x + new_chunk_offset.x, 0, z + new_chunk_offset.z});
			}
		}
		std::swap(xz_map, new_xz_map);

		// 4. calculate mesh data for all new chunks
		std::vector<Chunk_Mesh> new_meshes;
		new_meshes.resize(meshes.size());

		fs::u8 default_mask[8 * 8] = {};
		memset(default_mask, 0xFF, sizeof(default_mask));
		
		int r_diameter = render_diameter();
		for (int z = 0; z < r_diameter; ++z)
		for (int x = 0; x < r_diameter; ++x) {
			int lx = x + look.x;
			int lz = z + look.z;
	
			bool good = (lx >= 0 && lx < r_diameter && lz >= 0 && lz < r_diameter);
	
			lx = euclidean_remainder(lx, r_diameter);
			lz = euclidean_remainder(lz, r_diameter);
	
			new_meshes[MAP2D(x, z, r_diameter)] = meshes[MAP2D(lx, lz, r_diameter)];
			
			// need to regenerate mesh
			if (!good) {
				generate_mesh(gfx, x, z, new_meshes[MAP2D(x, z, r_diameter)], default_mask);
			}
		}
		std::swap(meshes, new_meshes);

		chunk_offset = new_chunk_offset;
	}

	auto generate_mesh(fs::Graphics& gfx, int x, int z, Chunk_Mesh& mesh, fs::byte* default_mask) -> void {
		int c_diameter = chunk_diameter();
		int r_diameter = render_diameter();

		std::vector<quad> quads;
		quads.reserve(1 << 10);
		adjacent_chunks adj;

		fs::byte empty[8*8] = {};

		auto base_column_index = xz_map[MAP2D((x + 1), (z + 1), c_diameter)];
		auto& column = chunk_columns[base_column_index];

		auto pos_x_column_index = xz_map[MAP2D((x), (z + 1), c_diameter)];
		auto neg_x_column_index = xz_map[MAP2D((x + 2), (z + 1), c_diameter)];
		auto pos_z_column_index = xz_map[MAP2D((x + 1), (z), c_diameter)];
		auto neg_z_column_index = xz_map[MAP2D((x + 1), (z + 2), c_diameter)];

		auto ctx = mesh.upload_begin(gfx);
		for_n(y, world_chunk_height) {
			adj.pos[0] = chunk_columns[pos_x_column_index].y[y];
			adj.pos[2] = chunk_columns[pos_z_column_index].y[y];
			adj.neg[0] = chunk_columns[neg_x_column_index].y[y];
			adj.neg[2] = chunk_columns[neg_z_column_index].y[y];
			adj.pos[1] = (y == 0) ? default_mask : column.y[y - 1];
			adj.neg[1] = (y == world_chunk_height-1) ? empty : column.y[y + 1];
			generate_quads_for_chunk(column.y[y], &adj, quads);
			mesh.add_chunk_quads(ctx, quads, y * 8);
			quads.clear();
		}
		mesh.upload_end(gfx, ctx);
	}

	auto draw(fs::Render_Context* ctx, VkPipelineLayout layout) {
		int r_diameter = render_diameter();
		for (int z = 0; z < r_diameter; ++z)
		for (int x = 0; x < r_diameter; ++x) {
			auto& mesh = meshes[MAP2D(x, z, r_diameter)];
			glm::vec4 pos = glm::vec4(float(x), 0.0f, float(z), 0.0);
			vkCmdPushConstants(
				ctx->command_buffer,
				layout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				64,
				sizeof(pos),
				&pos
			);
			mesh.draw(ctx);
		}
	}

	auto generate_mesh_for_all_chunks(fs::Graphics& gfx) -> void {
		quad_count = 0;
		int r_diameter = render_diameter();
		int c_diameter = chunk_diameter();

		fs::u8 default_mask[8 * 8] = {};
		memset(default_mask, 0xFF, sizeof(default_mask));

#if 0
		struct Thread_Info {
			int x, z;
			Chunk_Mesh* mesh;
			Chunk_Mesh::Upload_Context ctx;
		};
		std::vector<Thread_Info> infos;
		infos.reserve(r_diameter * r_diameter);
		for (int z = 0; z < r_diameter; ++z)
		for (int x = 0; x < r_diameter; ++x) {
			auto& mesh = meshes[MAP2D(x, z, r_diameter)];
			infos.emplace_back(x, z, &mesh, mesh.upload_begin(gfx));
		}
		std::for_each(std::execution::par, infos.begin(), infos.end(), [&](Thread_Info& info) {
			auto& [x, z, mesh, ctx] = info;
			auto base_column_index = xz_map[MAP2D((x+1),(z+1),c_diameter)];
			auto& column = chunk_columns[base_column_index];

			auto pos_x_column_index = xz_map[MAP2D((x  ),(z+1),c_diameter)];
			auto neg_x_column_index = xz_map[MAP2D((x+2),(z+1),c_diameter)];
			auto pos_z_column_index = xz_map[MAP2D((x+1),(z  ),c_diameter)];
			auto neg_z_column_index = xz_map[MAP2D((x+1),(z+2),c_diameter)];

			std::vector<quad> quads;
			for_n (y, world_chunk_height) {
				adjacent_chunks adj;
				adj.pos[0] = chunk_columns[pos_x_column_index].y[y];
				adj.pos[2] = chunk_columns[pos_z_column_index].y[y];
				adj.neg[0] = chunk_columns[neg_x_column_index].y[y];
				adj.neg[2] = chunk_columns[neg_z_column_index].y[y];
				adj.pos[1] = (y==0)? default_mask : column.y[y-1];
				adj.neg[1] = (y==world_chunk_height-1)? default_mask : column.y[y+1];
				generate_quads_for_chunk(column.y[y], &adj, quads);
				mesh->add_chunk_quads(ctx, quads, y*8);
				quads.clear();
			}
		});
		for (auto& info: infos) {
			info.mesh->upload_end(gfx, info.ctx);
		}
#else
		std::vector<quad> quads;
		quads.reserve(1 << 10);
		for (int z = 0; z < r_diameter; ++z)
		for (int x = 0; x < r_diameter; ++x) {
			generate_mesh(gfx, x, z, meshes[MAP2D(x, z, r_diameter)], default_mask);
		}
#endif
	}

	auto generate_all_chunks () -> void {
		int c_diameter = chunk_diameter();
		for (int z = 0; z < c_diameter; ++z)
		for (int x = 0; x < c_diameter; ++x) {
			auto column_index = xz_map[MAP2D(x,z,c_diameter)];
			generate_chunk_column(chunk_columns[column_index], {x + chunk_offset.x, 0, z + chunk_offset.z});
		}
	}

	auto generate_chunk_column (Chunk_Column& column, fs::v3s32 offset) -> void {
#if TERRAIN != TERRAIN_BLOBS
		float height_field[64];
		for (int z = 0; z < 8; ++z)
		for (int x = 0; x < 8; ++x)
		{
			height_field[z*8 + x] = terrain_height_from_location(x + offset.x*8, z + offset.z*8);
		}
#endif
	
		memset(&column, 0, sizeof(column));

		auto perlin_noise = [](int x, int y, int z) { return stb_perlin_noise3(float(x)/64.0f,float(y)/64.0f,float(z)/64.0f,0,0,0); };

		for_n (cy, world_chunk_height) {
			auto block_mask = column.y[cy];

			for (int y = 0; y < 8; ++y)
			for (int z = 0; z < 8; ++z)
			for (int x = 0; x < 8; ++x)
			{
#if TERRAIN != TERRAIN_BLOBS
				float height = height_field[z*8 + x];
				if (float(y + cy*8) / 64.0f < height) {
					block_mask[y*8 + z] |= (1 << x);
				}
#else
				block_mask[y * 8 + z] |= ((perlin_noise(x + offset.x*8, y + cy*8, z + offset.z*8) > 0.2f) << x);
#endif
			}
		}
	}
};

struct draw_data {
	int vertex_count = 0;
	int index_count = 0;
};

struct transform_data {
	glm::mat4 view_projection;
	glm::vec4 time;
	glm::vec4 light_dir;
	glm::vec4 eye_position;
};

static float _amplitude = 0.0f;
static float _time = 0.0f;

struct app_load_data {
	float x, y, z;
	float rx, ry;
	float fov;

	static constexpr const char* filename = "app_data.bin";

	static void load(Camera_Controller& cam) {
		app_load_data data;
		if (auto f = std::ifstream(filename, std::ios::binary)) {
			f.read((char*)&data, sizeof(data));
			cam.field_of_view = data.fov;
			cam.position.x = data.x;
			cam.position.y = data.y;
			cam.position.z = data.z;
			cam.view_rotation.x = data.rx;
			cam.view_rotation.y = data.ry;
		}
	}
	static void save(Camera_Controller const& cam) {
		app_load_data data;
		data.fov = cam.field_of_view;
		data.x = cam.position.x;
		data.y = cam.position.y;
		data.z = cam.position.z;
		data.rx = cam.view_rotation.x;
		data.ry = cam.view_rotation.y;
		if (auto f = std::ofstream(filename, std::ios::binary)) {
			f.write((char*)&data, sizeof(data));
		}
	}
};

extern fs::Engine engine;

struct Renderer {
	struct vert {
#include "../shaders/color.vert.inl"
	};
	struct frag {
#include "../shaders/color.frag.inl"
	};
	struct white_frag {
#include "../shaders/white.frag.inl"
	};

	vk::Pipeline     pipeline;
	vk::Pipeline     wireframe_pipeline;
	vk::Pipeline     depth_pipeline;
	VkPipelineLayout pipeline_layout;

	VkImageView     atlas_view;
	gfx::Image      atlas;
	VkDescriptorSet atlas_set;
	VkSampler       atlas_sampler;

	VkImageView     block_types_view;
	gfx::Image      block_types;
	VkSampler       sampler;
	VkDescriptorSet block_types_set;

	Skybox skybox;

	void create(VkRenderPass in_render_pass) {
		auto& gfx = engine.graphics;

		Pipeline_Layout_Creator{}
			.add_push_range(VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(transform_data))
			.add_layout(engine.texture_layout)
			.add_layout(engine.texture_layout)
			.create(&pipeline_layout);

		vk::Basic_Vertex_Input<decltype(vertex::position), decltype(vertex::texcoord)> vi{};
		auto pc = Pipeline_Creator{in_render_pass, pipeline_layout}
			.add_shader(VK_SHADER_STAGE_VERTEX_BIT, fs::create_shader(gfx.device, vert::size, vert::data))
			.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, fs::create_shader(gfx.device, frag::size, frag::data))
			.vertex_input(&vi)
			.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
			.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
		
		pc.rasterization_state.cullMode = VK_CULL_MODE_BACK_BIT;
		pc.rasterization_state.depthBiasEnable = VK_TRUE;
		pc.rasterization_state.depthBiasConstantFactor = 0.01f;
		pc.rasterization_state.depthBiasSlopeFactor    = 1.0f;
		pc.create(&pipeline);

		pc.create_no_fragment(&depth_pipeline);

		pc.rasterization_state.depthBiasEnable = VK_FALSE;
		pc.rasterization_state.polygonMode = VK_POLYGON_MODE_LINE;
		vkDestroyShaderModule(gfx.device, pc.shaders[1].module, nullptr);
		pc.shaders[1].module = fs::create_shader(gfx.device, white_frag::size, white_frag::data);
		pc.create_and_destroy_shaders(&wireframe_pipeline);

		auto image_format = VK_FORMAT_R8G8B8A8_UNORM;

		int w, h, comp;
		auto image_data = stbi_load("../assets/block_atlas_96x128.png", &w, &h, &comp, 4);

		Image_Creator ic(image_format, VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT, {.width = fs::u32(w), .height = fs::u32(h)});
		ic.image_info.mipLevels = 5;
		ic.create(atlas);

		gfx.upload_image(atlas.image, image_data, { fs::u32(w), fs::u32(h), 1 }, image_format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		free(image_data);

		generateMipmaps(gfx, atlas.image, image_format, w, h, ic.image_info.mipLevels);

		auto view_info = vk::image_view_2d(atlas.image, image_format);
		view_info.subresourceRange.levelCount = ic.image_info.mipLevels;
		vkCreateImageView(gfx.device, &view_info, nullptr, &atlas_view);

		VkDescriptorSetAllocateInfo desc_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		desc_info.descriptorPool = engine.descriptor_pool;
		desc_info.descriptorSetCount = 1;
		desc_info.pSetLayouts = &engine.texture_layout;
		vkAllocateDescriptorSets(gfx.device, &desc_info, &atlas_set);

		auto sampler_info = vk::sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
		vkCreateSampler(gfx.device, &sampler_info, nullptr, &sampler);
		sampler_info.minFilter = VK_FILTER_LINEAR;
		sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler_info.minLod = 0.0f;// static_cast<float>(ic.image_info.mipLevels - 2); // Optional
		sampler_info.maxLod = static_cast<float>(ic.image_info.mipLevels-1);
		vkCreateSampler(gfx.device, &sampler_info, nullptr, &atlas_sampler);

		{
			VkDescriptorImageInfo imageInfo;
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = atlas_view;
			imageInfo.sampler = atlas_sampler;
			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.descriptorCount = 1;
			write.dstBinding = 0;
			write.dstSet = atlas_set;
			write.pImageInfo = &imageInfo;
			vkUpdateDescriptorSets(gfx.device, 1, &write, 0, nullptr);
		}

		int render_chunk_diameter = 8;
		{
			auto format = VK_FORMAT_R8_UINT;
			
			VkImageCreateInfo image_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				.imageType = VK_IMAGE_TYPE_3D,
				.format = format,
				.extent = {.width = unsigned(render_chunk_diameter)*8, .height = unsigned(world_chunk_height)*8, .depth = unsigned(render_chunk_diameter)*8},
				.mipLevels = 1,
				.arrayLayers = 1,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			};
			VmaAllocationCreateInfo allocation_info{
			//	.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
				.usage = VMA_MEMORY_USAGE_AUTO,
			};

			auto vkr = vmaCreateImage(gfx.allocator, &image_info, &allocation_info, &block_types.image, &block_types.allocation, nullptr);

			upload();

			auto view_info = vk::image_view_2d(block_types.image, format);
			view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
			vkCreateImageView(gfx.device, &view_info, nullptr, &block_types_view);
		}
		{
			VkDescriptorSetAllocateInfo desc_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			desc_info.descriptorPool = engine.descriptor_pool;
			desc_info.descriptorSetCount = 1;
			desc_info.pSetLayouts = &engine.texture_layout;
			vkAllocateDescriptorSets(gfx.device, &desc_info, &block_types_set);
		}
		{
			VkDescriptorImageInfo imageInfo;
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = block_types_view;
			imageInfo.sampler = sampler;
			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.descriptorCount = 1;
			write.dstBinding = 0;
			write.dstSet = block_types_set;
			write.pImageInfo = &imageInfo;
			vkUpdateDescriptorSets(gfx.device, 1, &write, 0, nullptr);
		}

		skybox.create(gfx, in_render_pass);
	}

	void upload() {
		auto& gfx = engine.graphics;
		int render_chunk_diameter = 8;
		auto block_type_data = new fs::u8[render_chunk_diameter*render_chunk_diameter*world_chunk_height*8*8*8];

		FS_FOR(render_chunk_diameter*render_chunk_diameter*world_chunk_height*8*8*8) {
			block_type_data[i] = dist_h(rng);
		}

	//	for_n(x, render_chunk_diameter*8)
	//	for_n(z, render_chunk_diameter*8)
	//	for_n(y, 8*8) {
	//		block_type_data[z*render_chunk_diameter*8*8*8 + y*render_chunk_diameter*8 + x]
	//			= (y)%8;
	//	}

		gfx.upload_image(block_types.image, block_type_data, {unsigned(render_chunk_diameter)*8,unsigned(world_chunk_height)*8,unsigned(render_chunk_diameter)*8}, VK_FORMAT_R8_UINT);
	}

	void destroy() {
		auto& gfx = engine.graphics;
		vkDestroyPipelineLayout(gfx.device, pipeline_layout, nullptr);
		vkDestroyImageView(gfx.device, atlas_view, nullptr);
		vkDestroyImageView(gfx.device, block_types_view, nullptr);
		vkDestroySampler(gfx.device, sampler, nullptr);
		vkDestroySampler(gfx.device, atlas_sampler, nullptr);
		skybox.destroy(gfx);
	}

	void draw(fs::Render_Context& ctx, Camera_Controller const& cam, World& world, VkImage depth_image, bool wireframe, bool wireframe_depth) {
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(ctx.gfx->sc_extent.width);
		viewport.height = static_cast<float>(ctx.gfx->sc_extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(ctx.command_buffer, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = ctx.gfx->sc_extent;
		vkCmdSetScissor(ctx.command_buffer, 0, 1, &scissor);
		
#define vkCmdPushConstants_fv(OFFSET, SIZE, VALUES) \
		vkCmdPushConstants(ctx.command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, OFFSET, SIZE, VALUES);

		transform_data transform;
		transform.view_projection = cam.get_transform();
	//	transform.light_dir = glm::vec4(cam.get_view_direction(), 0.0f);
		transform.light_dir = glm::normalize(glm::vec4(sinf(_time), -1.0, cosf(_time), 0.0f));
		transform.eye_position = glm::vec4(cam.get_position(), 0.0f);
		vkCmdPushConstants_fv(0, sizeof(transform), &transform);
		
		VkDescriptorSet sets[] = { atlas_set, block_types_set };
		FS_VK_BIND_DESCRIPTOR_SETS(ctx.command_buffer, pipeline_layout, vk::count(sets), sets);

#if 1
		if (wireframe) {
			if (wireframe_depth) {
				vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_pipeline);
				world.draw(&ctx, pipeline_layout);
			}
			vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe_pipeline);
			world.draw(&ctx, pipeline_layout);
		}
		else {
			vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			world.draw(&ctx, pipeline_layout);
		}
#else
		vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		world.draw(&ctx, pipeline_layout);
		if (wireframe) {
			vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe_pipeline);
			world.draw(&ctx, pipeline_layout);
		}
#endif

		transform.view_projection = cam.get_skybox_transform();
		skybox.draw(&ctx, &transform.view_projection);
	}
};

class Game_Scene : public fs::Scene
{
public:
	Game_Scene() {
		app_load_data::load(camera_controller);
		
		outline_technique.create(engine.graphics);
		r.create(outline_technique.render_pass);

		last_chunk_position = camera_controller.get_chunk_position();

		world.chunk_offset = last_chunk_position - fs::v3s32(world.render_radius, 0, world.render_radius);
		world.create(engine.graphics);
		world.generate_all_chunks();
		world.generate_mesh_for_all_chunks(engine.graphics);
	}
	virtual ~Game_Scene() override {
		world.destroy(engine.graphics);
		app_load_data::save(camera_controller);
		r.destroy();
		outline_technique.destroy(engine.graphics);
	}

	void handle_event(fs::Event const& e) {
		if (camera_controller.handle_event(e)) return;
		switch (e.type)
		{
		default:
		break; case fs::Event_Key_Down: {
			if (e.key_down.key_id == fs::keys::F) {
				engine.window.set_mode((fs::Window_Mode)!(bool)engine.window.mode);
				engine.flags |= engine.fGraphics_Recreate_Swap_Chain;
			}
			else if (e.key_down.key_id == fs::keys::T)
				camera_controller.position = {};
			else if (e.key_down.key_id == fs::keys::L) {
				wireframe = !wireframe;
			}
			else if (e.key_down.key_id == fs::keys::K) {
			//	if (wireframe) 
			//		wireframe_depth = !wireframe_depth;
			//	else
					post_fx_enable = !post_fx_enable;
			}

			else if (e.key_down.key_id == fs::keys::Up)
				_amplitude += 0.1f;
			else if (e.key_down.key_id == fs::keys::Down)
				_amplitude -= 0.1f;

			else if (e.key_down.key_id == fs::keys::M)
				r.upload();
		}
		break; case fs::Event_Key_Up: {
			if (e.key_down.key_id == fs::keys::Escape)
				engine.window.set_using_mouse_detlas(!engine.window.is_using_mouse_deltas());
		}
		}
	}

	virtual void on_update(double _dt, std::vector<fs::Event> const& events, fs::Render_Context* ctx) override {
		for (auto&& e: events)
			handle_event(e);

		using namespace fs;
		float dt = (float)_dt;
		_time += dt;
		camera_controller.update(dt);

		static double generation_time = 0.0;
		auto current_chunk_position = camera_controller.get_chunk_position();
		if (current_chunk_position.x != last_chunk_position.x ||
			current_chunk_position.z != last_chunk_position.z) {
			
			auto start = fs::timestamp();
			last_chunk_position = current_chunk_position;
			world.recenter_chunks(engine.graphics, last_chunk_position);
			generation_time = fs::seconds_elasped(start, fs::timestamp());
		}

		outline_technique.post_fx_enable = post_fx_enable;
		outline_technique.begin(ctx);
		r.draw(*ctx, camera_controller, world, outline_technique.depth_image.image, wireframe, wireframe_depth);
		outline_technique.end(ctx);

		auto P = glm::ivec3(glm::floor(camera_controller.position));
		auto C = camera_controller.get_chunk_position();
		engine.debug_layer.add("Position: %i,%i,%i  Chunk: %i,%i,%i", P.x, P.y, P.z, C.x, C.y, C.z);
		engine.debug_layer.add("render wireframe: %s", FS_BTF(wireframe));
		float FOV = camera_controller.field_of_view;
		engine.debug_layer.add("FOV: %.2f (%.1f deg)", FOV, FOV * (360.0f/float(FS_TAU)));
		engine.debug_layer.add("number of quads: %i", quad_count);
		engine.debug_layer.add("generation time: %.1f ms", generation_time*1e3);
		engine.debug_layer.add("GPU memory usage: %.2f%% / %.3f MiB", double(100*used_gpu_memory)/double(total_gpu_memory), double(total_gpu_memory)/double(1024*1024));
	}

	virtual void on_resize() override {
		outline_technique.resize(engine.graphics);
	}
private:
	Camera_Controller camera_controller;
	World world;
	bool wireframe = false;
	bool wireframe_depth = true;
	bool post_fx_enable  = true;
	int block_count = 0;

	fs::v3s32 last_chunk_position;

	Renderer r;
	Outline_Technique outline_technique;
};

fs::Scene* on_create_scene(fs::Scene_Key const& key) {
	return new Game_Scene;
}

fs::Defaults on_create() {
	engine.app_name = FS_str("\"something\"");
	engine.app_version = {2, 2, 0};
	engine.app_version_info = FS_str("ok");
	static auto title = std::format("minecraft clone [{}]", engine.get_version_string().str());
	return {
		.window_title = FS_str_std(title),
	};
}
