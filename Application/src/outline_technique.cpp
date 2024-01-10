#include "outline_technique.h"

void Outline_Render_Pass::begin(fs::Render_Context* ctx, VkFramebuffer frame_buffer, fs::color clear) {
	VkClearValue clear_values[2];
	clear_values[0].color = { clear.r, clear.g, clear.b, clear.a };
	clear_values[1].depthStencil = { 1.0f, 0 };
	VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	beginInfo.framebuffer = frame_buffer;
	beginInfo.renderPass = handle;
	beginInfo.clearValueCount = 2;
	beginInfo.pClearValues = clear_values;
	beginInfo.renderArea.extent = ctx->gfx->sc_extent;
	beginInfo.renderArea.offset = { 0, 0 };
	vkCmdBeginRenderPass(ctx->command_buffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void Outline_Render_Pass::end(fs::Render_Context* ctx) {
	vkCmdEndRenderPass(ctx->command_buffer);
}

void Outline_Render_Pass::destroy(fs::Graphics& gfx) {
	vkDestroyRenderPass(gfx.device, handle, nullptr);
}

void Outline_Render_Pass::create(fs::Graphics& gfx) {
	Render_Pass_Creator{4}
		.add_attachment(gfx.sc_format)
		.add_attachment(VK_FORMAT_D32_SFLOAT)
		.add_subpass({{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}}, {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL})
		.add_subpass_with_input_attachment({{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}}, {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL})
		.add_external_subpass_dependency(0)
		.add_dependency(0, 1, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
		.create(&handle);
}

// ********************************************************************************************

struct post_fx_vertex_shader {
#include "../shaders/fullscreen.vert.inl"
};
struct post_fx_fragment_shader {
#include "../shaders/outline.frag.inl"
};

void Outline_Technique::create(fs::Graphics& gfx)
{
	render_pass.create(gfx);

	Pipeline_Layout_Creator{}
		.add_layout(engine.texture_layout.handle)
		.create(&post_pipeline_layout);

	auto vs = fs::create_shader(gfx.device, post_fx_vertex_shader::size, post_fx_vertex_shader::data);
	auto fs = fs::create_shader(gfx.device, post_fx_fragment_shader::size, post_fx_fragment_shader::data);

	auto vertex_input = VkPipelineVertexInputStateCreateInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

	auto pc = Pipeline_Creator{render_pass, post_pipeline_layout, 1}
		.add_shader(VK_SHADER_STAGE_VERTEX_BIT, vs)
		.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, fs)
		.vertex_input(&vertex_input)
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);
	
	pc.blend_attachment.blendEnable         = VK_TRUE;
	pc.blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
	pc.blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
	pc.blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	pc.blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	pc.blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	pc.blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	
	pc.create_and_destroy_shaders(&post_pipeline);

	auto sampler_info = vk::sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	vkCreateSampler(gfx.device, &sampler_info, nullptr, &sampler);
	
	VkImageUsageFlags depth_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	Image_Creator{VK_FORMAT_D32_SFLOAT, depth_usage, gfx.sc_extent}
		.create(depth_image);

	auto view_info = vk::image_view_2d(depth_image.image, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
	vkCreateImageView(gfx.device, &view_info, nullptr, &depth_image_view);

	{
		VkDescriptorSetAllocateInfo set_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		set_info.descriptorPool = engine.descriptor_pool;
		set_info.pSetLayouts = &engine.texture_layout;
		set_info.descriptorSetCount = 1;
		vkAllocateDescriptorSets(gfx.device, &set_info, &depth_set);

		VkDescriptorImageInfo image_info;
		image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
		image_info.imageView = depth_image_view;
		image_info.sampler = sampler;
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.dstBinding = 0;
		write.dstSet = depth_set;
		write.pImageInfo = &image_info;
		vkUpdateDescriptorSets(gfx.device, 1, &write, 0, nullptr);
	}
	
	VkImageView attachments[2] = { nullptr, depth_image_view };
	FS_FOR(gfx.sc_image_count) {
		attachments[0] = gfx.sc_image_views[i];
		VkFramebufferCreateInfo frame_buffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
		frame_buffer_info.renderPass = render_pass;
		frame_buffer_info.attachmentCount = (uint32_t)std::size(attachments);
		frame_buffer_info.pAttachments = attachments;
		frame_buffer_info.width  = gfx.sc_extent.width;
		frame_buffer_info.height = gfx.sc_extent.height;
		frame_buffer_info.layers = 1;
		vkCreateFramebuffer(gfx.device, &frame_buffer_info, nullptr, frame_buffers + i);
	}
}

void Outline_Technique::destroy(fs::Graphics& gfx)
{
	vkDestroySampler(gfx.device, sampler, nullptr);
	vkDestroyImageView(gfx.device, depth_image_view, nullptr);
	vkDestroyPipeline(gfx.device, post_pipeline, nullptr);
	vkDestroyPipelineLayout(gfx.device, post_pipeline_layout, nullptr);
	FS_FOR(gfx.sc_image_count)
	vkDestroyFramebuffer(gfx.device, frame_buffers[i], nullptr);
	render_pass.destroy(gfx);
}

void Outline_Technique::resize(fs::Graphics& gfx)
{
	depth_image.~Image();
	vkDestroyImageView(gfx.device, depth_image_view, nullptr);
	FS_FOR(gfx.sc_image_count)
	vkDestroyFramebuffer(gfx.device, frame_buffers[i], nullptr);

	////////////////////////////////////////////////////////////////
	
	VkImageUsageFlags depth_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	Image_Creator{VK_FORMAT_D32_SFLOAT, depth_usage, gfx.sc_extent}
		.create(depth_image);
		
	auto view_info = vk::image_view_2d(depth_image.image, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
	vkCreateImageView(gfx.device, &view_info, nullptr, &depth_image_view);

	{
		VkDescriptorImageInfo image_info;
		image_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
		image_info.imageView = depth_image_view;
		image_info.sampler = sampler;
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.dstBinding = 0;
		write.dstSet = depth_set;
		write.pImageInfo = &image_info;
		vkUpdateDescriptorSets(gfx.device, 1, &write, 0, nullptr);
	}
	
	VkImageView attachments[2] = { nullptr, depth_image_view };
	FS_FOR(gfx.sc_image_count) {
		attachments[0] = gfx.sc_image_views[i];
		VkFramebufferCreateInfo frame_buffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
		frame_buffer_info.renderPass = render_pass;
		frame_buffer_info.attachmentCount = (uint32_t)std::size(attachments);
		frame_buffer_info.pAttachments = attachments;
		frame_buffer_info.width  = gfx.sc_extent.width;
		frame_buffer_info.height = gfx.sc_extent.height;
		frame_buffer_info.layers = 1;
		vkCreateFramebuffer(gfx.device, &frame_buffer_info, nullptr, frame_buffers + i);
	}
}

void Outline_Technique::begin(fs::Render_Context* ctx)
{
	render_pass.begin(ctx, frame_buffers[ctx->image_index], fs::colors::Black);
}

void Outline_Technique::end(fs::Render_Context* ctx)
{
	vkCmdNextSubpass(ctx->command_buffer, VK_SUBPASS_CONTENTS_INLINE);

	if (post_fx_enable) {
		vkCmdBindPipeline(ctx->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, post_pipeline);

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

		FS_VK_BIND_DESCRIPTOR_SETS(ctx->command_buffer, post_pipeline_layout, 1, &depth_set);

		vkCmdDraw(ctx->command_buffer, 3, 1, 0, 0);
	}

	render_pass.end(ctx);
}
