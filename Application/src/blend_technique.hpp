#include <Fission/Core/Engine.hh>

extern fs::Engine engine;


// TODO: is it faster to TRANSFER or to COPY the final image? (maybe depends on hardware)
struct Blend_Technique {
	static constexpr auto     image_format = VK_FORMAT_R32G32B32A32_SFLOAT;
	static constexpr auto src_image_format = VK_FORMAT_R32G32B32A32_SFLOAT;

	fs::Render_Pass render_pass;
	VkPipeline blend_pipeline;
	VkPipeline copy_pipeline;

	VkImage       src_image;
	VkImage       dst_image;
	VmaAllocation src_allocation;
	VmaAllocation dst_allocation;
	VkImageView   src_view;
	VkImageView   dst_view;

	VkFramebuffer frame_buffers[fs::Graphics::max_sc_images];

	VkDescriptorSetLayout blend_descriptor_layout;
	VkPipelineLayout      blend_pipeline_layout;
	VkDescriptorSet       blend_descriptor_set;
	VkDescriptorSet       copy_descriptor_set;

	struct blend_vs {
#include "shaders/blend.vert.inl"
	};
	struct blend_fs {
#include "shaders/blend.frag.inl"
	};

	void create() {
		auto& gfx = engine.graphics;

		create_render_pass();
		create_images_and_frame_buffers(gfx);

		{
			VkDescriptorSetLayoutBinding binding{};
			binding.binding = 0;
			binding.descriptorCount = 1;
			binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
			binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
			VkDescriptorSetLayoutCreateInfo descriptorInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			descriptorInfo.bindingCount = 1;
			descriptorInfo.pBindings = &binding;
			vkCreateDescriptorSetLayout(gfx.device, &descriptorInfo, nullptr, &blend_descriptor_layout);

			VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			pipelineLayoutInfo.pSetLayouts = &blend_descriptor_layout;
			pipelineLayoutInfo.setLayoutCount = 1;
			vkCreatePipelineLayout(gfx.device, &pipelineLayoutInfo, nullptr, &blend_pipeline_layout);

			VkDescriptorSetAllocateInfo descSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			descSetAllocInfo.descriptorPool = engine.descriptor_pool;
			descSetAllocInfo.pSetLayouts = &blend_descriptor_layout;
			descSetAllocInfo.descriptorSetCount = 1;
			vkAllocateDescriptorSets(gfx.device, &descSetAllocInfo, &blend_descriptor_set);
			vkAllocateDescriptorSets(gfx.device, &descSetAllocInfo, &copy_descriptor_set);

			VkDescriptorImageInfo imageInfo;
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = src_view;
			imageInfo.sampler = nullptr;
			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
			write.descriptorCount = 1;
			write.dstBinding = 0;
			write.dstSet = blend_descriptor_set;
			write.pImageInfo = &imageInfo;
			vkUpdateDescriptorSets(gfx.device, 1, &write, 0, nullptr);
			imageInfo.imageView = dst_view;
			write.dstSet = copy_descriptor_set;
			vkUpdateDescriptorSets(gfx.device, 1, &write, 0, nullptr);
		}

		create_blend_pipeline(gfx.device);
	}

