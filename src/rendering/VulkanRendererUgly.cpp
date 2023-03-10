#include "VulkanRendererUgly.h"

#include <iostream>
#include <format>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <array>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#endif

#ifndef TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#endif

#include <vulkan/vulkan.h>
#define VKRH vulkan_handle_vk_result;

#include "../util/FileReader.h"

namespace std {
    template<> struct hash<ise::rendering::Vertex> {
        size_t operator()(ise::rendering::Vertex const& vertex) const {
            return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.tex_coord) << 1);
        }
    };
}

void ise::rendering::vulkan_create_instance(VulkanRendererData& renderer)
{
    if (renderer.custom_config.enable_validation_layers && !vulkan_check_validation_layer_support(renderer))
    {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Hello Triangle";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    if (renderer.custom_config.enable_validation_layers)
    {
        renderer.instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    create_info.enabledExtensionCount = static_cast<uint32_t>(renderer.instance_extensions.size());
    create_info.ppEnabledExtensionNames = renderer.instance_extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
    if (renderer.custom_config.enable_validation_layers)
    {
        create_info.enabledLayerCount = static_cast<uint32_t>(renderer.validation_layers.size());
        create_info.ppEnabledLayerNames = renderer.validation_layers.data();

        debug_create_info = {};
        debug_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_create_info.pfnUserCallback = vulkan_debug_callback;

        create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debug_create_info;
    }
    else
    {
        create_info.enabledLayerCount = 0;
        create_info.pNext = nullptr;
    }

    VKRH(vkCreateInstance(&create_info, nullptr, &renderer.instance));
}

void ise::rendering::vulkan_setup_debug_messenger(VulkanRendererData& renderer)
{
    if (!renderer.custom_config.enable_validation_layers)
    {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT create_info;
    create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = vulkan_debug_callback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(renderer.instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(renderer.instance, &create_info, nullptr, &renderer.debug_messenger);
    }
    else
    {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

void ise::rendering::vulkan_pick_physical_device(VulkanRendererData& renderer)
{
    uint32_t device_count = 0;
    VKRH(vkEnumeratePhysicalDevices(renderer.instance, &device_count, nullptr));

    if (device_count == 0)
    {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    VKRH(vkEnumeratePhysicalDevices(renderer.instance, &device_count, devices.data()));

    for (const auto& device : devices)
    {
        if (vulkan_is_device_suitable(device, renderer))
        {
            renderer.physical_device = device;
            renderer.msaa_samples = vulkan_get_max_usable_sample_count(renderer);
            break;
        }
    }

    if (renderer.physical_device == VK_NULL_HANDLE)
    {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

void ise::rendering::vulkan_create_logical_device(VulkanRendererData& renderer)
{
    QueueFamilyIndices indices = vulkan_find_queue_families(renderer.physical_device, renderer);

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    std::set<uint32_t> unique_queue_families = { indices.graphics_family.value(), indices.present_family.value() };

    float queue_priority = 1.0f;
    for (uint32_t queue_family : unique_queue_families)
    {
        VkDeviceQueueCreateInfo queue_create_info{};
        queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex = queue_family;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;
        queue_create_infos.push_back(queue_create_info);
    }

    VkPhysicalDeviceFeatures device_features{};

    if (renderer.custom_config.max_anisotropy < 1.0f)
    {
        device_features.samplerAnisotropy = VK_FALSE;
    }
    else
    {
        device_features.samplerAnisotropy = VK_TRUE;
    }

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
    create_info.pQueueCreateInfos = queue_create_infos.data();

    create_info.pEnabledFeatures = &device_features;

    create_info.enabledExtensionCount = static_cast<uint32_t>(renderer.device_extensions.size());
    create_info.ppEnabledExtensionNames = renderer.device_extensions.data();

    if (renderer.custom_config.enable_validation_layers)
    {
        create_info.enabledLayerCount = static_cast<uint32_t>(renderer.validation_layers.size());
        create_info.ppEnabledLayerNames = renderer.validation_layers.data();
    }
    else
    {
        create_info.enabledLayerCount = 0;
    }

    if (vkCreateDevice(renderer.physical_device, &create_info, nullptr, &renderer.device) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(renderer.device, indices.graphics_family.value(), 0, &renderer.graphics_queue);
    vkGetDeviceQueue(renderer.device, indices.present_family.value(), 0, &renderer.present_queue);
}

void ise::rendering::vulkan_create_swap_chain(VulkanRendererData& renderer)
{
    SwapChainSupportDetails swap_chain_support = vulkan_query_swap_chain_support(renderer.physical_device, renderer);

    VkSurfaceFormatKHR surface_format = vulkan_choose_swap_surface_format(swap_chain_support.formats);
    VkPresentModeKHR present_mode = vulkan_choose_swap_present_mode(swap_chain_support.present_modes, renderer);
    VkExtent2D extent = swap_chain_support.capabilities.currentExtent;

    uint32_t image_count = swap_chain_support.capabilities.minImageCount + 1;
    if (swap_chain_support.capabilities.maxImageCount > 0 && image_count > swap_chain_support.capabilities.maxImageCount)
    {
        image_count = swap_chain_support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = renderer.surface;

    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = vulkan_find_queue_families(renderer.physical_device, renderer);
    uint32_t queue_family_indices[] = { indices.graphics_family.value(), indices.present_family.value() };

    if (indices.graphics_family != indices.present_family)
    {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    }
    else
    {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    create_info.preTransform = swap_chain_support.capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(renderer.device, &create_info, nullptr, &renderer.swap_chain) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create swap chain!");
    }

    VKRH(vkGetSwapchainImagesKHR(renderer.device, renderer.swap_chain, &image_count, nullptr));
    renderer.swap_chain_images.resize(image_count);
    VKRH(vkGetSwapchainImagesKHR(renderer.device, renderer.swap_chain, &image_count, renderer.swap_chain_images.data()));

    renderer.swap_chain_image_format = surface_format.format;
    renderer.swap_chain_extent = extent;
}

void ise::rendering::vulkan_create_image_views(VulkanRendererData& renderer)
{
    renderer.swap_chain_image_views.resize(renderer.swap_chain_images.size());

    for (uint32_t i = 0; i < renderer.swap_chain_images.size(); i++)
    {
        renderer.swap_chain_image_views[i] = vulkan_create_image_view(renderer, renderer.swap_chain_images[i], renderer.swap_chain_image_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
}

void ise::rendering::vulkan_create_render_pass(VulkanRendererData& renderer)
{
    VkAttachmentDescription color_attachment{};
    color_attachment.format = renderer.swap_chain_image_format;
    color_attachment.samples = renderer.msaa_samples;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (renderer.msaa_samples & VK_SAMPLE_COUNT_1_BIT)
    {
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    else
    {
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkAttachmentDescription depth_attachment{};
    depth_attachment.format = vulkan_find_supported_format(
        renderer,
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
    depth_attachment.samples = renderer.msaa_samples;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription color_attachment_resolve{};
    color_attachment_resolve.format = renderer.swap_chain_image_format;
    color_attachment_resolve.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment_resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment_resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment_resolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment_resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment_resolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment_resolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref{};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_attachment_resolve_ref{};
    color_attachment_resolve_ref.attachment = 2;
    color_attachment_resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;
    if (!(renderer.msaa_samples & VK_SAMPLE_COUNT_1_BIT))
    {
        subpass.pResolveAttachments = &color_attachment_resolve_ref;
    }

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments;
    attachments.push_back(color_attachment);
    attachments.push_back(depth_attachment);
    if (!(renderer.msaa_samples & VK_SAMPLE_COUNT_1_BIT))
    {
        attachments.push_back(color_attachment_resolve);
    }

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    if (vkCreateRenderPass(renderer.device, &render_pass_info, nullptr, &renderer.render_pass) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create render pass!");
    }
}

void ise::rendering::vulkan_create_descriptor_set_layout(VulkanRendererData& renderer)
{
    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(renderer.physical_device, &physical_device_properties);

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    VkDescriptorSetLayoutBinding ubo_layout_binding{};
    ubo_layout_binding.binding = 0;
    ubo_layout_binding.descriptorCount = 1;
    ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_layout_binding.pImmutableSamplers = nullptr;
    ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings.push_back(ubo_layout_binding);

    VkDescriptorSetLayoutCreateInfo layout_info_uniform_buffers{};
    layout_info_uniform_buffers.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info_uniform_buffers.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info_uniform_buffers.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(renderer.device, &layout_info_uniform_buffers, nullptr, &renderer.descriptor_set_layout_uniform_buffers) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create descriptor set layout for uniform buffers!");
    }

    bindings.clear();
    VkDescriptorSetLayoutBinding sampler_layout_binding{};
    sampler_layout_binding.binding = 0;
    sampler_layout_binding.descriptorCount = 1;
    sampler_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_layout_binding.pImmutableSamplers = nullptr;
    sampler_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(sampler_layout_binding);

    VkDescriptorSetLayoutCreateInfo layout_info_textures{};
    layout_info_textures.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info_textures.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info_textures.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(renderer.device, &layout_info_textures, nullptr, &renderer.descriptor_set_layout_textures) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create descriptor set layout for uniform buffers!");
    }
}

void ise::rendering::vulkan_create_graphics_pipeline(VulkanRendererData& renderer)
{
    auto vert_code = ise::util::readFile(std::string(STRINGIFY(VULKAN_SHADER_DIR)) + "/vert.spv");
    auto frag_code = ise::util::readFile(std::string(STRINGIFY(VULKAN_SHADER_DIR)) + "/frag.spv");

    VkShaderModule vert_shader_module = vulkan_create_shader_module(renderer, vert_code);
    VkShaderModule frag_shader_module = vulkan_create_shader_module(renderer, frag_code);

    VkPipelineShaderStageCreateInfo vert_shader_stage_info{};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_shader_module;
    vert_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_shader_stage_info{};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_shader_module;
    frag_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = { vert_shader_stage_info, frag_shader_stage_info };

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto binding_description = Vertex::get_binding_description();
    auto attribute_descriptions = Vertex::get_attribute_descriptions();

    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
    vertex_input_info.pVertexBindingDescriptions = &binding_description;
    vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = renderer.msaa_samples;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;

    std::vector<VkDynamicState> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
    descriptor_set_layouts.push_back(renderer.descriptor_set_layout_uniform_buffers);
    descriptor_set_layouts.push_back(renderer.descriptor_set_layout_textures);

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = descriptor_set_layouts.size();
    pipeline_layout_info.pSetLayouts = descriptor_set_layouts.data();

    if (vkCreatePipelineLayout(renderer.device, &pipeline_layout_info, nullptr, &renderer.pipeline_layout) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = renderer.pipeline_layout;
    pipeline_info.renderPass = renderer.render_pass;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(renderer.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &renderer.graphics_pipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(renderer.device, frag_shader_module, nullptr);
    vkDestroyShaderModule(renderer.device, vert_shader_module, nullptr);
}

void ise::rendering::vulkan_create_command_pool(VulkanRendererData& renderer)
{
    QueueFamilyIndices queue_family_indices = vulkan_find_queue_families(renderer.physical_device, renderer);

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_indices.graphics_family.value();

    if (vkCreateCommandPool(renderer.device, &pool_info, nullptr, &renderer.command_pool) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create graphics command pool!");
    }
}

void ise::rendering::vulkan_create_color_resources(VulkanRendererData& renderer)
{
    if (renderer.msaa_samples & VK_SAMPLE_COUNT_1_BIT)
    {
        return;
    }

    VkFormat color_format = renderer.swap_chain_image_format;

    vulkan_create_image(
        renderer,
        renderer.swap_chain_extent.width,
        renderer.swap_chain_extent.height,
        1,
        renderer.msaa_samples,
        color_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        renderer.color_image,
        renderer.color_image_memory);

    renderer.color_image_view = vulkan_create_image_view(
        renderer,
        renderer.color_image,
        color_format,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1);
}

void ise::rendering::vulkan_create_depth_resources(VulkanRendererData& renderer)
{
    VkFormat depth_format = vulkan_find_supported_format(
        renderer,
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );

    vulkan_create_image(
        renderer,
        renderer.swap_chain_extent.width,
        renderer.swap_chain_extent.height,
        1,
        renderer.msaa_samples,
        depth_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        renderer.depth_image,
        renderer.depth_image_memory);

    renderer.depth_image_view = vulkan_create_image_view(
        renderer,
        renderer.depth_image,
        depth_format,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        1);
}

void ise::rendering::vulkan_create_framebuffers(VulkanRendererData& renderer)
{
    renderer.swap_chain_framebuffers.resize(renderer.swap_chain_image_views.size());

    for (size_t i = 0; i < renderer.swap_chain_image_views.size(); i++)
    {
        std::vector<VkImageView> attachments;

        if (renderer.msaa_samples & VK_SAMPLE_COUNT_1_BIT)
        {
            attachments.push_back(renderer.swap_chain_image_views[i]);
            attachments.push_back(renderer.depth_image_view);
        }
        else
        {
            attachments.push_back(renderer.color_image_view);
            attachments.push_back(renderer.depth_image_view);
            attachments.push_back(renderer.swap_chain_image_views[i]);
        }
        

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = renderer.render_pass;
        framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebuffer_info.pAttachments = attachments.data();
        framebuffer_info.width = renderer.swap_chain_extent.width;
        framebuffer_info.height = renderer.swap_chain_extent.height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(renderer.device, &framebuffer_info, nullptr, &renderer.swap_chain_framebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

void ise::rendering::vulkan_create_uniform_buffers(VulkanRendererData& renderer)
{
    VkDeviceSize buffer_size = sizeof(UniformBufferObject);

    renderer.uniform_buffers.resize(renderer.custom_config.max_frames_in_flight);
    renderer.uniform_buffers_memory.resize(renderer.custom_config.max_frames_in_flight);
    renderer.uniform_buffers_mapped.resize(renderer.custom_config.max_frames_in_flight);

    for (size_t i = 0; i < renderer.custom_config.max_frames_in_flight; i++)
    {
        vulkan_create_buffer(renderer, buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, renderer.uniform_buffers[i], renderer.uniform_buffers_memory[i]);

        VKRH(vkMapMemory(renderer.device, renderer.uniform_buffers_memory[i], 0, buffer_size, 0, &renderer.uniform_buffers_mapped[i]));
    }
}

void ise::rendering::vulkan_create_descriptor_pool(VulkanRendererData& renderer)
{
    std::array<VkDescriptorPoolSize, 2> pool_sizes{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = static_cast<uint32_t>(renderer.custom_config.max_frames_in_flight);
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = 65536;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = 65536;

    if (vkCreateDescriptorPool(renderer.device, &pool_info, nullptr, &renderer.descriptor_pool) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void ise::rendering::vulkan_create_uniform_buffers_descriptor_sets(VulkanRendererData& renderer)
{
    std::vector<VkDescriptorSetLayout> layouts(renderer.custom_config.max_frames_in_flight, renderer.descriptor_set_layout_uniform_buffers);
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = renderer.descriptor_pool;
    alloc_info.descriptorSetCount = static_cast<uint32_t>(renderer.custom_config.max_frames_in_flight);
    alloc_info.pSetLayouts = layouts.data();

    renderer.uniform_buffers_descriptor_sets.resize(renderer.custom_config.max_frames_in_flight);
    if (vkAllocateDescriptorSets(renderer.device, &alloc_info, renderer.uniform_buffers_descriptor_sets.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < renderer.custom_config.max_frames_in_flight; i++)
    {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = renderer.uniform_buffers[i];
        buffer_info.offset = 0;
        buffer_info.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptor_write{};

        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = renderer.uniform_buffers_descriptor_sets[i];
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pBufferInfo = &buffer_info;

        vkUpdateDescriptorSets(renderer.device, 1, &descriptor_write, 0, nullptr);
    }
}

void ise::rendering::vulkan_create_command_buffers(VulkanRendererData& renderer)
{
    renderer.command_buffers.resize(renderer.custom_config.max_frames_in_flight);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = renderer.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = (uint32_t)renderer.command_buffers.size();

    if (vkAllocateCommandBuffers(renderer.device, &alloc_info, renderer.command_buffers.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

ise::rendering::RenderTexture* ise::rendering::vulkan_create_render_texture(std::string key, VulkanRendererData& renderer)
{
    std::lock_guard<std::mutex> lock(renderer.mutex);

    vulkan_destroy_render_texture_unsafe(key, renderer);
    renderer.render_textures[key] = new RenderTexture;
    return renderer.render_textures[key];
}

bool ise::rendering::vulkan_destroy_render_texture(std::string key, VulkanRendererData& renderer)
{
    std::lock_guard<std::mutex> lock(renderer.mutex);
    return vulkan_destroy_render_texture_unsafe(key, renderer);
}

bool ise::rendering::vulkan_destroy_render_texture_unsafe(std::string key, VulkanRendererData& renderer)
{
    if (renderer.render_textures.count(key) == 0)
    {
        return false;
    }

    RenderTexture* render_texture = renderer.render_textures[key];
    renderer.render_textures.erase(key);

    vkDestroySampler(renderer.device, render_texture->sampler, nullptr);
    vkDestroyImageView(renderer.device, render_texture->image_view, nullptr);

    vkDestroyImage(renderer.device, render_texture->image, nullptr);
    vkFreeMemory(renderer.device, render_texture->image_memory, nullptr);

    delete render_texture;

    return true;
}

ise::rendering::RenderObject* ise::rendering::vulkan_create_render_object(VulkanRendererData& renderer)
{
    std::lock_guard<std::mutex> lock(renderer.mutex);

    RenderObject* render_object = new RenderObject;
    renderer.render_objects.push_back(render_object);
    return render_object;
}

void ise::rendering::vulkan_create_texture_image(VulkanRendererData& renderer, RenderTexture& render_texture)
{
    std::lock_guard<std::mutex> lock(renderer.mutex);

    VkDeviceSize image_size = render_texture.raw_texture.width * render_texture.raw_texture.height * 4;
    render_texture.mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(render_texture.raw_texture.width, render_texture.raw_texture.height)))) + 1;

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    vulkan_create_buffer(renderer, image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

    void* data;
    VKRH(vkMapMemory(renderer.device, staging_buffer_memory, 0, image_size, 0, &data));
    memcpy(data, render_texture.raw_texture.pixels, static_cast<size_t>(image_size));
    vkUnmapMemory(renderer.device, staging_buffer_memory);

    vulkan_create_image(renderer, render_texture.raw_texture.width, render_texture.raw_texture.height, render_texture.mip_levels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, render_texture.image, render_texture.image_memory);

    vulkan_transition_image_layout(renderer, render_texture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, render_texture.mip_levels);
    vulkan_copy_buffer_to_image(renderer, staging_buffer, render_texture.image, static_cast<uint32_t>(render_texture.raw_texture.width), static_cast<uint32_t>(render_texture.raw_texture.height));
    //transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL while generating mipmaps

    vkDestroyBuffer(renderer.device, staging_buffer, nullptr);
    vkFreeMemory(renderer.device, staging_buffer_memory, nullptr);

    vulkan_generate_mipmaps(renderer, render_texture.image, VK_FORMAT_R8G8B8A8_SRGB, render_texture.raw_texture.width, render_texture.raw_texture.height, render_texture.mip_levels);

    render_texture.image_view = vulkan_create_image_view(renderer, render_texture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, render_texture.mip_levels);
}

void ise::rendering::vulkan_create_texture_sampler(VulkanRendererData& renderer, RenderTexture& render_texture)
{
    std::lock_guard<std::mutex> lock(renderer.mutex);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(renderer.physical_device, &properties);

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    switch (renderer.custom_config.texture_filtering)
    {
    case NEAREST:
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        break;
    case NEAREST_IF_CLOSE_TO_CAMERA_ELSE_BILINEAR:
    case NEAREST_IF_CLOSE_TO_CAMERA_ELSE_TRILINEAR:
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        break;
    case BILINEAR:
    case TRILINEAR:
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        break;
    }
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (renderer.custom_config.max_anisotropy < 1.0f)
    {
        sampler_info.anisotropyEnable = VK_FALSE;
    }
    else
    {
        sampler_info.anisotropyEnable = VK_TRUE;
        sampler_info.maxAnisotropy = std::clamp(
            renderer.custom_config.max_anisotropy,
            1.0f,
            properties.limits.maxSamplerAnisotropy);
    }
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    if (renderer.custom_config.texture_filtering == TRILINEAR ||
        renderer.custom_config.texture_filtering == NEAREST_IF_CLOSE_TO_CAMERA_ELSE_TRILINEAR)
    {
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
    else
    {
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = static_cast<float>(render_texture.mip_levels);
    sampler_info.mipLodBias = 0.0f;

    if (vkCreateSampler(renderer.device, &sampler_info, nullptr, &render_texture.sampler) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create texture sampler!");
    }
}

void ise::rendering::vulkan_create_textures_description_set(VulkanRendererData& renderer, RenderObject& render_object)
{
    std::lock_guard<std::mutex> lock(renderer.mutex);

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(renderer.physical_device, &physical_device_properties);

    if (render_object.textures.size() > physical_device_properties.limits.maxPerStageDescriptorSamplers)
    {
        throw std::runtime_error(std::format("Too many textures. Textures: {0} Limit: {1}", render_object.textures.size(), physical_device_properties.limits.maxPerStageDescriptorSamplers));
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = renderer.descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &renderer.descriptor_set_layout_textures;

    if (vkAllocateDescriptorSets(renderer.device, &alloc_info, &render_object.texture_description_set) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    std::vector<VkWriteDescriptorSet> descriptor_writes;
    for (size_t i = 0; i < render_object.textures.size(); i++)
    {
        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = renderer.render_textures[render_object.textures[i]]->image_view;
        image_info.sampler = renderer.render_textures[render_object.textures[i]]->sampler;

        VkWriteDescriptorSet descriptor_write;
        descriptor_write.pNext = nullptr;
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = render_object.texture_description_set;
        descriptor_write.dstBinding = i;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptor_write.descriptorCount = 1;
        descriptor_write.pImageInfo = &image_info;

        descriptor_writes.push_back(descriptor_write);
    }

    vkUpdateDescriptorSets(renderer.device, static_cast<uint32_t>(descriptor_writes.size()), descriptor_writes.data(), 0, nullptr);
}

void ise::rendering::vulkan_load_model_geometry(VulkanRendererData& renderer, RenderObject& render_object, std::vector<float> offset)
{
    std::lock_guard<std::mutex> lock(renderer.mutex);

    std::unordered_map<Vertex, uint32_t> unique_vertices{};

    for (const auto& shape : render_object.geometry.shapes)
    {
        for (const auto& index : shape.mesh.indices)
        {
            Vertex vertex{};

            vertex.pos = {
                render_object.geometry.attrib.vertices[3 * index.vertex_index + 0] + offset[0],
                render_object.geometry.attrib.vertices[3 * index.vertex_index + 1] + offset[1],
                render_object.geometry.attrib.vertices[3 * index.vertex_index + 2] + offset[2]
            };

            vertex.tex_coord = {
                render_object.geometry.attrib.texcoords[2 * index.texcoord_index + 0],
                1.0f - render_object.geometry.attrib.texcoords[2 * index.texcoord_index + 1]
            };

            vertex.color = { 1.0f, 1.0f, 1.0f };

            if (unique_vertices.count(vertex) == 0)
            {
                unique_vertices[vertex] = static_cast<uint32_t>(renderer.vertices.size());
                renderer.vertices.push_back(vertex);
            }

            renderer.indices.push_back(unique_vertices[vertex]);
        }
    }

    vulkan_update_index_buffer(renderer);
    vulkan_update_vertex_buffer(renderer);
}

void ise::rendering::vulkan_draw_frame(VulkanRendererData& renderer)
{
    std::lock_guard<std::mutex> lock(renderer.mutex);

    VKRH(vkWaitForFences(renderer.device, 1, &renderer.in_flight_fences[renderer.current_frame], VK_TRUE, UINT64_MAX));

    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(renderer.device, renderer.swap_chain, UINT64_MAX, renderer.image_available_semaphores[renderer.current_frame], VK_NULL_HANDLE, &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        vulkan_recreate_swap_chain(renderer);
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    vulkan_update_uniform_buffer(renderer, renderer.current_frame);

    VKRH(vkResetFences(renderer.device, 1, &renderer.in_flight_fences[renderer.current_frame]));
    VKRH(vkResetCommandBuffer(renderer.command_buffers[renderer.current_frame], /*VkCommandBufferResetFlagBits*/ 0));

    vulkan_record_command_buffer(renderer, renderer.command_buffers[renderer.current_frame], image_index);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = { renderer.image_available_semaphores[renderer.current_frame] };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &renderer.command_buffers[renderer.current_frame];

    VkSemaphore signal_semaphores[] = { renderer.render_finished_semaphores[renderer.current_frame] };
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    if (vkQueueSubmit(renderer.graphics_queue, 1, &submit_info, renderer.in_flight_fences[renderer.current_frame]) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;

    VkSwapchainKHR swap_chains[] = { renderer.swap_chain };
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swap_chains;

    present_info.pImageIndices = &image_index;

    result = vkQueuePresentKHR(renderer.present_queue, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || renderer.force_recreate_swapchain)
    {
        renderer.force_recreate_swapchain = false;
        vulkan_recreate_swap_chain(renderer);
    }
    else if (result != VK_SUCCESS)
    {
        throw std::runtime_error("failed to present swap chain image!");
    }

    renderer.current_frame = (renderer.current_frame + 1) % renderer.custom_config.max_frames_in_flight;
}

void ise::rendering::vulkan_cleanup(VulkanRendererData& renderer)
{
    VKRH(vkDeviceWaitIdle(renderer.device));

    vulkan_cleanup_swap_chain(renderer);

    vkDestroyPipeline(renderer.device, renderer.graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(renderer.device, renderer.pipeline_layout, nullptr);
    vkDestroyRenderPass(renderer.device, renderer.render_pass, nullptr);

    for (size_t i = 0; i < renderer.custom_config.max_frames_in_flight; i++)
    {
        vkDestroyBuffer(renderer.device, renderer.uniform_buffers[i], nullptr);
        vkFreeMemory(renderer.device, renderer.uniform_buffers_memory[i], nullptr);
    }

    vkDestroyDescriptorPool(renderer.device, renderer.descriptor_pool, nullptr);

    for (auto render_texture : renderer.render_textures)
    {
        vkDestroySampler(renderer.device, render_texture.second->sampler, nullptr);
        vkDestroyImageView(renderer.device, render_texture.second->image_view, nullptr);

        vkDestroyImage(renderer.device, render_texture.second->image, nullptr);
        vkFreeMemory(renderer.device, render_texture.second->image_memory, nullptr);

        delete render_texture.second;
    }
    renderer.render_textures.clear();

    for (int i = 0; i < renderer.render_objects.size(); ++i)
    {
        delete renderer.render_objects[i];
    }
    renderer.render_objects.clear();

    vkDestroyDescriptorSetLayout(renderer.device, renderer.descriptor_set_layout_uniform_buffers, nullptr);
    vkDestroyDescriptorSetLayout(renderer.device, renderer.descriptor_set_layout_textures, nullptr);

    vkDestroyBuffer(renderer.device, renderer.index_buffer, nullptr);
    vkFreeMemory(renderer.device, renderer.index_buffer_memory, nullptr);

    vkDestroyBuffer(renderer.device, renderer.vertex_buffer, nullptr);
    vkFreeMemory(renderer.device, renderer.vertex_buffer_memory, nullptr);

    for (size_t i = 0; i < renderer.custom_config.max_frames_in_flight; i++)
    {
        vkDestroySemaphore(renderer.device, renderer.render_finished_semaphores[i], nullptr);
        vkDestroySemaphore(renderer.device, renderer.image_available_semaphores[i], nullptr);
        vkDestroyFence(renderer.device, renderer.in_flight_fences[i], nullptr);
    }

    vkDestroyCommandPool(renderer.device, renderer.command_pool, nullptr);

    vkDestroyDevice(renderer.device, nullptr);

    if (renderer.custom_config.enable_validation_layers)
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(renderer.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            func(renderer.instance, renderer.debug_messenger, nullptr);
        }
    }

    vkDestroySurfaceKHR(renderer.instance, renderer.surface, nullptr);
    vkDestroyInstance(renderer.instance, nullptr);
}

bool ise::rendering::vulkan_check_validation_layer_support(VulkanRendererData& renderer)
{
    uint32_t layer_count;
    VKRH(vkEnumerateInstanceLayerProperties(&layer_count, nullptr));

    std::vector<VkLayerProperties> available_layers(layer_count);
    VKRH(vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data()));

    for (const char* layer_name : renderer.validation_layers)
    {
        bool layer_found = false;

        for (const auto& layer_properties : available_layers)
        {
            if (strcmp(layer_name, layer_properties.layerName) == 0)
            {
                layer_found = true;
                break;
            }
        }

        if (!layer_found)
        {
            return false;
        }
    }

    return true;
}

bool ise::rendering::vulkan_is_device_suitable(VkPhysicalDevice device, VulkanRendererData& renderer)
{
    QueueFamilyIndices indices = vulkan_find_queue_families(device, renderer);

    bool extensions_supported = vulkan_check_device_extension_support(device, renderer);

    bool swap_chain_adequate = false;
    if (extensions_supported)
    {
        SwapChainSupportDetails swap_chain_support = vulkan_query_swap_chain_support(device, renderer);
        swap_chain_adequate = !swap_chain_support.formats.empty() && !swap_chain_support.present_modes.empty();
    }

    VkPhysicalDeviceFeatures supported_features;
    VKRH(vkGetPhysicalDeviceFeatures(device, &supported_features));

    return indices.is_complete() && extensions_supported && swap_chain_adequate && supported_features.samplerAnisotropy;
}

ise::rendering::QueueFamilyIndices ise::rendering::vulkan_find_queue_families(VkPhysicalDevice device, VulkanRendererData& renderer)
{
    QueueFamilyIndices indices;

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

    int i = 0;
    for (const auto& queue_family : queue_families)
    {
        if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphics_family = i;
        }

        VkBool32 present_support = false;
        VKRH(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, renderer.surface, &present_support));;

        if (present_support)
        {
            indices.present_family = i;
        }

        if (indices.is_complete())
        {
            break;
        }

        i++;
    }

    return indices;
}

bool ise::rendering::vulkan_check_device_extension_support(VkPhysicalDevice device, VulkanRendererData& renderer)
{
    uint32_t extension_count;
    VKRH(vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr));

    std::vector<VkExtensionProperties> available_extensions(extension_count);
    VKRH(vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data()));

    std::set<std::string> required_extensions(renderer.device_extensions.begin(), renderer.device_extensions.end());

    for (const auto& extension : available_extensions)
    {
        required_extensions.erase(extension.extensionName);
    }

    return required_extensions.empty();
}

ise::rendering::SwapChainSupportDetails ise::rendering::vulkan_query_swap_chain_support(VkPhysicalDevice device, VulkanRendererData& renderer)
{
    SwapChainSupportDetails details;

    VKRH(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, renderer.surface, &details.capabilities));

    uint32_t format_count;
    VKRH(vkGetPhysicalDeviceSurfaceFormatsKHR(device, renderer.surface, &format_count, nullptr));

    if (format_count != 0)
    {
        details.formats.resize(format_count);
        VKRH(vkGetPhysicalDeviceSurfaceFormatsKHR(device, renderer.surface, &format_count, details.formats.data()));
    }

    uint32_t present_mode_count;
    VKRH(vkGetPhysicalDeviceSurfacePresentModesKHR(device, renderer.surface, &present_mode_count, nullptr));

    if (present_mode_count != 0)
    {
        details.present_modes.resize(present_mode_count);
        VKRH(vkGetPhysicalDeviceSurfacePresentModesKHR(device, renderer.surface, &present_mode_count, details.present_modes.data()));
    }

    return details;
}

VkSampleCountFlagBits ise::rendering::vulkan_get_max_usable_sample_count(VulkanRendererData& renderer)
{
    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(renderer.physical_device, &physical_device_properties);

    uint32_t acceptable_sample_count = VK_SAMPLE_COUNT_1_BIT;
    uint32_t current_sample_count = VK_SAMPLE_COUNT_1_BIT;
    while (true)
    {
        acceptable_sample_count |= current_sample_count;

        if (renderer.custom_config.msaa_sample_target & current_sample_count)
        {
            break;
        }

        if (current_sample_count & VK_SAMPLE_COUNT_64_BIT)
        {
            break;
        }

        current_sample_count <<= 1;
    }

    VkSampleCountFlags possible_sample_count = physical_device_properties.limits.framebufferColorSampleCounts & physical_device_properties.limits.framebufferDepthSampleCounts;
    if (possible_sample_count & acceptable_sample_count & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
    if (possible_sample_count & acceptable_sample_count & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
    if (possible_sample_count & acceptable_sample_count & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
    if (possible_sample_count & acceptable_sample_count & VK_SAMPLE_COUNT_8_BIT)  return VK_SAMPLE_COUNT_8_BIT;
    if (possible_sample_count & acceptable_sample_count & VK_SAMPLE_COUNT_4_BIT)  return VK_SAMPLE_COUNT_4_BIT;
    if (possible_sample_count & acceptable_sample_count & VK_SAMPLE_COUNT_2_BIT)  return VK_SAMPLE_COUNT_2_BIT;

    return VK_SAMPLE_COUNT_1_BIT;
}

VkSurfaceFormatKHR ise::rendering::vulkan_choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& available_formats)
{
    for (const auto& available_format : available_formats)
    {
        if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB && available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return available_format;
        }
    }

    return available_formats[0];
}

VkPresentModeKHR ise::rendering::vulkan_choose_swap_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes, VulkanRendererData& renderer)
{
    if (renderer.custom_config.v_sync)
    {
        for (const auto& available_present_mode : available_present_modes)
        {
            if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return available_present_mode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    return VK_PRESENT_MODE_IMMEDIATE_KHR;
}

VkImageView ise::rendering::vulkan_create_image_view(VulkanRendererData& renderer, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels)
{
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect_flags;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mip_levels;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VkImageView image_view;
    if (vkCreateImageView(renderer.device, &view_info, nullptr, &image_view) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create texture image view!");
    }

    return image_view;
}

VkFormat ise::rendering::vulkan_find_supported_format(VulkanRendererData& renderer, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
    for (VkFormat format : candidates)
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(renderer.physical_device, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
        {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

VkShaderModule ise::rendering::vulkan_create_shader_module(VulkanRendererData& renderer, const std::vector<char>& code)
{
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shader_module;
    if (vkCreateShaderModule(renderer.device, &create_info, nullptr, &shader_module) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create shader module!");
    }

    return shader_module;
}

void ise::rendering::vulkan_create_image(VulkanRendererData& renderer, uint32_t width, uint32_t height, uint32_t mip_levels, VkSampleCountFlagBits num_samples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& image_memory)
{
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = mip_levels;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = tiling;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = num_samples;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(renderer.device, &image_info, nullptr, &image) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(renderer.device, image, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = vulkan_find_memory_type(renderer, mem_requirements.memoryTypeBits, properties);

    if (vkAllocateMemory(renderer.device, &alloc_info, nullptr, &image_memory) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate image memory!");
    }

    VKRH(vkBindImageMemory(renderer.device, image, image_memory, 0));
}

uint32_t ise::rendering::vulkan_find_memory_type(VulkanRendererData& renderer, uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(renderer.physical_device, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

void ise::rendering::vulkan_create_buffer(VulkanRendererData& renderer, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& buffer_memory)
{
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(renderer.device, &buffer_info, nullptr, &buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(renderer.device, buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = vulkan_find_memory_type(renderer, mem_requirements.memoryTypeBits, properties);

    if (vkAllocateMemory(renderer.device, &alloc_info, nullptr, &buffer_memory) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    VKRH(vkBindBufferMemory(renderer.device, buffer, buffer_memory, 0));
}

void ise::rendering::vulkan_transition_image_layout(VulkanRendererData& renderer, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels)
{
    VkCommandBuffer command_buffer = vulkan_begin_single_time_commands(renderer);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mip_levels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        command_buffer,
        source_stage, destination_stage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vulkan_end_single_time_commands(renderer, command_buffer);
}

void ise::rendering::vulkan_copy_buffer_to_image(VulkanRendererData& renderer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkCommandBuffer command_buffer = vulkan_begin_single_time_commands(renderer);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vulkan_end_single_time_commands(renderer, command_buffer);
}

void ise::rendering::vulkan_generate_mipmaps(VulkanRendererData& renderer, VkImage image, VkFormat image_format, int32_t tex_width, int32_t tex_height, uint32_t mip_levels)
{
    // Check if image format supports linear blitting
    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(renderer.physical_device, image_format, &format_properties);

    if (!(format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
    {
        throw std::runtime_error("texture image format does not support linear blitting!");
    }

    VkCommandBuffer command_buffer = vulkan_begin_single_time_commands(renderer);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mip_width = tex_width;
    int32_t mip_height = tex_height;

    for (uint32_t i = 1; i < mip_levels; i++)
    {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mip_width, mip_height, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { mip_width > 1 ? mip_width / 2 : 1, mip_height > 1 ? mip_height / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(command_buffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        if (mip_width > 1) mip_width /= 2;
        if (mip_height > 1) mip_height /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mip_levels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    vulkan_end_single_time_commands(renderer, command_buffer);
}

void ise::rendering::vulkan_copy_buffer(VulkanRendererData& renderer, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = vulkan_begin_single_time_commands(renderer);

    VkBufferCopy copy_region{};
    copy_region.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copy_region);

    vulkan_end_single_time_commands(renderer, commandBuffer);
}

void ise::rendering::vulkan_create_sync_objects(VulkanRendererData& renderer)
{
    renderer.image_available_semaphores.resize(renderer.custom_config.max_frames_in_flight);
    renderer.render_finished_semaphores.resize(renderer.custom_config.max_frames_in_flight);
    renderer.in_flight_fences.resize(renderer.custom_config.max_frames_in_flight);

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < renderer.custom_config.max_frames_in_flight; i++)
    {
        if (vkCreateSemaphore(renderer.device, &semaphore_info, nullptr, &renderer.image_available_semaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(renderer.device, &semaphore_info, nullptr, &renderer.render_finished_semaphores[i]) != VK_SUCCESS ||
            vkCreateFence(renderer.device, &fence_info, nullptr, &renderer.in_flight_fences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

void ise::rendering::vulkan_cleanup_swap_chain(VulkanRendererData& renderer)
{
    vkDestroyImageView(renderer.device, renderer.depth_image_view, nullptr);
    vkDestroyImage(renderer.device, renderer.depth_image, nullptr);
    vkFreeMemory(renderer.device, renderer.depth_image_memory, nullptr);

    if (!(renderer.msaa_samples & VK_SAMPLE_COUNT_1_BIT))
    {
        vkDestroyImageView(renderer.device, renderer.color_image_view, nullptr);
        vkDestroyImage(renderer.device, renderer.color_image, nullptr);
        vkFreeMemory(renderer.device, renderer.color_image_memory, nullptr);
    }

    for (auto frame_buffer : renderer.swap_chain_framebuffers)
    {
        vkDestroyFramebuffer(renderer.device, frame_buffer, nullptr);
    }

    for (auto image_view : renderer.swap_chain_image_views)
    {
        vkDestroyImageView(renderer.device, image_view, nullptr);
    }

    vkDestroySwapchainKHR(renderer.device, renderer.swap_chain, nullptr);
}

void ise::rendering::vulkan_recreate_swap_chain(VulkanRendererData& renderer)
{
    VKRH(vkDeviceWaitIdle(renderer.device));

    vulkan_cleanup_swap_chain(renderer);

    vulkan_create_swap_chain(renderer);
    vulkan_create_image_views(renderer);
    vulkan_create_color_resources(renderer);
    vulkan_create_depth_resources(renderer);
    vulkan_create_framebuffers(renderer);
}

void ise::rendering::vulkan_update_vertex_buffer(VulkanRendererData& renderer)
{
    VkDeviceSize new_buffer_size = sizeof(renderer.vertices[0]) * renderer.vertices.size();
    if (renderer.vertex_buffer_size > 0)
    {
        vkDestroyBuffer(renderer.device, renderer.vertex_buffer, nullptr);
        vkFreeMemory(renderer.device, renderer.vertex_buffer_memory, nullptr);
    }
    renderer.vertex_buffer_size = new_buffer_size;

    if (new_buffer_size == 0)
    {
        return;
    }

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    vulkan_create_buffer(renderer, new_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

    void* data;
    VKRH(vkMapMemory(renderer.device, staging_buffer_memory, 0, new_buffer_size, 0, &data));
    memcpy(data, renderer.vertices.data(), (size_t)new_buffer_size);
    vkUnmapMemory(renderer.device, staging_buffer_memory);

    vulkan_create_buffer(renderer, new_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, renderer.vertex_buffer, renderer.vertex_buffer_memory);

    vulkan_copy_buffer(renderer, staging_buffer, renderer.vertex_buffer, new_buffer_size);

    vkDestroyBuffer(renderer.device, staging_buffer, nullptr);
    vkFreeMemory(renderer.device, staging_buffer_memory, nullptr);
}

void ise::rendering::vulkan_update_index_buffer(VulkanRendererData& renderer)
{
    VkDeviceSize new_buffer_size = sizeof(renderer.indices[0]) * renderer.indices.size();
    if (renderer.index_buffer_size > 0)
    {
        vkDestroyBuffer(renderer.device, renderer.index_buffer, nullptr);
        vkFreeMemory(renderer.device, renderer.index_buffer_memory, nullptr);
    }
    renderer.index_buffer_size = new_buffer_size;

    if (new_buffer_size == 0)
    {
        return;
    }

    VkBuffer staging_buffer;
    VkDeviceMemory staging_buffer_memory;
    vulkan_create_buffer(renderer, new_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

    void* data;
    VKRH(vkMapMemory(renderer.device, staging_buffer_memory, 0, new_buffer_size, 0, &data));
    memcpy(data, renderer.indices.data(), (size_t)new_buffer_size);
    vkUnmapMemory(renderer.device, staging_buffer_memory);

    vulkan_create_buffer(renderer, new_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, renderer.index_buffer, renderer.index_buffer_memory);

    vulkan_copy_buffer(renderer, staging_buffer, renderer.index_buffer, new_buffer_size);

    vkDestroyBuffer(renderer.device, staging_buffer, nullptr);
    vkFreeMemory(renderer.device, staging_buffer_memory, nullptr);
}

VkCommandBuffer ise::rendering::vulkan_begin_single_time_commands(VulkanRendererData& renderer)
{
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = renderer.command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    VKRH(vkAllocateCommandBuffers(renderer.device, &alloc_info, &command_buffer));

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VKRH(vkBeginCommandBuffer(command_buffer, &begin_info));

    return command_buffer;
}

void ise::rendering::vulkan_end_single_time_commands(VulkanRendererData& renderer, VkCommandBuffer command_buffer)
{
    VKRH(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    VKRH(vkQueueSubmit(renderer.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
    VKRH(vkQueueWaitIdle(renderer.graphics_queue));

    vkFreeCommandBuffers(renderer.device, renderer.command_pool, 1, &command_buffer);
}

void ise::rendering::vulkan_update_uniform_buffer(VulkanRendererData& renderer, uint32_t current_image)
{
    static auto start_time = std::chrono::high_resolution_clock::now();

    auto current_time = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

    UniformBufferObject ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    // eye is camera position
    // center is lookAt position
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    switch (renderer.custom_config.projection_type)
    {
        case PERSPECTIVE_PROJECTION:
            ubo.proj = glm::perspective(
                glm::radians(renderer.custom_config.perspective_vertical_fov),
                renderer.swap_chain_extent.width / (float)renderer.swap_chain_extent.height,
                renderer.custom_config.z_near,
                renderer.custom_config.z_far);
            break;
        case ORTHOGRAPHIC_PROJECTION:
            float width_proportion = (float)renderer.swap_chain_extent.width / (float)renderer.swap_chain_extent.height;
            if (width_proportion > 1.0f)
            {
                ubo.proj = glm::ortho(
                    renderer.custom_config.orthographic_scale_factor * width_proportion / (float)-2.0,
                    renderer.custom_config.orthographic_scale_factor * width_proportion / (float)2.0,
                    renderer.custom_config.orthographic_scale_factor * -0.5f,
                    renderer.custom_config.orthographic_scale_factor * 0.5f,
                    renderer.custom_config.z_near,
                    renderer.custom_config.z_far);
            }
            else
            {
                float height_proportion = (float)renderer.swap_chain_extent.height / (float)renderer.swap_chain_extent.width;
                ubo.proj = glm::ortho(
                    renderer.custom_config.orthographic_scale_factor * -0.5f,
                    renderer.custom_config.orthographic_scale_factor * 0.5f,
                    renderer.custom_config.orthographic_scale_factor * height_proportion / (float)-2.0f,
                    renderer.custom_config.orthographic_scale_factor * height_proportion / (float)2.0f,
                    renderer.custom_config.z_near,
                    renderer.custom_config.z_far);
            }
            break;
    }

    ubo.proj[1][1] *= -1;

    memcpy(renderer.uniform_buffers_mapped[current_image], &ubo, sizeof(ubo));
}

void ise::rendering::vulkan_record_command_buffer(VulkanRendererData& renderer, VkCommandBuffer command_buffer, uint32_t image_index)
{
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = renderer.render_pass;
    render_pass_info.framebuffer = renderer.swap_chain_framebuffers[image_index];
    render_pass_info.renderArea.offset = { 0, 0 };
    render_pass_info.renderArea.extent = renderer.swap_chain_extent;

    std::array<VkClearValue, 2> clear_values{};
    clear_values[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
    clear_values[1].depthStencil = { 1.0f, 0 };

    render_pass_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.graphics_pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)renderer.swap_chain_extent.width;
    viewport.height = (float)renderer.swap_chain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = renderer.swap_chain_extent;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    VkBuffer vertex_buffers[] = { renderer.vertex_buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

    vkCmdBindIndexBuffer(command_buffer, renderer.index_buffer, 0, VK_INDEX_TYPE_UINT32);

    std::vector<VkDescriptorSet> descriptor_sets;
    descriptor_sets.push_back(renderer.uniform_buffers_descriptor_sets[renderer.current_frame]);
    descriptor_sets.push_back(renderer.render_objects[0]->texture_description_set);

    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.pipeline_layout, 0, descriptor_sets.size(), descriptor_sets.data(), 0, nullptr);

    vkCmdDrawIndexed(command_buffer, static_cast<uint32_t>(renderer.indices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(command_buffer);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void ise::rendering::vulkan_handle_vk_result(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        throw new std::runtime_error(std::format("Vulkan failed with code: {0}", (int)result));
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL ise::rendering::vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data)
{
    std::cerr << "validation layer: " << p_callback_data->pMessage << std::endl;
    return VK_FALSE;
}