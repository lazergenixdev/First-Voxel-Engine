#include "gfx.h"
#include "Camera_Controller.h"
#include "world.hpp"

struct World_Renderer {
	World_Renderer() = default;

	auto create(fs::Graphics& gfx, VkRenderPass render_pass) -> void;
	auto destroy() -> void;
	auto resize_frame_buffers() -> void;

	auto upload_world(World& world) -> void;

	auto draw(
		fs::Render_Context& ctx,
		Camera_Controller const& cc,
		World& world,
		float dt
	) -> void;

	auto create_vertex_buffers() -> void;

	fs::Render_Pass  render_pass;
	VkPipelineLayout pipeline_layout;
	VkPipeline       pipeline;
	VkBuffer         index_buffer;
	VmaAllocation    index_allocation;
	VkImageView      depth_view = nullptr;
	gfx::Image       depth_image;
	VkImageView      color_view;
	gfx::Image       color_image;
	VkFramebuffer    frame_buffers[fs::Graphics::max_sc_images];
	VkFormat         depth_format = VK_FORMAT_D32_SFLOAT;
	VkDescriptorSet  transform_set;
	VkBuffer         transform_buffer;
	VmaAllocation    transform_allocation;

	VkBuffer         vertex_buffer[2] = {};
	VmaAllocation    vertex_allocation[2] = {};
	Vertex*          mapped_vertex_gpu_data[2] = {};
	fs::u32          vbi = 0; // vertex buffer index

	Vertex*          vertex_cpu_data = nullptr;

	struct Chunk_Draw_Data {
		fs::v3s32 position;
		fs::u32   lod;
		fs::u32   vertex_offset;
		fs::u32   index_count;
	};

	std::vector<Chunk_Draw_Data> chunks;

	struct Debug_Vertex {
		fs::v3f32 position;
	};
	VkPipeline       debug_pipeline;
	VkBuffer         debug_vertex_buffer;
	VmaAllocation    debug_vertex_allocation;
	Debug_Vertex*    debug_mapped_vertex_data = nullptr;
	bool             debug_show_chunk_bounds = false;

	VkPipeline       debug_wireframe_pipeline;

	float t = 0.0f;
	
	bool             debug_wireframe = false;
	bool             use_render_pass = true;

	struct Vertex_Push {
		fs::v4f32 position_offset;
		fs::u32 lod;
	};
	struct Transform_Data {
		glm::mat4 projection;
		float time;
	};

private:
	auto begin_render(fs::Render_Context& ctx) -> void;
	auto   end_render(fs::Render_Context& ctx) -> void;
};
