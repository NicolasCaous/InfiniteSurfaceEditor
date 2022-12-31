#include "VulkanRenderer.h"

#include <chrono>
#include <iostream>

#include "VulkanRendererUgly.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_Vulkan.h"

ise::rendering::VulkanRenderer::VulkanRenderer()
{
    SDL_Vulkan_LoadLibrary(nullptr);

    this->m_mutex = SDL_CreateMutex();
    this->m_finished = SDL_CreateCond();
    this->m_render_thread = NULL;

    vulkan_device_initialization(this->m_data);
    vulkan_create_swapchain(this->m_data);
    vulkan_get_queues(this->m_data);
    vulkan_create_render_pass(this->m_data);
    vulkan_create_graphics_pipeline(this->m_data);
    vulkan_create_framebuffers(this->m_data);
    vulkan_create_command_pool(this->m_data);
    vulkan_create_command_buffers(this->m_data);
    vulkan_create_sync_objects(this->m_data);
}

ise::rendering::VulkanRenderer::~VulkanRenderer()
{
    this->stop();

    SDL_DestroyMutex(this->m_mutex);
    SDL_DestroyCond(this->m_finished);

    SDL_WaitThread(this->m_render_thread, NULL);
}

int ise::rendering::VulkanRenderer::render_thread_handler(void* data)
{
    VulkanRenderer* renderer = (VulkanRenderer*) data;

    renderer->m_accepting_new_draw_call = true;
    while (renderer->m_accepting_new_draw_call)
    {
        auto old_timestamp = std::chrono::high_resolution_clock::now();
        vulkan_draw_frame(renderer->m_data);

        auto new_timestamp = std::chrono::high_resolution_clock::now();
        auto elapsed_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(new_timestamp - old_timestamp).count();

        auto sleep_microseconds = (1000000 / renderer->m_max_frames_per_second) - elapsed_microseconds;
        if (sleep_microseconds > 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_microseconds));
        }
        std::cout << sleep_microseconds << std::endl;
    }

    SDL_CondSignal(renderer->m_finished);

    return 0;
}

void ise::rendering::VulkanRenderer::start()
{
    if (!this->m_already_started)
    {
        this->m_render_thread = SDL_CreateThread(VulkanRenderer::render_thread_handler, "VulkanRenderThread", (void*) this);
        this->m_already_started = true;
    }
}

void ise::rendering::VulkanRenderer::stop()
{
    SDL_LockMutex(this->m_mutex);
    if (this->m_accepting_new_draw_call)
    {
        this->m_accepting_new_draw_call = false;
        SDL_CondWait(this->m_finished, this->m_mutex);
        vulkan_cleanup(this->m_data);
    }
    SDL_UnlockMutex(this->m_mutex);
}

void ise::rendering::VulkanRenderer::recreate_renderer()
{
    this->stop();
    SDL_WaitThread(this->m_render_thread, NULL);

    this->m_already_started = false;
    this->m_accepting_new_draw_call = false;

    vulkan_device_initialization(this->m_data);
    vulkan_create_swapchain(this->m_data);
    vulkan_get_queues(this->m_data);
    vulkan_create_render_pass(this->m_data);
    vulkan_create_graphics_pipeline(this->m_data);
    vulkan_create_framebuffers(this->m_data);
    vulkan_create_command_pool(this->m_data);
    vulkan_create_command_buffers(this->m_data);
    vulkan_create_sync_objects(this->m_data);

    this->start();
}

bool ise::rendering::VulkanRenderer::windows_match(SDL_Window* window)
{
    return window == this->m_data.window;
}

void ise::rendering::VulkanRenderer::handle_window_resize()
{
    this->m_data.force_recreate_swapchain = true;
}