#pragma once
#include <Fission/Core/Engine.hh>
#include <Fission/Core/Console.hh>
#include <vector>

extern fs::Engine engine;

namespace vk {
#define VK_HANDLE_MEMBER_FUNCTIONS(NAME)      \
	Vk##NAME * operator&() { return &handle; } \
	operator Vk##NAME() { return handle; }      \

	struct Pipeline {
		VkPipeline handle;
		VK_HANDLE_MEMBER_FUNCTIONS(Pipeline)
		~Pipeline() { vkDestroyPipeline(engine.graphics.device, handle, nullptr); }
	};
}

struct Pipeline_Layout_Creator {
	std::vector<VkDescriptorSetLayout> layouts;
	std::vector<VkPushConstantRange> push_ranges;

	Pipeline_Layout_Creator& add_layout(VkDescriptorSetLayout layout) {
		layouts.emplace_back(layout);
		return *this;
	}
	Pipeline_Layout_Creator& add_push_range(VkShaderStageFlags stage, fs::u32 size, fs::u32 offset = 0u) {
		VkPushConstantRange range;
		range.offset = offset;
		range.size = size;
		range.stageFlags = stage;
		push_ranges.emplace_back(range);
		return *this;
	}
	VkResult create(VkPipelineLayout* pLayout) {
		VkPipelineLayoutCreateInfo ci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		ci.pSetLayouts = layouts.data();
		ci.setLayoutCount = (fs::u32)layouts.size();
		ci.pPushConstantRanges = push_ranges.data();
		ci.pushConstantRangeCount = (fs::u32)push_ranges.size();
		return vkCreatePipelineLayout(engine.graphics.device, &ci, nullptr, pLayout);
	}
};

struct Render_Pass_Creator {
	std::vector<VkAttachmentDescription> attachments;
	std::vector<VkAttachmentReference> attachment_references;
	std::vector<VkSubpassDescription> subpasses;
	std::vector<VkSubpassDependency> subpass_dependencies;

	Render_Pass_Creator(int attachment_reference_count) {
		attachment_references.reserve(attachment_reference_count);
	}

	Render_Pass_Creator& add_external_subpass_dependency(uint32_t subpass) {
		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = subpass;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		subpass_dependencies.emplace_back(dependency);
		return *this;
	}

	VkPipelineStageFlags pick_stage_mask_from_access_mask(VkAccessFlags access) {
		switch (access)
		{
		case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:         return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT: return VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		case VK_ACCESS_SHADER_READ_BIT:                    return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		default:                                           return 0;
		}
	}

	Render_Pass_Creator& add_dependency(uint32_t src_subpass, uint32_t dst_subpass, VkAccessFlags src_access, VkAccessFlags dst_access) {
		VkSubpassDependency dependency{};
		dependency.srcSubpass = src_subpass;
		dependency.dstSubpass = dst_subpass;
		dependency.srcStageMask = pick_stage_mask_from_access_mask(src_access);
		dependency.srcAccessMask = src_access;
		dependency.dstStageMask = pick_stage_mask_from_access_mask(dst_access);
		dependency.dstAccessMask = dst_access;
		subpass_dependencies.emplace_back(dependency);
		return *this;
	}

	Render_Pass_Creator& add_subpass(std::initializer_list<VkAttachmentReference> const& refs) {
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = (fs::u32)refs.size();
		subpass.pColorAttachments = attachment_references.data() + attachment_references.size();
		for (auto&& ref : refs) attachment_references.emplace_back(ref);
		subpasses.emplace_back(subpass);
		return *this;
	}
	Render_Pass_Creator& add_subpass_input(std::initializer_list<VkAttachmentReference> const& refs, std::initializer_list<VkAttachmentReference> const& input_refs) {
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = (fs::u32)refs.size();
		subpass.inputAttachmentCount = (fs::u32)input_refs.size();
		subpass.pColorAttachments = attachment_references.data() + attachment_references.size();
		for (auto&& ref : refs) attachment_references.emplace_back(ref);
		subpass.pInputAttachments = attachment_references.data() + attachment_references.size();
		for (auto&& ref : input_refs) attachment_references.emplace_back(ref);
		subpasses.emplace_back(subpass);
		return *this;
	}

