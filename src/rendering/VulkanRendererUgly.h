#pragma once

#include "SDL2/SDL.h"
#include "VkBootstrap.h"

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
        struct VulkanRendererData {
            bool v_sync = true;
            bool force_recreate_swapchain = false;
            int max_frames_in_flight = 3;

            vkb::Instance instance;
            vkb::Device device;
            vkb::Swapchain swapchain;

            SDL_Window* window;
            VkSurfaceKHR surface;

            VkQueue graphics_queue;
            VkQueue present_queue;

            std::vector<VkImage> swapchain_images;
            std::vector<VkImageView> swapchain_image_views;
            std::vector<VkFramebuffer> framebuffers;

            VkRenderPass render_pass;
            VkPipelineLayout pipeline_layout;
            VkPipeline graphics_pipeline;

            std::vector<VkSemaphore> available_semaphores;
            std::vector<VkSemaphore> finished_semaphore;
            std::vector<VkFence> in_flight_fences;
            std::vector<VkFence> image_in_flight;
            size_t current_frame = 0;

            VkCommandPool command_pool;
            std::vector<VkCommandBuffer> command_buffers;
        };

        void vulkan_device_initialization(VulkanRendererData& renderer);
        void vulkan_create_swapchain(VulkanRendererData& renderer);
        void vulkan_recreate_swapchain(VulkanRendererData& renderer);
        void vulkan_get_queues(VulkanRendererData& renderer);
        void vulkan_create_render_pass(VulkanRendererData& renderer);
        VkShaderModule vulkan_create_shader_module(VulkanRendererData& renderer, const std::vector<char>& code);
        void vulkan_create_graphics_pipeline(VulkanRendererData& renderer);
        void vulkan_create_framebuffers(VulkanRendererData& renderer);
        void vulkan_create_command_pool(VulkanRendererData& renderer);
        void vulkan_create_command_buffers(VulkanRendererData& renderer);
        void vulkan_create_sync_objects(VulkanRendererData& renderer);
        void vulkan_draw_frame(VulkanRendererData& renderer);
        void vulkan_cleanup(VulkanRendererData& renderer);
        void vulkan_handle_vk_result(VkResult result);
        void vulkan_handle_sdl_bool(SDL_bool sdl_bool);
        void vulkan_handle_sdl_int(int result);
    }
}