#include "VulkanRenderer.h"

#include <chrono>
#include <iostream>

#include "VulkanRendererUgly.h"
#include "SDL2/SDL.h"

ise::rendering::VulkanRenderer::VulkanRenderer()
{
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

    SDL_AddEventWatch(VulkanRenderer::SDL_event_watcher, this);
}

ise::rendering::VulkanRenderer::~VulkanRenderer()
{
    this->stop();

    SDL_DestroyMutex(this->m_mutex);
    SDL_DestroyCond(this->m_finished);
    SDL_DelEventWatch(VulkanRenderer::SDL_event_watcher, this);

    SDL_WaitThread(this->m_render_thread, NULL);
    SDL_Quit();
}

int ise::rendering::VulkanRenderer::render_thread_handler(void* data)
{
    VulkanRenderer* renderer = (VulkanRenderer*) data;

    bool running = renderer->m_accepting_new_draw_call;

    while (running)
    {
        auto old_timestamp = std::chrono::high_resolution_clock::now();
        vulkan_draw_frame(renderer->m_data);
        running = renderer->m_accepting_new_draw_call;

        auto new_timestamp = std::chrono::high_resolution_clock::now();
        auto elapsed_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(new_timestamp - old_timestamp).count();

        auto sleep_microseconds = (1000000 / renderer->m_max_frames_per_second) - elapsed_microseconds;
        if (sleep_microseconds > 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_microseconds));
        }
        std::cout << sleep_microseconds << std::endl;
    }

    SDL_LockMutex(renderer->m_mutex);
    SDL_CondSignal(renderer->m_finished);
    SDL_UnlockMutex(renderer->m_mutex);

    return 0;
}

int ise::rendering::VulkanRenderer::SDL_event_watcher(void* data, SDL_Event* event)
{
    VulkanRenderer* renderer = (VulkanRenderer*) data;

    if (event->type == SDL_QUIT)
    {
        renderer->stop();
    }

    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_RESIZED)
    {
        SDL_Window* win = SDL_GetWindowFromID(event->window.windowID);

        if (win == renderer->m_data.window) {
            renderer->m_data.force_refresh = true;
        }
    }

    if (event->type == SDL_KEYDOWN && event->key.keysym.scancode == SDL_SCANCODE_A)
    {
        //SDL_SetWindowSize(renderer->m_data.window, 800, 800);
        renderer->m_data.v_sync = true;
        renderer->m_data.force_refresh = true;
    }

    if (event->type == SDL_KEYDOWN && event->key.keysym.scancode == SDL_SCANCODE_S)
    {
        //SDL_SetWindowSize(renderer->m_data.window, 400, 400);
        renderer->m_data.v_sync = false;
        renderer->m_data.force_refresh = true;
    }

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

void ise::rendering::VulkanRenderer::wait_until_stop()
{
    if (!this->m_already_started)
    {
        throw new std::runtime_error("Can't wait renderer without starting render thread. Run start() before calling this function");
    }

    bool running = this->m_accepting_new_draw_call;

    while (running)
    {
        auto old_timestamp = std::chrono::high_resolution_clock::now();

        SDL_PumpEvents();
        running = this->m_accepting_new_draw_call;

        auto new_timestamp = std::chrono::high_resolution_clock::now();
        auto elapsed_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(new_timestamp - old_timestamp).count();

        old_timestamp = new_timestamp;

        auto sleep_microseconds = (1000000 / this->m_max_ticks_per_second) - elapsed_microseconds;
        if (sleep_microseconds > 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_microseconds));
        }
    }
}