#include <Fission/Core/Engine.hh>
#include "gfx.h"
#include "Camera_Controller.h"
#include <stb_perlin.h>

struct LOD_Tree {
	LOD_Tree* q00 = nullptr;
	LOD_Tree* q01 = nullptr;
	LOD_Tree* q10 = nullptr;
	LOD_Tree* q11 = nullptr;
	fs::v2f32 center = {};
	int lod = 0; // LOD of 0 means that there is no chunk here, we are a parent.

	LOD_Tree(int lod) : lod(lod) {}
	LOD_Tree(int lod, fs::v2f32 center) : lod(lod), center(center) {}
};

// @TODO: only need a lookup table, no need for switch
fs::rf32 quadrant(fs::rf32 const& rect, int quadrant) {
	auto q = rect.size() * 0.25f;
	auto half = rect.size() * 0.5f;
	switch (quadrant)
	{
	default: /* 0 */ {
		return fs::rf32::from_topleft(rect.topLeft(), half);
	}
	break; case 0b01: {
		return fs::rf32::from_topleft(rect.topLeft() + fs::v2f32(0.0f, rect.height() * 0.5f), half);
	}
	break; case 0b10: {
		return fs::rf32::from_topleft(rect.topLeft() + fs::v2f32(rect.width() * 0.5f, 0.0f), half);
	}
	break; case 0b11: {
		return fs::rf32::from_topleft(rect.topLeft() + rect.size() * 0.5f, half);
	}
	break;
	}
}

struct World {
	float render_distance;
	LOD_Tree* root;

	World() {
		using namespace fs;
		root = new LOD_Tree(0);
		int max_lod = 8;
		render_distance = float((8) << max_lod);

		auto generate_lod_sub_tree = [](LOD_Tree* node, rf32 rect, int max, int offset) {
			auto p = reinterpret_cast<LOD_Tree**>(node);
			for (int i = 1; i <= max; ++i) {
				for (auto k = 0; k < 4; ++k) {
					if (k == offset) continue;
					p[k] = new LOD_Tree(i, quadrant(rect, k).center());
				}
				p[offset] = new LOD_Tree((i == max)*max, quadrant(rect, offset).center());
				p = reinterpret_cast<LOD_Tree**>(p[offset]);
				rect = quadrant(rect, offset);
			}
		};

		auto base = rf32::from_center(2.0f*render_distance, 2.0f*render_distance);

		generate_lod_sub_tree(root->q00 = new LOD_Tree(0), quadrant(base, 0b00), max_lod, 0b11);
		generate_lod_sub_tree(root->q01 = new LOD_Tree(0), quadrant(base, 0b01), max_lod, 0b10);
		generate_lod_sub_tree(root->q10 = new LOD_Tree(0), quadrant(base, 0b10), max_lod, 0b01);
		generate_lod_sub_tree(root->q11 = new LOD_Tree(0), quadrant(base, 0b11), max_lod, 0b00);

	//	fix_lod(root, 4);
	}

	auto constexpr lod_from_distance(float dist) -> int {
		return int(dist) >> 8;
	}

	auto fix_lod(LOD_Tree* node, int min_lod) -> void {
		if (node == nullptr) return;
		auto lod = node->lod;
		if (lod != 0 && lod < min_lod) {
			node->lod = 0;
			float size = render_distance / float(1 << lod);
			fs::rf32 rect = fs::rf32::from_center(node->center, size, size);
			node->q00 = new LOD_Tree(lod+1, quadrant(rect, 0b00).center());
			node->q01 = new LOD_Tree(lod+1, quadrant(rect, 0b01).center());
			node->q10 = new LOD_Tree(lod+1, quadrant(rect, 0b10).center());
			node->q11 = new LOD_Tree(lod+1, quadrant(rect, 0b11).center());
		}
		fix_lod(node->q00, min_lod);
		fix_lod(node->q01, min_lod);
		fix_lod(node->q10, min_lod);
		fix_lod(node->q11, min_lod);
	}
};

struct Mesh {
	struct vertex {
		fs::v3f32 position;
	};

	std::vector<vertex> vertex_array;
};

namespace removethis {
	inline float xoff = 0.0f;
	inline float yoff = 0.0f;
}
float height_map(float x, float y) {
	return 3.0f * stb_perlin_fbm_noise3((x + removethis::xoff)*0.10346f, (y + removethis::yoff)*0.10346f, 0.0f, 2.0f, 0.5f, 8);
//	return 4.0f * stb_perlin_noise3((x + removethis::xoff)*0.10346f, (y + removethis::yoff)*0.10346f, 0.0f, 0, 0, 0);
}

static constexpr int Vc = 32;

void generate_mesh(Mesh& m, fs::v2f32 center, int lod, float rd) {
	float size = rd / float(1 << lod);
	fs::rf32 rect = fs::rf32::from_center(center, size, size);

	m.vertex_array.clear();
	m.vertex_array.reserve((Vc+1)*(Vc+1));

	for (int y = 0; y < Vc+1; ++y)
	for (int x = 0; x < Vc+1; ++x) {
		float xx = fs::lerp(rect.x.low, rect.x.high, float(x)/float(Vc));
		float yy = fs::lerp(rect.y.low, rect.y.high, float(y)/float(Vc));

		m.vertex_array.emplace_back(Mesh::vertex{ .position = {xx,height_map(xx, yy),yy} });
	}
}

