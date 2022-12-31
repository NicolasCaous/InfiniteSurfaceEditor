#include "VulkanRendererUgly.h"

#include <iostream>
#include <format>
#include <fstream>

#include "VkBootstrap.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_Vulkan.h"

#include "../util/FileReader.h"

void ise::rendering::vulkan_device_initialization(VulkanRendererData& renderer)
{
	vulkan_handle_sdl_int(SDL_Init(SDL_INIT_EVERYTHING));
	vulkan_handle_sdl_int(SDL_Vulkan_LoadLibrary(nullptr));
	renderer.window = SDL_CreateWindow("Example Vulkan Application", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (renderer.window == NULL)
	{
		throw new std::runtime_error(std::format("SDL window creation failed with message {0}", SDL_GetError()));
	}


	unsigned int extensionCount = 0;
	vulkan_handle_sdl_bool(SDL_Vulkan_GetInstanceExtensions(renderer.window, &extensionCount, nullptr));
	std::vector<const char*> extensionNames(extensionCount);
	vulkan_handle_sdl_bool(SDL_Vulkan_GetInstanceExtensions(renderer.window, &extensionCount, extensionNames.data()));

	vkb::InstanceBuilder builder;
	auto inst_builder = builder.set_app_name("Example Vulkan Application")
		//.request_validation_layers()
		.use_default_debug_messenger();

	for (auto extensionName : extensionNames)
	{
		inst_builder.enable_extension(extensionName);
	}

	auto inst_ret = inst_builder.build();
	if (!inst_ret)
	{
		throw new std::runtime_error(std::format("Vulkan instance creation failed with error: VK_RESULT? {1} MESSAGE {0}", inst_ret.error().message(), (int)inst_ret.vk_result()));
	}
	renderer.instance = inst_ret.value();

	vulkan_handle_sdl_bool(SDL_Vulkan_CreateSurface(renderer.window, renderer.instance, &renderer.surface));

	vkb::PhysicalDeviceSelector phys_device_selector(renderer.instance);
	auto physical_device_ret = phys_device_selector.set_surface(renderer.surface).select();
	if (!physical_device_ret)
	{
		throw new std::runtime_error(std::format("Vulkan physical device creation failed with error: VK_RESULT? {1} MESSAGE {0}", physical_device_ret.error().message(), (int)physical_device_ret.vk_result()));
	}
	vkb::PhysicalDevice physical_device = physical_device_ret.value();

	vkb::DeviceBuilder device_builder{ physical_device };
	auto device_ret = device_builder.build();
	if (!device_ret)
	{
		throw new std::runtime_error(std::format("Vulkan device creation failed with error: VK_RESULT? {1} MESSAGE {0}", device_ret.error().message(), (int)device_ret.vk_result()));
	}
	renderer.device = device_ret.value();
}

void ise::rendering::vulkan_create_swapchain(VulkanRendererData& renderer)
{
	vkb::destroy_swapchain(renderer.swapchain);

	vkb::SwapchainBuilder swapchain_builder{ renderer.device };
	auto swapchain_ret = swapchain_builder
		.set_old_swapchain(renderer.swapchain)
		.set_desired_present_mode(renderer.v_sync ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR)
		.build();
	if (!swapchain_ret)
	{
		throw new std::runtime_error(std::format("Vulkan device creation failed with error: VK_RESULT? {1} MESSAGE {0}", swapchain_ret.error().message(), (int)swapchain_ret.vk_result()));
	}
	renderer.swapchain = swapchain_ret.value();
}

void ise::rendering::vulkan_recreate_swapchain(VulkanRendererData& renderer)
{
	vulkan_handle_vk_result(vkDeviceWaitIdle(renderer.device));

	vkDestroyCommandPool(renderer.device, renderer.command_pool, nullptr);

	for (auto framebuffer : renderer.framebuffers)
	{
		vkDestroyFramebuffer(renderer.device, framebuffer, nullptr);
	}

	renderer.swapchain.destroy_image_views(renderer.swapchain_image_views);

	vulkan_create_swapchain(renderer);
	vulkan_create_framebuffers(renderer);
	vulkan_create_command_pool(renderer);
	vulkan_create_command_buffers(renderer);
}

void ise::rendering::vulkan_get_queues(VulkanRendererData& renderer)
{
	auto graphics_queue_ret = renderer.device.get_queue(vkb::QueueType::graphics);
	if (!graphics_queue_ret)
	{
		throw new std::runtime_error(std::format("Vulkan get_queue(vkb::QueueType::graphics) call failed with error: VK_RESULT? {1} MESSAGE {0}", graphics_queue_ret.error().message(), (int)graphics_queue_ret.vk_result()));
	}
	renderer.graphics_queue = graphics_queue_ret.value();
	auto present_queue_ret = renderer.device.get_queue(vkb::QueueType::present);
	if (!present_queue_ret)
	{
		throw new std::runtime_error(std::format("Vulkan get_queue(vkb::QueueType::present) call failed with error: VK_RESULT? {1} MESSAGE {0}", present_queue_ret.error().message(), (int)present_queue_ret.vk_result()));
	}
	renderer.present_queue = present_queue_ret.value();
}

void ise::rendering::vulkan_create_render_pass(VulkanRendererData& renderer)
{
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = renderer.swapchain.image_format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 1;
	render_pass_info.pDependencies = &dependency;

	vulkan_handle_vk_result(vkCreateRenderPass(renderer.device, &render_pass_info, nullptr, &renderer.render_pass));
}

VkShaderModule ise::rendering::vulkan_create_shader_module(VulkanRendererData& renderer, const std::vector<char>& code)
{
	VkShaderModuleCreateInfo create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.codeSize = code.size();
	create_info.pCode = reinterpret_cast<const uint32_t*> (code.data());

	VkShaderModule shaderModule;
	vulkan_handle_vk_result(vkCreateShaderModule(renderer.device, &create_info, nullptr, &shaderModule));

	return shaderModule;
}

void ise::rendering::vulkan_create_graphics_pipeline(VulkanRendererData& renderer)
{
	auto vert_code = ise::util::readFile(std::string(STRINGIFY(VULKAN_SHADER_DIR)) + "/vert.spv");
	auto frag_code = ise::util::readFile(std::string(STRINGIFY(VULKAN_SHADER_DIR)) + "/frag.spv");

	VkShaderModule vert_module = vulkan_create_shader_module(renderer, vert_code);
	VkShaderModule frag_module = vulkan_create_shader_module(renderer, frag_code);

	VkPipelineShaderStageCreateInfo vert_stage_info = {};
	vert_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vert_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vert_stage_info.module = vert_module;
	vert_stage_info.pName = "main";

	VkPipelineShaderStageCreateInfo frag_stage_info = {};
	frag_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	frag_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	frag_stage_info.module = frag_module;
	frag_stage_info.pName = "main";

	VkPipelineShaderStageCreateInfo shader_stages[] = { vert_stage_info, frag_stage_info };

	VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
	vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_info.vertexBindingDescriptionCount = 0;
	vertex_input_info.vertexAttributeDescriptionCount = 0;

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
	input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	input_assembly.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)renderer.swapchain.extent.width;
	viewport.height = (float)renderer.swapchain.extent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = renderer.swapchain.extent;

	VkPipelineViewportStateCreateInfo viewport_state = {};
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo color_blending = {};
	color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blending.logicOpEnable = VK_FALSE;
	color_blending.logicOp = VK_LOGIC_OP_COPY;
	color_blending.attachmentCount = 1;
	color_blending.pAttachments = &colorBlendAttachment;
	color_blending.blendConstants[0] = 0.0f;
	color_blending.blendConstants[1] = 0.0f;
	color_blending.blendConstants[2] = 0.0f;
	color_blending.blendConstants[3] = 0.0f;

	VkPipelineLayoutCreateInfo pipeline_layout_info = {};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 0;
	pipeline_layout_info.pushConstantRangeCount = 0;

	vulkan_handle_vk_result(vkCreatePipelineLayout(
		renderer.device, &pipeline_layout_info, nullptr, &renderer.pipeline_layout));

	std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamic_info = {};
	dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_info.dynamicStateCount = static_cast<uint32_t> (dynamic_states.size());
	dynamic_info.pDynamicStates = dynamic_states.data();

	VkGraphicsPipelineCreateInfo pipeline_info = {};
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = shader_stages;
	pipeline_info.pVertexInputState = &vertex_input_info;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterizer;
	pipeline_info.pMultisampleState = &multisampling;
	pipeline_info.pColorBlendState = &color_blending;
	pipeline_info.pDynamicState = &dynamic_info;
	pipeline_info.layout = renderer.pipeline_layout;
	pipeline_info.renderPass = renderer.render_pass;
	pipeline_info.subpass = 0;
	pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

	vulkan_handle_vk_result(vkCreateGraphicsPipelines(
		renderer.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &renderer.graphics_pipeline));

	vkDestroyShaderModule(renderer.device, frag_module, nullptr);
	vkDestroyShaderModule(renderer.device, vert_module, nullptr);
}

