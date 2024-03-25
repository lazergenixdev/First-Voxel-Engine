#include "World_Renderer.hpp"

struct vert_shader {
#include "../shaders/color.vert.inl"
};
struct frag_shader {
#include "../shaders/color.frag.inl"
};

struct debug_vs {
#include "../shaders/debug.vert.inl"
};
struct debug_fs {
#include "../shaders/debug.frag.inl"
};

struct white_fs {
#include "../shaders/white.frag.inl"
};

//auto constexpr max_index_count_per_chunk = (max_vertex_count_per_chunk / 2) * 3;
auto constexpr max_index_count_per_chunk = ((1 << 16)/6) * 6;

auto World_Renderer::create(fs::Graphics& gfx, VkRenderPass in_render_pass) -> void {
	Render_Pass_Creator{ 3 }
		.add_attachment(engine.graphics.sc_format, VK_SAMPLE_COUNT_8_BIT)
		.add_attachment(depth_format, VK_SAMPLE_COUNT_8_BIT)
		.add_attachment(engine.graphics.sc_format)
		.add_subpass(
			{{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},
			{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
			{2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
		)
		.add_external_subpass_dependency(0)
		.create(&render_pass.handle);

	transform_layout.create(gfx);

	Pipeline_Layout_Creator{}
		.add_push_range(VK_SHADER_STAGE_VERTEX_BIT, sizeof(Vertex_Push))
		.add_layout(transform_layout)
		.create(&pipeline_layout);

	auto vs = fs::create_shader(gfx.device, vert_shader::size, vert_shader::data);
	auto fs = fs::create_shader(gfx.device, frag_shader::size, frag_shader::data);

	auto enable_blending = [](auto& state, bool add) {
		state.blendEnable = VK_TRUE;
		state.colorBlendOp = VK_BLEND_OP_ADD;
		state.alphaBlendOp = VK_BLEND_OP_ADD;
		state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		if (add) {
			state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		}

	};
	auto disable_blending = [](auto& state) {
		state.blendEnable = VK_FALSE;
	};

	vk::Basic_Vertex_Input<decltype(Vertex::data)> vi;
	auto pipeline_creator = Pipeline_Creator{ in_render_pass, pipeline_layout }
		.add_shader(VK_SHADER_STAGE_VERTEX_BIT,   vs)
		.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, fs)
		.vertex_input(&vi)
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
	
	pipeline_creator.rasterization_state.cullMode = CPU_SIDE_BACKFACE_CULLING? VK_CULL_MODE_NONE:VK_CULL_MODE_BACK_BIT;
//	pipeline_creator.multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
//	pipeline_creator.depth_stencil_state.depthTestEnable = VK_FALSE;
//	enable_blending(pipeline_creator.blend_attachment, true);
	pipeline_creator.create(&pipeline);

	vkDestroyShaderModule(gfx.device, pipeline_creator.shaders[1].module, nullptr);
	pipeline_creator.shaders[1].module = fs::create_shader(gfx.device, white_fs::size, white_fs::data);
	pipeline_creator.rasterization_state.depthBiasConstantFactor = -0.5f;
	pipeline_creator.rasterization_state.depthBiasSlopeFactor    = 0.0f;
	pipeline_creator.rasterization_state.depthBiasEnable         = VK_TRUE;
	pipeline_creator.rasterization_state.polygonMode = VK_POLYGON_MODE_LINE;
	disable_blending(pipeline_creator.blend_attachment);
	pipeline_creator.create_and_destroy_shaders(&debug_wireframe_pipeline);

	vk::Basic_Vertex_Input<fs::v3f32> debug_vi;
	pipeline_creator.vertex_input_state = &debug_vi;
	pipeline_creator.depth_stencil_state.depthTestEnable = VK_TRUE;
	pipeline_creator.rasterization_state.depthBiasEnable = VK_FALSE;
	pipeline_creator.rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
	pipeline_creator.rasterization_state.cullMode = VK_CULL_MODE_NONE;
	pipeline_creator.input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	enable_blending(pipeline_creator.blend_attachment, false);
	pipeline_creator.shaders[0].module = fs::create_shader(gfx.device, debug_vs::size, debug_vs::data);
	pipeline_creator.shaders[1].module = fs::create_shader(gfx.device, debug_fs::size, debug_fs::data);
	pipeline_creator.create_and_destroy_shaders(&debug_pipeline);

	resize_frame_buffers();

	{
		VmaAllocationCreateInfo allocInfo = {};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = max_index_count_per_chunk * sizeof(fs::u16);
		bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		vmaCreateBuffer(gfx.allocator, &bufferInfo, &allocInfo, &index_buffer, &index_allocation, nullptr);

		fs::u16 index_array[] = { 0, 1, 2, 2, 3, 0 };
		auto cpu_index_array = new fs::u16[max_index_count_per_chunk];
		FS_FOR(max_index_count_per_chunk/6) {
			[&]<size_t...S>(std::index_sequence<S...>) {
				( (cpu_index_array[i*6 + S] = index_array[S] + i*4), ... );
			}(std::make_index_sequence<std::size(index_array)>{});
		}
		gfx.upload_buffer(index_buffer, cpu_index_array, max_index_count_per_chunk * sizeof(fs::u16));
		delete [] cpu_index_array;
	}

	{
		VmaAllocationCreateInfo allocInfo = {};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = sizeof(Transform_Data);
		bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		vmaCreateBuffer(gfx.allocator, &bufferInfo, &allocInfo, &transform_buffer, &transform_allocation, nullptr);
	}

	{
		VmaAllocationCreateInfo allocInfo = {};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = (1 << 12) * sizeof(Debug_Vertex);
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		vmaCreateBuffer(engine.graphics.allocator, &bufferInfo, &allocInfo, &debug_vertex_buffer, &debug_vertex_allocation, nullptr);
	}

	{
		VkDescriptorSetLayout layouts[] = { transform_layout };
		VkDescriptorSetAllocateInfo descSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		descSetAllocInfo.descriptorPool = engine.descriptor_pool;
		descSetAllocInfo.pSetLayouts = layouts;
		descSetAllocInfo.descriptorSetCount = 1;
		vkAllocateDescriptorSets(gfx.device, &descSetAllocInfo, &transform_set);
	}

	{
		VkDescriptorBufferInfo bufferInfo;
		bufferInfo.buffer = transform_buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(Transform_Data);
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.descriptorCount = 1;
		write.dstBinding = 0;
		write.dstSet = transform_set;
		write.pBufferInfo = &bufferInfo;
		vkUpdateDescriptorSets(gfx.device, 1, &write, 0, nullptr);
	}

	create_vertex_buffers();
}

auto World_Renderer::create_vertex_buffers() -> void {
	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = 2000 * max_vertex_count_per_chunk * sizeof(Vertex);
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	FS_FOR(2) {
		vmaCreateBuffer(engine.graphics.allocator, &bufferInfo, &allocInfo, &vertex_buffer[i], &vertex_allocation[i], nullptr);
		vmaMapMemory(engine.graphics.allocator, vertex_allocation[i], (void**)&mapped_vertex_gpu_data[i]);
	}

//	vertex_cpu_data = new Vertex[2000 * max_vertex_count_per_chunk];
	total_vertex_gpu_memory += bufferInfo.size;
}

auto World_Renderer::destroy() -> void {
	delete[] vertex_cpu_data;
	FS_FOR(2) {
		vmaUnmapMemory(engine.graphics.allocator, vertex_allocation[i]);
		vmaDestroyBuffer(engine.graphics.allocator, vertex_buffer[i], vertex_allocation[i]);
	}
	vmaDestroyBuffer(engine.graphics.allocator, debug_vertex_buffer, debug_vertex_allocation);
	vmaDestroyBuffer(engine.graphics.allocator, transform_buffer, transform_allocation);
	vmaDestroyBuffer(engine.graphics.allocator, index_buffer, index_allocation);
	vkDestroyPipelineLayout(engine.graphics.device, pipeline_layout, nullptr);
	vkDestroyPipeline(engine.graphics.device, pipeline, nullptr);
	vkDestroyPipeline(engine.graphics.device, debug_wireframe_pipeline, nullptr);
	vkDestroyPipeline(engine.graphics.device, debug_pipeline, nullptr);
//	vkDestroyPipeline(engine.graphics.device, depth_pipeline, nullptr);
//	vkDestroyPipeline(engine.graphics.device, wireframe_pipeline, nullptr);
	vkDestroyImageView(engine.graphics.device, depth_view, nullptr);
	vkDestroyImageView(engine.graphics.device, color_view, nullptr);
	FS_FOR(engine.graphics.sc_image_count)
		vkDestroyFramebuffer(engine.graphics.device, frame_buffers[i], nullptr);
	transform_layout.destroy(engine.graphics);
	render_pass.destroy();
}

auto World_Renderer::resize_frame_buffers() -> void {
	auto& gfx = engine.graphics;
	if (depth_view) {
		depth_image.~Image();
		color_image.~Image();
		vkDestroyImageView(gfx.device, depth_view, nullptr);
		vkDestroyImageView(gfx.device, color_view, nullptr);
		FS_FOR (gfx.sc_image_count) vkDestroyFramebuffer(gfx.device, frame_buffers[i], nullptr);
	}

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

auto World_Renderer::upload_world(World& world) -> void {
	vbi = fs::u32(vbi == 0);
	vmaMapMemory(engine.graphics.allocator, debug_vertex_allocation, (void**)&debug_mapped_vertex_data);
	int dvc = 0;

	total_number_of_quads = 0;
	fs::u32 offset = 0;

	chunks.clear();
	auto& vertex_data = mapped_vertex_gpu_data[vbi];
#if LOOP_ALL
	for (auto&& [k,chunk] : world.loaded_chunks) {
#else
	auto add_chunk_quads = [&](LOD_Node* node, Chunk* chunk) -> void {
#endif
#if LOOP_ALL
		if (!chunk.active || chunk.place_holder) continue;
#else
		// we want to recurse up the tree to find something a chunk that is loaded.
		if (chunk->place_holder) {
			for(;;) {
				if (node->parent == (fs::u16)-1) return;
				node = world.node_allocator.base + node->parent;

				fs::v3s32 p = position_from_quad_tree(node);
				auto it = world.loaded_chunks.find(p);
				if (it != world.loaded_chunks.end() && !it->second.place_holder) {
					//__debugbreak();
					chunk = &it->second;
					break;
				}
			}
		}
#endif
		auto vertex_offset = offset;

		debug_mapped_vertex_data[dvc++] = { fs::v3f32(chunk->position.x,-1, chunk->position.z) };
		debug_mapped_vertex_data[dvc++] = { fs::v3f32(chunk->position.x, 1, chunk->position.z) };

		fs::u32 quad_count = 0;
		auto const scale = TEST?1:(1 << chunk->lod);
		auto const S = 1 << chunk->lod;
		for (auto const& q : chunk->quads) {
			int const x = int(q.x & 0x7F) * scale;
			int const y = int(q.y & 0x7F) * scale;
			int const z = int(q.z & 0x7F) * scale;

			int a = q.l0;
			int b = q.l1;

			auto const normal = normal_index(q);
			a *= scale;
			b *= scale;

			switch (normal) {
			default:
			break; case Normal::Pos_Y: {
#if CPU_SIDE_BACKFACE_CULLING
				if (world.center_position.y > float(y * S + chunk->position.y)) {
#endif
					quad_count++;
					vertex_data[offset++] = Vertex(x+a, y, z+b, q.palette);
					vertex_data[offset++] = Vertex(x+a, y, z,   q.palette);
					vertex_data[offset++] = Vertex(x,   y, z,   q.palette);
					vertex_data[offset++] = Vertex(x,   y, z+b, q.palette);
#if CPU_SIDE_BACKFACE_CULLING
				}
#endif
			}
			break; case Normal::Neg_X: {
#if CPU_SIDE_BACKFACE_CULLING
				if (world.center_position.x < float(x * S + chunk->position.x)) {
#endif
					quad_count++;
					vertex_data[offset++] = Vertex(x, y+a, z+b, q.palette);
					vertex_data[offset++] = Vertex(x, y+a, z,   q.palette);
					vertex_data[offset++] = Vertex(x, y,   z,   q.palette);
					vertex_data[offset++] = Vertex(x, y,   z+b, q.palette);
#if CPU_SIDE_BACKFACE_CULLING
				}
#endif
			}
			break; case Normal::Pos_X: {
#if CPU_SIDE_BACKFACE_CULLING
				if (world.center_position.x > float(x * S + chunk->position.x)) {
#endif
					quad_count++;
					vertex_data[offset++] = Vertex(x+scale, y,   z,   q.palette);
					vertex_data[offset++] = Vertex(x+scale, y+a, z,   q.palette);
					vertex_data[offset++] = Vertex(x+scale, y+a, z+b, q.palette);
					vertex_data[offset++] = Vertex(x+scale, y,   z+b, q.palette);
#if CPU_SIDE_BACKFACE_CULLING
				}
#endif
			}
			break; case Normal::Neg_Z: {
#if CPU_SIDE_BACKFACE_CULLING
				if (world.center_position.z < float(z * S + chunk->position.z)) {
#endif
					quad_count++;
					vertex_data[offset++] = Vertex(x+a, y+b, z, q.palette);
					vertex_data[offset++] = Vertex(x+a, y,   z, q.palette);
					vertex_data[offset++] = Vertex(x,   y,   z, q.palette);
					vertex_data[offset++] = Vertex(x,   y+b, z, q.palette);
#if CPU_SIDE_BACKFACE_CULLING
				}
#endif
			}
			break; case Normal::Pos_Z: {
#if CPU_SIDE_BACKFACE_CULLING
				if (world.center_position.z > float(z * S + chunk->position.z)) {
#endif
					quad_count++;
					vertex_data[offset++] = Vertex(x,   y,   z+scale, q.palette);
					vertex_data[offset++] = Vertex(x+a, y,   z+scale, q.palette);
					vertex_data[offset++] = Vertex(x+a, y+b, z+scale, q.palette);
					vertex_data[offset++] = Vertex(x,   y+b, z+scale, q.palette);
#if CPU_SIDE_BACKFACE_CULLING
				}
#endif
			}
			}
		}

		auto index_count = 6 * quad_count;
		total_number_of_quads += quad_count;

		// This bad.
		if (index_count > max_index_count_per_chunk) {
			index_count = max_index_count_per_chunk;
		}

		chunks.emplace_back(chunk->position, chunk->lod, vertex_offset, index_count);
	}
#if LOOP_ALL
#else
	;
	world.for_each_chunk(&world.root, add_chunk_quads);
#endif
	
	vmaUnmapMemory(engine.graphics.allocator, debug_vertex_allocation);
	vmaFlushAllocation(engine.graphics.allocator, debug_vertex_allocation, 0, dvc * sizeof(Debug_Vertex));

	auto const size = offset * sizeof(Vertex);
	used_vertex_gpu_memory = size;
//	memcpy(mapped_vertex_gpu_data, vertex_cpu_data, size);
	vmaFlushAllocation(engine.graphics.allocator, vertex_allocation[vbi], 0, size);
}

auto World_Renderer::draw(
	fs::Render_Context& ctx,
	Camera_Controller const& cc,
	World& world,
	float dt
) -> void {
	auto& gfx = engine.graphics;
	begin_render(ctx);
	t += dt;

	auto p = fs::v3s32::from(glm::ivec3(cc.position));

	{
		Transform_Data transform = {
			.projection = cc.get_transform(),
		//	.time       = t,
			.position_offset = glm::ivec4(glm::ivec3(cc.position),0)
		};
		Transform_Data* gpu_transform;
		vmaMapMemory(gfx.allocator, transform_allocation, (void**)&gpu_transform);
		memcpy(gpu_transform, &transform, sizeof(transform));
		vmaUnmapMemory(gfx.allocator, transform_allocation);
		vmaFlushAllocation(gfx.allocator, transform_allocation, 0, sizeof(Transform_Data));
	}
	
	FS_VK_BIND_DESCRIPTOR_SETS(ctx.command_buffer, pipeline_layout, 1, &transform_set);

	vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	auto view = cc.get_view_direction();

	Vertex_Push push;
	for (auto&& [position, lod, vertex_offset, index_count]: chunks) {
		push.position_offset = fs::v4f32(fs::v3f32(position-p), 0.0f);
		push.lod = lod;
		int cs = CHUNK_SIZE << lod - 1;//chunk size
		auto hh = glm::vec3(push.position_offset.x + cs, push.position_offset.y, push.position_offset.z + cs);
		if (glm::dot(hh, view) >= 0.0f) {
			vkCmdPushConstants(ctx.command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
			vkCmdDrawIndexed(ctx.command_buffer, index_count, 1, 0, vertex_offset, 0);
		}
	}

	if (debug_wireframe) {
		vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug_wireframe_pipeline);
		for (auto&& [position, lod, vertex_offset, index_count]: chunks) {
			push.position_offset = fs::v4f32(fs::v3f32(position-p), 0.0f);
			push.lod = lod;
			vkCmdPushConstants(ctx.command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
			vkCmdDrawIndexed(ctx.command_buffer, index_count, 1, 0, vertex_offset, 0);
		}
	}

	if (debug_show_chunk_bounds) {
		push.position_offset = fs::v4f32(fs::v3f32(cc.get_chunk_position()), 0.0f);
		push.position_offset.y = 0.0f;
		vkCmdPushConstants(ctx.command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
		vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug_pipeline);
		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(ctx.command_buffer, 0, 1, &debug_vertex_buffer, &offset);
		vkCmdDraw(ctx.command_buffer, 2 * (fs::u32)world.loaded_chunks.size(), 1, 0, 0);
	}

	end_render(ctx);
	
	engine.debug_layer.add("Loaded Chunks     = %i", world.loaded_chunks.size());
	engine.debug_layer.add("Chunks Drawn      = %i", chunks.size());
	engine.debug_layer.add("Number of Quads   = %i", total_number_of_quads);
	engine.debug_layer.add("Total Quad Memory = %.2f MiB", SIZE_MB(world.total_quad_memory));
}

auto World_Renderer::begin_render(fs::Render_Context& ctx) -> void {
	auto& gfx = engine.graphics;

	if (use_render_pass) {
		VkClearValue clear_values[3];
		clear_values[0].color = {};
		clear_values[1].depthStencil.depth = 1.0f;
		clear_values[2].color = {};
		VkRenderPassBeginInfo render_pass_begin_info{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		render_pass_begin_info.clearValueCount = (fs::u32)std::size(clear_values);
		render_pass_begin_info.pClearValues = clear_values;
		render_pass_begin_info.framebuffer = frame_buffers[ctx.image_index];
		render_pass_begin_info.renderArea = { .offset = {}, .extent = gfx.sc_extent };
		render_pass_begin_info.renderPass = render_pass;
		vkCmdBeginRenderPass(ctx.command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
	}

	vkCmdBindIndexBuffer(ctx.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(ctx.command_buffer, 0, 1, &vertex_buffer[vbi], &offset);

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(gfx.sc_extent.width);
	viewport.height = static_cast<float>(gfx.sc_extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(ctx.command_buffer, 0, 1, &viewport);
	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = gfx.sc_extent;
	vkCmdSetScissor(ctx.command_buffer, 0, 1, &scissor);
}

auto World_Renderer::end_render(fs::Render_Context& ctx) -> void {
	if (use_render_pass) {
		render_pass.end(&ctx);
	}
}