void recursive_gen(std::vector<Mesh>& out, int& i, LOD_Tree* node, float rd) {
	if (node == nullptr) return;
	if (node->lod != 0) {
		generate_mesh(out[i++], node->center, node->lod, rd);
	}
	recursive_gen(out, i, node->q00, rd);
	recursive_gen(out, i, node->q01, rd);
	recursive_gen(out, i, node->q10, rd);
	recursive_gen(out, i, node->q11, rd);
}

void generate_meshes_from_world(World const& w, std::vector<Mesh>& out) {
	out.clear();
	out.resize(1<<14);
	int i = 0;
	recursive_gen(out, i, w.root, w.render_distance);
	out.resize(i);
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
	VmaAllocation vertex_allocation;
	VkBuffer      vertex_buffer;

	VkImageView   depth_view;
	gfx::Image    depth_image;

	VkImageView   color_view;
	gfx::Image    color_image;

	VkFramebuffer frame_buffers[fs::Graphics::max_sc_images];

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

	uint32_t index_count = 0;
	uint32_t vertex_count = 0;
	uint32_t mesh_count = 0;

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

		vk::Basic_Vertex_Input<fs::v3f32> vi;
		auto pipeline_creator = Pipeline_Creator{ render_pass, pipeline_layout }
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

		{
			VmaAllocationCreateInfo allocInfo = {};
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };

			bufferInfo.size = (1 << 20) * sizeof(fs::v3f32);
			bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			vmaCreateBuffer(gfx.allocator, &bufferInfo, &allocInfo, &vertex_buffer, &vertex_allocation, nullptr);

			bufferInfo.size = (1 << 15) * sizeof(fs::u32);
			bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			vmaCreateBuffer(gfx.allocator, &bufferInfo, &allocInfo, &index_buffer, &index_allocation, nullptr);
		}
		{
			std::vector<fs::u32> index_array;
			int w = Vc+1, h = Vc+1;
			for (int y = 0; y < h - 1; ++y)
			for (int x = 0; x < w - 1; ++x) {
				auto i = y * w + x;
				index_array.emplace_back(i);
				index_array.emplace_back(i + 1);
				index_array.emplace_back(i + w);
				index_array.emplace_back(i + 1);
				index_array.emplace_back(i + w + 1);
				index_array.emplace_back(i + w);
			}

			uint32_t* gpu_index_buffer;
			vmaMapMemory(engine.graphics.allocator, index_allocation, (void**)&gpu_index_buffer);
			for (auto&& i : index_array) gpu_index_buffer[index_count++] = i;
			vmaUnmapMemory(engine.graphics.allocator, index_allocation);
			vmaFlushAllocation(engine.graphics.allocator, index_allocation, 0, sizeof(fs::u16) * index_count);
		}
	}
	void destroy() {
		vmaDestroyBuffer(engine.graphics.allocator, vertex_buffer, vertex_allocation);
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
	}

	void add_meshes(std::vector<Mesh>& meshes) {
		using vertex = Mesh::vertex;
		vertex   *gpu_vertex_buffer;
		
		vmaMapMemory(engine.graphics.allocator, vertex_allocation, (void**)&gpu_vertex_buffer);
		vertex_count = 0;
		for (auto&& m: meshes) {
			uint32_t count = m.vertex_array.size();
			memcpy(gpu_vertex_buffer + vertex_count, m.vertex_array.data(), sizeof(vertex)*count);
			vertex_count += count;
			mesh_count += 1;
		}
		vmaUnmapMemory(engine.graphics.allocator, vertex_allocation);
		vmaFlushAllocation(engine.graphics.allocator, vertex_allocation, 0, sizeof(vertex)*vertex_count);
	}

	void draw(fs::Render_Context* ctx, Camera_Controller const& cc, bool wireframe) {
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

		vkCmdBindIndexBuffer(ctx->command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(ctx->command_buffer, 0, 1, &vertex_buffer, &offset);

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

		uint32_t s = (Vc+1)*(Vc+1);
		if (wireframe) {
			vkCmdBindPipeline(ctx->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_pipeline);
			FS_FOR(mesh_count) vkCmdDrawIndexed(ctx->command_buffer, index_count, 1, 0, s*i, 0);

			vkCmdBindPipeline(ctx->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe_pipeline);
			FS_FOR(mesh_count) vkCmdDrawIndexed(ctx->command_buffer, index_count, 1, 0, s*i, 0);
		}
		else {
			vkCmdBindPipeline(ctx->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			FS_FOR(mesh_count) vkCmdDrawIndexed(ctx->command_buffer, index_count, 1, 0, s*i, 0);
		}
		

		engine.debug_layer.add("i: %i, V: %i", index_count, vertex_count);
		render_pass.end(ctx);
	}
};
