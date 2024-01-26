#pragma once
#include "gfx.h"
#include <glm/mat4x4.hpp> // glm::mat4

struct Skybox {
	vk::Pipeline       pipeline;
	vk::PipelineLayout pipeline_layout;
	gfx::Image         image;
	VkImageView        view;
	VkDescriptorSet    set;
	VkSampler          sampler;

	auto create(fs::Graphics& gfx, VkRenderPass in_render_pass) -> void;
	auto destroy(fs::Graphics& gfx) -> void;
	auto draw(fs::Render_Context* ctx, glm::mat4 const* transform) -> void;
};