void ise::rendering::vulkan_create_framebuffers(VulkanRendererData& renderer)
{
	renderer.swapchain_images = renderer.swapchain.get_images().value();
	renderer.swapchain_image_views = renderer.swapchain.get_image_views().value();

	renderer.framebuffers.resize(renderer.swapchain_image_views.size());

	for (size_t i = 0; i < renderer.swapchain_image_views.size(); i++)
	{
		VkImageView attachments[] = { renderer.swapchain_image_views[i] };

		VkFramebufferCreateInfo framebuffer_info = {};
		framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_info.renderPass = renderer.render_pass;
		framebuffer_info.attachmentCount = 1;
		framebuffer_info.pAttachments = attachments;
		framebuffer_info.width = renderer.swapchain.extent.width;
		framebuffer_info.height = renderer.swapchain.extent.height;
		framebuffer_info.layers = 1;

		vulkan_handle_vk_result(vkCreateFramebuffer(renderer.device, &framebuffer_info, nullptr, &renderer.framebuffers[i]));
	}
}

void ise::rendering::vulkan_create_command_pool(VulkanRendererData& renderer)
{
	VkCommandPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.queueFamilyIndex = renderer.device.get_queue_index(vkb::QueueType::graphics).value();

	vulkan_handle_vk_result(vkCreateCommandPool(renderer.device, &pool_info, nullptr, &renderer.command_pool));
}