	Render_Pass_Creator& add_subpass(std::initializer_list<VkAttachmentReference> const& refs, VkAttachmentReference depth_ref) {
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = (fs::u32)refs.size();
		subpass.pColorAttachments = attachment_references.data() + attachment_references.size();
		//	subpass.pResolveAttachments = &colorAttachmentResolveRef; // MSAA
		for (auto&& ref : refs) attachment_references.emplace_back(ref);
		subpass.pDepthStencilAttachment = attachment_references.data() + attachment_references.size();
		attachment_references.emplace_back(depth_ref);
		subpasses.emplace_back(subpass);
		return *this;
	}
	Render_Pass_Creator& add_subpass(std::initializer_list<VkAttachmentReference> const& refs, VkAttachmentReference depth_ref, VkAttachmentReference resolve_ref) {
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = (fs::u32)refs.size();
		subpass.pColorAttachments = attachment_references.data() + attachment_references.size();
		for (auto&& ref : refs) attachment_references.emplace_back(ref);
		subpass.pDepthStencilAttachment = attachment_references.data() + attachment_references.size();
		attachment_references.emplace_back(depth_ref);
		subpass.pResolveAttachments = attachment_references.data() + attachment_references.size(); // MSAA
		attachment_references.emplace_back(resolve_ref);
		subpasses.emplace_back(subpass);
		return *this;
	}

	Render_Pass_Creator& add_subpass_with_input_attachment(std::initializer_list<VkAttachmentReference> const& refs, VkAttachmentReference input_ref) {
		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = (fs::u32)refs.size();
		subpass.pColorAttachments = attachment_references.data() + attachment_references.size();
		//	subpass.pResolveAttachments = &colorAttachmentResolveRef; // MSAA
		for (auto&& ref : refs) attachment_references.emplace_back(ref);
		subpass.pInputAttachments = attachment_references.data() + attachment_references.size();
		subpass.inputAttachmentCount = 1;
		attachment_references.emplace_back(input_ref);
		subpasses.emplace_back(subpass);
		return *this;
	}

	VkImageLayout pick_final_image_layout_for_format(VkFormat format) {
		switch (format)
		{
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_X8_D24_UNORM_PACK32:
		case VK_FORMAT_D32_SFLOAT:
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		default:                             return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
	}

	enum Attachment_Preset {
		// Use this for Depth and Color images that we want to write to
		Attachment_New_Image,        // load = CLEAR, store = STORE, stencil = DONT CARE, inital = UNDEFINED
		Attachment_Cumulative_Image, // load = LOAD , store = STORE, stencil = DONT CARE, inital = UNDEFINED
	};

	Render_Pass_Creator& add_attachment(VkFormat format, VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT) {
		VkAttachmentDescription attachment{};
		attachment.format = format;
		attachment.samples = sample_count;
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment.finalLayout = pick_final_image_layout_for_format(format);
		attachments.emplace_back(attachment);
		return *this;
	}
	Render_Pass_Creator& add_attachment(VkFormat format, VkAttachmentLoadOp loadOp, VkImageLayout layout, VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT) {
		VkAttachmentDescription attachment{};
		attachment.format = format;
		attachment.samples = sample_count;
		attachment.loadOp = loadOp;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = layout;
		attachment.finalLayout = layout;
		attachments.emplace_back(attachment);
		return *this;
	}
	Render_Pass_Creator& add_attachment(VkFormat format, VkImageLayout initial_layout, VkImageLayout final_layout, VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT) {
		VkAttachmentDescription attachment{};
		attachment.format = format;
		attachment.samples = sample_count;
		attachment.loadOp = (initial_layout == VK_IMAGE_LAYOUT_UNDEFINED)? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = initial_layout;
		attachment.finalLayout = final_layout;
		attachments.emplace_back(attachment);
		return *this;
	}

	VkResult create(VkRenderPass* pRenderPass) {
		VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
		ci.attachmentCount = (fs::u32)attachments.size();
		ci.pAttachments = attachments.data();
		ci.subpassCount = (fs::u32)subpasses.size();
		ci.pSubpasses = subpasses.data();
		ci.dependencyCount = (fs::u32)subpass_dependencies.size();
		ci.pDependencies = subpass_dependencies.data();
		return vkCreateRenderPass(engine.graphics.device, &ci, nullptr, pRenderPass);
	}
};

namespace gfx {
	struct Image {
		VkImage image;
		VmaAllocation allocation;

		~Image() {
			vmaDestroyImage(engine.graphics.allocator, image, allocation);
		}
	};
}

struct Image_Creator {
	VkImageCreateInfo image_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
	};
	VmaAllocationCreateInfo allocation_info{ .usage = VMA_MEMORY_USAGE_AUTO };

	Image_Creator() = default;
	Image_Creator(VkFormat format, VkImageUsageFlags usage, VkExtent2D extent) : Image_Creator() {
		image_info.format = format;
		image_info.usage = usage;
		image_info.extent = { .width = extent.width, .height = extent.height, .depth = 1 };
	}