	void create_render_pass() {
		Render_Pass_Creator{ 16 }
			.add_attachment(src_image_format)
			.add_attachment(image_format, VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.add_attachment(engine.graphics.sc_format)
			.add_subpass({{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}})
			.add_subpass_input({{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}}, {{0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}})
			.add_subpass_input({{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}}, {{1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}})
			.add_external_subpass_dependency(0)
			.add_dependency(0, 1, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
			.add_dependency(1, 2, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
			.create(&render_pass.handle);
	}

	void create_blend_pipeline(VkDevice device) {
		auto vs = fs::create_shader(device, blend_vs::size, blend_vs::data);
		auto fs = fs::create_shader(device, blend_fs::size, blend_fs::data);

		VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		Pipeline_Creator pc{render_pass, blend_pipeline_layout, 1};
		pc	.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
			.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
			.add_dynamic_state(VK_DYNAMIC_STATE_BLEND_CONSTANTS)
			.add_shader(VK_SHADER_STAGE_VERTEX_BIT, vs)
			.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, fs)
			.vertex_input(&vertex_input);
		pc.blend_attachment = {};
		pc.blend_attachment.colorWriteMask = 0b1111;
		pc.blend_attachment.blendEnable = VK_TRUE;
		pc.blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
		pc.blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
		pc.blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
		pc.create(&blend_pipeline);

		pc.subpass = 2;
		pc.blend_attachment.blendEnable = VK_FALSE;
		pc.dynamic_states.resize(2);
		pc.create_and_destroy_shaders(&copy_pipeline);
	}

	void create_images_and_frame_buffers(fs::Graphics& gfx) {
		{
			auto format = image_format;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			imageInfo.arrayLayers = 1;
			imageInfo.extent = vk::extent3d(gfx.sc_extent);
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.mipLevels = 1;

			imageInfo.format = src_image_format;
			imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
			vmaCreateImage(gfx.allocator, &imageInfo, &allocInfo, &src_image, &src_allocation, nullptr);

			imageInfo.format = format;
			imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
			vmaCreateImage(gfx.allocator, &imageInfo, &allocInfo, &dst_image, &dst_allocation, nullptr);

			auto viewInfo = vk::image_view_2d(src_image, src_image_format);
			vkCreateImageView(gfx.device, &viewInfo, nullptr, &src_view);

			viewInfo.format = format;
			viewInfo.image = dst_image;
			vkCreateImageView(gfx.device, &viewInfo, nullptr, &dst_view);

			auto const stride = vk::size_of(format);
			auto data = _aligned_malloc(imageInfo.extent.width * imageInfo.extent.height * stride, 1024);
			memset(data, 0, imageInfo.extent.width * imageInfo.extent.height * stride);
			gfx.upload_image(dst_image, data, imageInfo.extent, format);
			_aligned_free(data);
		}
		{
			VkImageView attachments[] = { src_view, dst_view, VK_NULL_HANDLE };
			VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			framebufferInfo.renderPass = render_pass;
			framebufferInfo.attachmentCount = (fs::u32)std::size(attachments);
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = gfx.sc_extent.width;
			framebufferInfo.height = gfx.sc_extent.height;
			framebufferInfo.layers = 1;
			FS_FOR(gfx.sc_image_count) {
				attachments[2] = gfx.sc_image_views[i];
				vkCreateFramebuffer(gfx.device, &framebufferInfo, nullptr, frame_buffers + i);
			}
		}
	}

	void destroy() {
		auto& gfx = engine.graphics;
		vkDestroyPipeline(gfx.device, blend_pipeline, nullptr);
		vkDestroyPipeline(gfx.device, copy_pipeline, nullptr);

		vmaDestroyImage(gfx.allocator, src_image, src_allocation);
		vmaDestroyImage(gfx.allocator, dst_image, dst_allocation);
		vkDestroyImageView(gfx.device, src_view, nullptr);
		vkDestroyImageView(gfx.device, dst_view, nullptr);

		FS_FOR(gfx.sc_image_count) vkDestroyFramebuffer(gfx.device, frame_buffers[i], nullptr);

		vkDestroyDescriptorSetLayout(gfx.device, blend_descriptor_layout, nullptr);
		vkDestroyPipelineLayout(gfx.device, blend_pipeline_layout, nullptr);

		vkDestroyRenderPass(gfx.device, render_pass, nullptr);
	}

	void resize() {
		auto& gfx = engine.graphics;
		vmaDestroyImage(gfx.allocator, src_image, src_allocation);
		vmaDestroyImage(gfx.allocator, dst_image, dst_allocation);
		vkDestroyImageView(gfx.device, src_view, nullptr);
		vkDestroyImageView(gfx.device, dst_view, nullptr);
		FS_FOR(gfx.sc_image_count) vkDestroyFramebuffer(gfx.device, frame_buffers[i], nullptr);

		create_images_and_frame_buffers(gfx);

		VkDescriptorImageInfo imageInfo;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = src_view;
		imageInfo.sampler = nullptr;
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		write.descriptorCount = 1;
		write.dstBinding = 0;
		write.dstSet = blend_descriptor_set;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(gfx.device, 1, &write, 0, nullptr);
		imageInfo.imageView = dst_view;
		write.dstSet = copy_descriptor_set;
		vkUpdateDescriptorSets(gfx.device, 1, &write, 0, nullptr);
	}

	void begin(fs::Render_Context* ctx) {
		using namespace fs;
		auto& cmd = ctx->command_buffer;

		VkClearValue clearColor[3];
		memset(clearColor, 0, sizeof(clearColor));
		clearColor[0].color.float32[0] = 1.0f;
		clearColor[0].color.float32[1] = 1.0f;
		clearColor[0].color.float32[2] = 1.0f;
		clearColor[0].color.float32[3] = 1.0f;
		VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
		beginInfo.clearValueCount = 3;
		beginInfo.pClearValues = clearColor;
		beginInfo.framebuffer = frame_buffers[ctx->image_index];
		beginInfo.renderPass = render_pass;
		beginInfo.renderArea.extent = engine.graphics.sc_extent;
		vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	}
	void end(float dt, fs::Render_Context* ctx, float b = 0.98f) {
		using namespace fs;
		auto& cmd = ctx->command_buffer;

		vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);

		// Blend src Image with dst Image
		FS_VK_BIND_DESCRIPTOR_SETS(cmd, blend_pipeline_layout, 1, &blend_descriptor_set);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blend_pipeline);
		float blend = std::powf(b, dt * 120.0f);
		engine.debug_layer.add("blend = %.2f", blend);
		float blend_constants[4] = { 0.0f, 0.0f, 0.0f, blend };
		vkCmdSetBlendConstants(cmd, blend_constants);
		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);

		FS_VK_BIND_DESCRIPTOR_SETS(cmd, blend_pipeline_layout, 1, &copy_descriptor_set);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, copy_pipeline);
		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRenderPass(cmd);
	}
};