void ise::rendering::vulkan_create_command_buffers(VulkanRendererData& renderer)
{
	renderer.command_buffers.resize(renderer.framebuffers.size());

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = renderer.command_pool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = (uint32_t)renderer.command_buffers.size();

	vulkan_handle_vk_result(vkAllocateCommandBuffers(renderer.device, &allocInfo, renderer.command_buffers.data()));

	for (size_t i = 0; i < renderer.command_buffers.size(); i++)
	{
		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		vulkan_handle_vk_result(vkBeginCommandBuffer(renderer.command_buffers[i], &begin_info));

		VkRenderPassBeginInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_info.renderPass = renderer.render_pass;
		render_pass_info.framebuffer = renderer.framebuffers[i];
		render_pass_info.renderArea.offset = { 0, 0 };
		render_pass_info.renderArea.extent = renderer.swapchain.extent;
		VkClearValue clearColor{ { { 0.0f, 0.0f, 0.0f, 1.0f } } };
		render_pass_info.clearValueCount = 1;
		render_pass_info.pClearValues = &clearColor;

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)renderer.swapchain.extent.width;
		viewport.height = (float)renderer.swapchain.extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = renderer.swapchain.extent;

		vkCmdSetViewport(renderer.command_buffers[i], 0, 1, &viewport);
		vkCmdSetScissor(renderer.command_buffers[i], 0, 1, &scissor);

		vkCmdBeginRenderPass(renderer.command_buffers[i], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(renderer.command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.graphics_pipeline);

		vkCmdDraw(renderer.command_buffers[i], 3, 1, 0, 0);

		vkCmdEndRenderPass(renderer.command_buffers[i]);

		vulkan_handle_vk_result(vkEndCommandBuffer(renderer.command_buffers[i]));
	}
}

void ise::rendering::vulkan_create_sync_objects(VulkanRendererData& renderer)
{
	renderer.available_semaphores.resize(renderer.max_frames_in_flight);
	renderer.finished_semaphore.resize(renderer.max_frames_in_flight);
	renderer.in_flight_fences.resize(renderer.max_frames_in_flight);
	renderer.image_in_flight.resize(renderer.swapchain.image_count, VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphore_info = {};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fence_info = {};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < renderer.max_frames_in_flight; i++)
	{
		vulkan_handle_vk_result(vkCreateSemaphore(renderer.device, &semaphore_info, nullptr, &renderer.available_semaphores[i]));
		vulkan_handle_vk_result(vkCreateSemaphore(renderer.device, &semaphore_info, nullptr, &renderer.finished_semaphore[i]));
		vulkan_handle_vk_result(vkCreateFence(renderer.device, &fence_info, nullptr, &renderer.in_flight_fences[i]));
	}
}

