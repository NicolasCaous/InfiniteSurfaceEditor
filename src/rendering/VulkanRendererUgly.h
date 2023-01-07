#pragma once

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

#include <vulkan/vulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include <stb_image.h>

#include <tiny_obj_loader.h>

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

#ifdef _WIN32
    #define VK_USE_PLATFORM_WIN32_KHR
    #define PLATFORM_SURFACE_EXTENSION_NAME VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif

namespace ise
{
    namespace rendering
    {
        struct QueueFamilyIndices
        {
            std::optional<uint32_t> graphics_family;
            std::optional<uint32_t> present_family;

            bool is_complete()
            {
                return graphics_family.has_value() && present_family.has_value();
            }
        };

        struct SwapChainSupportDetails
        {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> present_modes;
        };

        struct UniformBufferObject
        {
            alignas(16) glm::mat4 model;
            alignas(16) glm::mat4 view;
            alignas(16) glm::mat4 proj;
        };

        struct Vertex
        {
            glm::vec3 pos;
            glm::vec3 color;
            glm::vec2 tex_coord;

            static VkVertexInputBindingDescription get_binding_description()
            {
                VkVertexInputBindingDescription bindingDescription{};
                bindingDescription.binding = 0;
                bindingDescription.stride = sizeof(Vertex);
                bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                return bindingDescription;
            }

            static std::array<VkVertexInputAttributeDescription, 3> get_attribute_descriptions()
            {
                std::array<VkVertexInputAttributeDescription, 3> attribute_descriptions{};

                attribute_descriptions[0].binding = 0;
                attribute_descriptions[0].location = 0;
                attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                attribute_descriptions[0].offset = offsetof(Vertex, pos);

                attribute_descriptions[1].binding = 0;
                attribute_descriptions[1].location = 1;
                attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
                attribute_descriptions[1].offset = offsetof(Vertex, color);

                attribute_descriptions[2].binding = 0;
                attribute_descriptions[2].location = 2;
                attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
                attribute_descriptions[2].offset = offsetof(Vertex, tex_coord);

                return attribute_descriptions;
            }

            bool operator==(const Vertex& other) const
            {
                return pos == other.pos && color == other.color && tex_coord == other.tex_coord;
            }
        };

        typedef enum ProjectionType
        {
            PERSPECTIVE_PROJECTION = 0,
            ORTHOGRAPHIC_PROJECTION = 1
        } ProjectionType;

        typedef enum TextureFilteringType
        {
            NEAREST = 0,
            NEAREST_IF_CLOSE_TO_CAMERA_ELSE_BILINEAR = 1,
            NEAREST_IF_CLOSE_TO_CAMERA_ELSE_TRILINEAR = 2,
            BILINEAR = 3,
            TRILINEAR = 4
        } TextureFilteringType;

        struct VulkanRendererConfig
        {
            #ifdef _DEBUG
            bool enable_validation_layers = true;
            #else
            bool enable_validation_layers = false;
            #endif
            int max_frames_in_flight = 3;
            bool v_sync = true;
            VkSampleCountFlagBits msaa_sample_target = VK_SAMPLE_COUNT_1_BIT;
            float max_anisotropy = 0.0f; // 0 is disabled
            TextureFilteringType texture_filtering = TRILINEAR;

            float z_near = 0.1f;
            float z_far = 10000.0f;
            ProjectionType projection_type = ORTHOGRAPHIC_PROJECTION;

            float perspective_vertical_fov = 60.0f;

            float orthographic_scale_factor = 2.0f;
        };

        struct VulkanRendererData
        {
            std::vector<const char*> instance_extensions;
            const std::vector<const char*> validation_layers = {
                "VK_LAYER_KHRONOS_validation"
            };
            const std::vector<const char*> device_extensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME
            };
            
            VulkanRendererConfig custom_config;
            bool force_recreate_swapchain = false;
            uint32_t current_frame = 0;

            VkInstance instance;
            VkDebugUtilsMessengerEXT debug_messenger;
            VkSurfaceKHR surface;

            VkPhysicalDevice physical_device = VK_NULL_HANDLE;
            VkSampleCountFlagBits msaa_samples = VK_SAMPLE_COUNT_1_BIT;
            VkDevice device;

            VkQueue graphics_queue;
            VkQueue present_queue;

            VkSwapchainKHR swap_chain;
            std::vector<VkImage> swap_chain_images;
            VkFormat swap_chain_image_format;
            VkExtent2D swap_chain_extent;
            std::vector<VkImageView> swap_chain_image_views;
            std::vector<VkFramebuffer> swap_chain_framebuffers;

            VkRenderPass render_pass;
            VkDescriptorSetLayout descriptor_set_layout;
            VkPipelineLayout pipeline_layout;
            VkPipeline graphics_pipeline;

            VkCommandPool command_pool;
            std::vector<VkCommandBuffer> command_buffers;

            VkImage color_image;
            VkDeviceMemory color_image_memory;
            VkImageView color_image_view;

            VkImage depth_image;
            VkDeviceMemory depth_image_memory;
            VkImageView depth_image_view;

            uint32_t mip_levels;
            VkImage texture_image;
            VkDeviceMemory texture_image_memory;
            VkImageView texture_image_view;
            VkSampler texture_sampler;

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            VkBuffer vertex_buffer;
            VkDeviceMemory vertex_buffer_memory;
            VkBuffer index_buffer;
            VkDeviceMemory index_buffer_memory;