	VkResult create(gfx::Image& image) {
		return vmaCreateImage(engine.graphics.allocator, &image_info, &allocation_info, &image.image, &image.allocation, nullptr);
	}
};

struct Pipeline_Creator {
	std::vector<VkDynamicState>                  dynamic_states;
	std::vector<VkPipelineShaderStageCreateInfo> shaders;
	VkPipelineColorBlendAttachmentState          blend_attachment = {
		.colorWriteMask = 0b1111,
	};
	VkPipelineVertexInputStateCreateInfo const* vertex_input_state;
	VkPipelineInputAssemblyStateCreateInfo      input_assembly_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};
	VkPipelineViewportStateCreateInfo           viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};
	VkPipelineRasterizationStateCreateInfo      rasterization_state{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0f,
	};
	VkPipelineMultisampleStateCreateInfo        multisample_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
	};
	VkPipelineDepthStencilStateCreateInfo       depth_stencil_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
		.minDepthBounds = 0.0f,
		.maxDepthBounds = 1.0f,
	};
	VkPipelineColorBlendStateCreateInfo         color_blend_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = 1,
		.pAttachments = nullptr,
	};
	VkPipelineDynamicStateCreateInfo            dynamic_state{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	VkPipelineLayout                            layout;
	VkRenderPass                                render_pass;
	uint32_t                                    subpass;

	Pipeline_Creator(VkRenderPass render_pass, VkPipelineLayout layout, uint32_t subpass = 0u) {
		this->layout = layout;
		this->render_pass = render_pass;
		this->subpass = subpass;
	}

	Pipeline_Creator& vertex_input(VkPipelineVertexInputStateCreateInfo const* vi) {
		vertex_input_state = vi;
		return *this;
	}

	Pipeline_Creator& add_dynamic_state(VkDynamicState state) {
		dynamic_states.emplace_back(state);
		return *this;
	}

	Pipeline_Creator& add_shader(VkShaderStageFlagBits stage, VkShaderModule shader) {
		VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
		info.module = shader;
		info.stage = stage;
		info.pName = "main";
		shaders.emplace_back(info);
		return *this;
	}

	VkResult create_and_destroy_shaders(VkPipeline* pipeline) {
		auto result = create(pipeline);
		for (auto&& [sType, pNext, flags, stage, module, pName, pSpecializationInfo] : shaders)
			vkDestroyShaderModule(engine.graphics.device, module, nullptr);
		return result;
	}
	VkResult create(VkPipeline* pipeline) {
		dynamic_state.dynamicStateCount = (fs::u32)dynamic_states.size();
		dynamic_state.pDynamicStates = dynamic_states.data();
		color_blend_state.pAttachments = &blend_attachment;

		VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.stageCount = (fs::u32)shaders.size();
		pipelineInfo.pStages = shaders.data();
		pipelineInfo.pVertexInputState = vertex_input_state;
		pipelineInfo.pInputAssemblyState = &input_assembly_state;
		pipelineInfo.pViewportState = &viewport_state;
		pipelineInfo.pRasterizationState = &rasterization_state;
		pipelineInfo.pMultisampleState = &multisample_state;
		pipelineInfo.pDepthStencilState = &depth_stencil_state;
		pipelineInfo.pColorBlendState = &color_blend_state;
		pipelineInfo.pDynamicState = &dynamic_state;
		pipelineInfo.layout = layout;
		pipelineInfo.renderPass = render_pass;
		pipelineInfo.subpass = subpass;
		return vkCreateGraphicsPipelines(engine.graphics.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, pipeline);
	}
	VkResult create_no_fragment(VkPipeline* pipeline) {
		dynamic_state.dynamicStateCount = (fs::u32)dynamic_states.size();
		dynamic_state.pDynamicStates = dynamic_states.data();
		color_blend_state.pAttachments = &blend_attachment;

		VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.stageCount = 1;
		pipelineInfo.pStages = shaders.data();
		pipelineInfo.pVertexInputState = vertex_input_state;
		pipelineInfo.pInputAssemblyState = &input_assembly_state;
		pipelineInfo.pViewportState = &viewport_state;
		pipelineInfo.pRasterizationState = &rasterization_state;
		pipelineInfo.pMultisampleState = &multisample_state;
		pipelineInfo.pDepthStencilState = &depth_stencil_state;
		pipelineInfo.pColorBlendState = &color_blend_state;
		pipelineInfo.pDynamicState = &dynamic_state;
		pipelineInfo.layout = layout;
		pipelineInfo.renderPass = render_pass;
		pipelineInfo.subpass = subpass;
		return vkCreateGraphicsPipelines(engine.graphics.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, pipeline);
	}
};
