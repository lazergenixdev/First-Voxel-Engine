#include "Skybox.h"
#include <Fission/Core/Engine.hh>
#include "stb_image.h"
#include "config.hpp"
extern fs::Engine engine;

//#define BLUE_SKY
//#define RED_SKY
#define CLOUD_SKY
//#define URBAN_SKY
#define SKYBOX_FOLDER "Backyard"
//#define SKYBOX_FOLDER "Roundabout"

struct vert {
#include "../shaders/skybox.vert.inl"
};
struct frag {
#include "../shaders/skybox.frag.inl"
};


auto Skybox::create(fs::Graphics& gfx, VkRenderPass in_render_pass) -> void {
	Pipeline_Layout_Creator{}
		.add_push_range(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4))
		.add_layout(engine.texture_layout)
		.create(&pipeline_layout);

	auto vertex_input = VkPipelineVertexInputStateCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	Pipeline_Creator{ in_render_pass, pipeline_layout }
		.add_shader(VK_SHADER_STAGE_VERTEX_BIT, fs::create_shader(gfx.device, vert::size, vert::data))
		.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, fs::create_shader(gfx.device, frag::size, frag::data))
		.vertex_input(&vertex_input)
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
		.create_and_destroy_shaders(&pipeline);
	//	pc.blend_attachment.blendEnable = VK_FALSE;

	auto format = VK_FORMAT_R8G8B8A8_UNORM;
	{
		int w, h, comp;
#if RAIN
		w = 1, h = 1;
#elif defined(RED_SKY)
		stbi_info("../assets/red/bkg2_right1.png", &w, &h, &comp);
#elif defined(BLUE_SKY)
		stbi_info("../assets/blue/bkg1_back.png", &w, &h, &comp);
#elif defined(CLOUD_SKY)
		stbi_info("../assets/clouds1/east.bmp", &w, &h, &comp);
#elif defined(URBAN_SKY)
		stbi_info("../assets/Roundabout/posx.jpg", &w, &h, &comp);
#endif

		auto ic = Image_Creator{ format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, {unsigned(w), unsigned(h)} };
		ic.image_info.arrayLayers = 6;
		ic.image_info.imageType = VK_IMAGE_TYPE_2D;
		ic.image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		ic.create(image);

		auto do_thing = [&](int i, char const* filename) {
			int w, h, comp;
			auto data = stbi_load(filename, &w, &h, &comp, 4);
			gfx.upload_image(image.image, data, { unsigned(w), unsigned(h), 1 }, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, i);
		};

		// This is an aweful, truely terrible way to upload image data to the GPU.
		// There is no need to be waiting for each operation to complete before starting the next!
		
#if RAIN
		fs::rgba8 color = (fs::rgba8)fs::colors::gray(0.1f);
		fs::rgba8* data = &color;
		gfx.upload_image(image.image, data, { unsigned(w), unsigned(h), 1 }, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0);
		gfx.upload_image(image.image, data, { unsigned(w), unsigned(h), 1 }, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
		gfx.upload_image(image.image, data, { unsigned(w), unsigned(h), 1 }, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);
		gfx.upload_image(image.image, data, { unsigned(w), unsigned(h), 1 }, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 3);
		gfx.upload_image(image.image, data, { unsigned(w), unsigned(h), 1 }, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 4);
		gfx.upload_image(image.image, data, { unsigned(w), unsigned(h), 1 }, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 5);
#elif defined(RED_SKY)
		do_thing(0, "../assets/red/bkg2_right1.png");
		do_thing(1, "../assets/red/bkg2_left2.png");
		do_thing(2, "../assets/red/bkg2_top3.png");
		do_thing(3, "../assets/red/bkg2_bottom4.png");
		do_thing(4, "../assets/red/bkg2_front5.png");
		do_thing(5, "../assets/red/bkg2_back6.png");
#elif defined(BLUE_SKY)
		do_thing(0, "../assets/blue/bkg1_right.png");
		do_thing(1, "../assets/blue/bkg1_left.png");
		do_thing(2, "../assets/blue/bkg1_top.png");
		do_thing(3, "../assets/blue/bkg1_bot.png");
		do_thing(4, "../assets/blue/bkg1_front.png");
		do_thing(5, "../assets/blue/bkg1_back.png");
#elif defined(CLOUD_SKY)
		do_thing(0, "../assets/clouds1/east.bmp");
		do_thing(1, "../assets/clouds1/west.bmp");
		do_thing(2, "../assets/clouds1/up.bmp");
		do_thing(3, "../assets/clouds1/down.bmp");
		do_thing(4, "../assets/clouds1/north.bmp");
		do_thing(5, "../assets/clouds1/south.bmp");
#elif defined(URBAN_SKY)
		do_thing(0, "../assets/Backyard/posx.jpg");
		do_thing(1, "../assets/Backyard/negx.jpg");
		do_thing(2, "../assets/Backyard/posy.jpg");
		do_thing(3, "../assets/Backyard/negy.jpg");
		do_thing(4, "../assets/Backyard/posz.jpg");
		do_thing(5, "../assets/Backyard/negz.jpg");
#endif
	}
	{
		auto view_info = vk::image_view_2d(image.image, format);
		view_info.subresourceRange.layerCount = 6;
		view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		vkCreateImageView(gfx.device, &view_info, nullptr, &view);

		VkDescriptorSetAllocateInfo descSetAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		descSetAllocInfo.descriptorPool = engine.descriptor_pool;
		descSetAllocInfo.pSetLayouts = &engine.texture_layout;
		descSetAllocInfo.descriptorSetCount = 1;
		vkAllocateDescriptorSets(gfx.device, &descSetAllocInfo, &set);

		auto sampler_info = vk::sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
		vkCreateSampler(gfx.device, &sampler_info, nullptr, &sampler);

		VkDescriptorImageInfo imageInfo;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = view;
		imageInfo.sampler = sampler;
		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.dstBinding = 0;
		write.dstSet = set;
		write.pImageInfo = &imageInfo;
		vkUpdateDescriptorSets(gfx.device, 1, &write, 0, nullptr);
	}
}

auto Skybox::destroy(fs::Graphics& gfx) -> void {
	vkDestroySampler(gfx.device, sampler, nullptr);
	vkDestroyImageView(gfx.device, view, nullptr);
}

auto Skybox::draw(fs::Render_Context* ctx, glm::mat4 const* transform) -> void {
	vkCmdBindPipeline(ctx->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdPushConstants(ctx->command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), transform);
	FS_VK_BIND_DESCRIPTOR_SETS(ctx->command_buffer, pipeline_layout, 1, &set);
	vkCmdDraw(ctx->command_buffer, 36, 1, 0, 0);
}
