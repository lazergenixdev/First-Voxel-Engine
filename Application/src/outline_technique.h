#pragma once
#include "gfx.h"

struct Outline_Render_Pass {
	VkRenderPass handle;

	inline constexpr operator VkRenderPass() const { return handle; }

	void begin(fs::Render_Context* ctx, VkFramebuffer frame_buffer, fs::color clear);
	void end(fs::Render_Context* ctx);

	void create(fs::Graphics& gfx);
	void destroy(fs::Graphics& gfx);
};

struct Outline_Technique {
	void create(fs::Graphics& gfx);
	void destroy(fs::Graphics& gfx);
	void resize(fs::Graphics& gfx);
	
	void begin(fs::Render_Context* ctx);
	void end(fs::Render_Context* ctx);

	bool post_fx_enable = true;

	Outline_Render_Pass render_pass;

	VkPipelineLayout  post_pipeline_layout;
	VkPipeline        post_pipeline;

	VkSampler         sampler;
	gfx::Image        depth_image;
	VkImageView       depth_image_view;
	VkDescriptorSet   depth_set;

	VkFramebuffer     frame_buffers[fs::Graphics::max_sc_images];

	float outline_color[4] = {1.0f, 1.0f, 1.0f, 0.0f};
};