            std::vector<VkBuffer> uniform_buffers;
            std::vector<VkDeviceMemory> uniform_buffers_memory;
            std::vector<void*> uniform_buffers_mapped;

            VkDescriptorPool descriptor_pool;
            std::vector<VkDescriptorSet> descriptor_sets;

            std::vector<VkSemaphore> image_available_semaphores;
            std::vector<VkSemaphore> render_finished_semaphores;
            std::vector<VkFence> in_flight_fences;
        };

        // Vulkan init. Follow the fuctions bellow:
        void vulkan_create_instance(VulkanRendererData& renderer);
        void vulkan_setup_debug_messenger(VulkanRendererData& renderer);
        // Before continuing, you MUST create a surface by yourself and store it inside renderer.surface
        // This is intended to not make this file dependable on SDL
        void vulkan_pick_physical_device(VulkanRendererData& renderer);
        void vulkan_create_logical_device(VulkanRendererData& renderer);
        void vulkan_create_swap_chain(VulkanRendererData& renderer);
        void vulkan_create_image_views(VulkanRendererData& renderer);
        void vulkan_create_render_pass(VulkanRendererData& renderer);
        void vulkan_create_descriptor_set_layout(VulkanRendererData& renderer);
        void vulkan_create_graphics_pipeline(VulkanRendererData& renderer);
        void vulkan_create_command_pool(VulkanRendererData& renderer);
        void vulkan_create_color_resources(VulkanRendererData& renderer);
        void vulkan_create_depth_resources(VulkanRendererData& renderer);
        void vulkan_create_framebuffers(VulkanRendererData& renderer);
        void vulkan_create_texture_image(VulkanRendererData& renderer);
        void vulkan_create_texture_image_view(VulkanRendererData& renderer);
        void vulkan_create_texture_sampler(VulkanRendererData& renderer);
        void vulkan_load_model(VulkanRendererData& renderer);
        void vulkan_create_vertex_buffer(VulkanRendererData& renderer);
        void vulkan_create_index_buffer(VulkanRendererData& renderer);
        void vulkan_create_uniform_buffers(VulkanRendererData& renderer);
        void vulkan_create_descriptor_pool(VulkanRendererData& renderer);
        void vulkan_create_descriptor_sets(VulkanRendererData& renderer);
        void vulkan_create_command_buffers(VulkanRendererData& renderer);
        void vulkan_create_sync_objects(VulkanRendererData& renderer);

        // Public utilities
        void vulkan_draw_frame(VulkanRendererData& renderer);
        void vulkan_cleanup(VulkanRendererData& renderer);

        // Low level helper functions. DON'T USE!
        bool vulkan_check_validation_layer_support(VulkanRendererData& renderer);
        bool vulkan_is_device_suitable(VkPhysicalDevice device, VulkanRendererData& renderer);
        QueueFamilyIndices vulkan_find_queue_families(VkPhysicalDevice device, VulkanRendererData& renderer);
        bool vulkan_check_device_extension_support(VkPhysicalDevice device, VulkanRendererData& renderer);
        SwapChainSupportDetails vulkan_query_swap_chain_support(VkPhysicalDevice device, VulkanRendererData& renderer);
        VkSampleCountFlagBits vulkan_get_max_usable_sample_count(VulkanRendererData& renderer);
        VkSurfaceFormatKHR vulkan_choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& available_formats);
        VkPresentModeKHR vulkan_choose_swap_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes, VulkanRendererData& renderer);
        VkImageView vulkan_create_image_view(VulkanRendererData& renderer, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels);
        VkFormat vulkan_find_supported_format(VulkanRendererData& renderer, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
        VkShaderModule vulkan_create_shader_module(VulkanRendererData& renderer, const std::vector<char>& code);
        void vulkan_create_image(VulkanRendererData& renderer, uint32_t width, uint32_t height, uint32_t mip_levels, VkSampleCountFlagBits num_samples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& image_memory);
        uint32_t vulkan_find_memory_type(VulkanRendererData& renderer, uint32_t type_filter, VkMemoryPropertyFlags properties);
        void vulkan_create_buffer(VulkanRendererData& renderer, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& buffer_memory);
        void vulkan_transition_image_layout(VulkanRendererData& renderer, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout, uint32_t mip_levels);
        void vulkan_copy_buffer_to_image(VulkanRendererData& renderer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
        void vulkan_generate_mipmaps(VulkanRendererData& renderer, VkImage image, VkFormat image_format, int32_t tex_width, int32_t tex_height, uint32_t mip_levels);
        void vulkan_copy_buffer(VulkanRendererData& renderer, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        void vulkan_cleanup_swap_chain(VulkanRendererData& renderer);
        void vulkan_recreate_swap_chain(VulkanRendererData& renderer);

        // Low level command buffers stuff
        VkCommandBuffer vulkan_begin_single_time_commands(VulkanRendererData& renderer);
        void vulkan_end_single_time_commands(VulkanRendererData& renderer, VkCommandBuffer command_buffer);
        void vulkan_update_uniform_buffer(VulkanRendererData& renderer, uint32_t current_image);
        void vulkan_record_command_buffer(VulkanRendererData& renderer, VkCommandBuffer command_buffer, uint32_t image_index);

        void vulkan_handle_vk_result(VkResult result);

        static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data);
    }
}