void ise::rendering::vulkan_draw_frame(VulkanRendererData& renderer)
{
	vulkan_handle_vk_result(vkWaitForFences(renderer.device, 1, &renderer.in_flight_fences[renderer.current_frame], VK_TRUE, UINT64_MAX));

	uint32_t image_index = 0;
	VkResult result = vkAcquireNextImageKHR(renderer.device,
		renderer.swapchain,
		UINT64_MAX,
		renderer.available_semaphores[renderer.current_frame],
		VK_NULL_HANDLE,
		&image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || renderer.force_refresh)
	{
		renderer.force_refresh = false;
		return vulkan_recreate_swapchain(renderer);
	}
	else
	{
		vulkan_handle_vk_result(result);
	}

	if (renderer.image_in_flight[image_index] != VK_NULL_HANDLE)
	{
		vulkan_handle_vk_result(vkWaitForFences(renderer.device, 1, &renderer.image_in_flight[image_index], VK_TRUE, UINT64_MAX));
	}
	renderer.image_in_flight[image_index] = renderer.in_flight_fences[renderer.current_frame];

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore wait_semaphores[] = { renderer.available_semaphores[renderer.current_frame] };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = wait_semaphores;
	submitInfo.pWaitDstStageMask = wait_stages;

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &renderer.command_buffers[image_index];

	VkSemaphore signal_semaphores[] = { renderer.finished_semaphore[renderer.current_frame] };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signal_semaphores;

	vulkan_handle_vk_result(vkResetFences(renderer.device, 1, &renderer.in_flight_fences[renderer.current_frame]));

	vulkan_handle_vk_result(vkQueueSubmit(renderer.graphics_queue, 1, &submitInfo, renderer.in_flight_fences[renderer.current_frame]));

	VkPresentInfoKHR present_info = {};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = signal_semaphores;

	VkSwapchainKHR swapChains[] = { renderer.swapchain };
	present_info.swapchainCount = 1;
	present_info.pSwapchains = swapChains;

	present_info.pImageIndices = &image_index;

	result = vkQueuePresentKHR(renderer.present_queue, &present_info);

	renderer.current_frame = (renderer.current_frame + 1) % renderer.max_frames_in_flight;
}

void ise::rendering::vulkan_cleanup(VulkanRendererData& renderer)
{
	vulkan_handle_vk_result(vkDeviceWaitIdle(renderer.device));

	for (size_t i = 0; i < renderer.max_frames_in_flight; i++)
	{
		vkDestroySemaphore(renderer.device, renderer.finished_semaphore[i], nullptr);
		vkDestroySemaphore(renderer.device, renderer.available_semaphores[i], nullptr);
		vkDestroyFence(renderer.device, renderer.in_flight_fences[i], nullptr);
	}

	vkDestroyCommandPool(renderer.device, renderer.command_pool, nullptr);

	for (auto framebuffer : renderer.framebuffers)
	{
		vkDestroyFramebuffer(renderer.device, framebuffer, nullptr);
	}

	vkDestroyPipeline(renderer.device, renderer.graphics_pipeline, nullptr);
	vkDestroyPipelineLayout(renderer.device, renderer.pipeline_layout, nullptr);
	vkDestroyRenderPass(renderer.device, renderer.render_pass, nullptr);

	renderer.swapchain.destroy_image_views(renderer.swapchain_image_views);

	vkb::destroy_swapchain(renderer.swapchain);
	vkb::destroy_device(renderer.device);
	vkb::destroy_surface(renderer.instance, renderer.surface);
	vkb::destroy_instance(renderer.instance);
}

void ise::rendering::vulkan_handle_vk_result(VkResult result)
{
	if (result != VK_SUCCESS)
	{
		throw new std::runtime_error(std::format("Vulkan failed with code: {0}", (int)result));
	}
}

void ise::rendering::vulkan_handle_sdl_bool(SDL_bool sdl_bool)
{
	if (sdl_bool == SDL_FALSE)
	{
		throw new std::runtime_error(std::string("Vulkan SDL(bool) failed with error: ") + SDL_GetError());
	}
}

void ise::rendering::vulkan_handle_sdl_int(int result)
{
	if (result != 0)
	{
		throw new std::runtime_error(std::format("Vulkan SDL(int) failed with error: {0} {1}", result, SDL_GetError()));
	}
}