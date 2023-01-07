#include "VulkanRenderer.h"

#include <chrono>
#include <SDL2/SDL.h>
#include <SDL2/SDL_Vulkan.h>

#include "VulkanRendererUgly.h"

ise::rendering::VulkanRenderer::VulkanRenderer()
{
    SDL_Vulkan_LoadLibrary(nullptr);

    this->m_mutex = SDL_CreateMutex();
    this->m_finished = SDL_CreateCond();
    this->m_render_thread = NULL;

    this->sdl_create_window();
    this->sdl_get_instance_extensions();

    vulkan_create_instance(this->m_data);
    vulkan_setup_debug_messenger(this->m_data);
    this->sdl_create_surface();
    vulkan_pick_physical_device(this->m_data);
    vulkan_create_logical_device(this->m_data);
    vulkan_create_swap_chain(this->m_data);
    vulkan_create_image_views(this->m_data);
    vulkan_create_render_pass(this->m_data);
    vulkan_create_descriptor_set_layout(this->m_data);
    vulkan_create_graphics_pipeline(this->m_data);
    vulkan_create_command_pool(this->m_data);
    vulkan_create_color_resources(this->m_data);
    vulkan_create_depth_resources(this->m_data);
    vulkan_create_framebuffers(this->m_data);
    vulkan_create_texture_image(this->m_data);
    vulkan_create_texture_image_view(this->m_data);
    vulkan_create_texture_sampler(this->m_data);
    vulkan_load_model(this->m_data);
    vulkan_create_vertex_buffer(this->m_data);
    vulkan_create_index_buffer(this->m_data);
    vulkan_create_uniform_buffers(this->m_data);
    vulkan_create_descriptor_pool(this->m_data);
    vulkan_create_descriptor_sets(this->m_data);
    vulkan_create_command_buffers(this->m_data);
    vulkan_create_sync_objects(this->m_data);
}

ise::rendering::VulkanRenderer::~VulkanRenderer()
{
    this->stop();

    SDL_DestroyWindow(this->m_window);
    SDL_DestroyMutex(this->m_mutex);
    SDL_DestroyCond(this->m_finished);

    SDL_WaitThread(this->m_render_thread, NULL);
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

    SDL_DestroyWindow(this->m_window);

    this->m_already_started = false;
    this->m_accepting_new_draw_call = false;

    this->sdl_create_window();
    this->sdl_get_instance_extensions();

    vulkan_create_instance(this->m_data);
    vulkan_setup_debug_messenger(this->m_data);
    this->sdl_create_surface();
    vulkan_pick_physical_device(this->m_data);
    vulkan_create_logical_device(this->m_data);
    vulkan_create_swap_chain(this->m_data);
    vulkan_create_image_views(this->m_data);
    vulkan_create_render_pass(this->m_data);
    vulkan_create_descriptor_set_layout(this->m_data);
    vulkan_create_graphics_pipeline(this->m_data);
    vulkan_create_command_pool(this->m_data);
    vulkan_create_color_resources(this->m_data);
    vulkan_create_depth_resources(this->m_data);
    vulkan_create_framebuffers(this->m_data);
    vulkan_create_texture_image(this->m_data);
    vulkan_create_texture_image_view(this->m_data);
    vulkan_create_texture_sampler(this->m_data);
    vulkan_load_model(this->m_data);
    vulkan_create_vertex_buffer(this->m_data);
    vulkan_create_index_buffer(this->m_data);
    vulkan_create_uniform_buffers(this->m_data);
    vulkan_create_descriptor_pool(this->m_data);
    vulkan_create_descriptor_sets(this->m_data);
    vulkan_create_command_buffers(this->m_data);
    vulkan_create_sync_objects(this->m_data);

    this->start();
}

bool ise::rendering::VulkanRenderer::windows_match(SDL_Window* window)
{
    return window == this->m_window;
}

void ise::rendering::VulkanRenderer::handle_window_resize()
{
    this->m_data.force_recreate_swapchain = true;
}

void ise::rendering::VulkanRenderer::sdl_create_window()
{
    this->m_window = SDL_CreateWindow(
        "Example Vulkan Application",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (this->m_window == NULL)
    {
        throw new std::runtime_error(std::format("SDL window creation failed with message {0}", SDL_GetError()));
    }

    SDL_SetWindowMinimumSize(this->m_window, 250, 250);
}

void ise::rendering::VulkanRenderer::sdl_get_instance_extensions()
{
    unsigned int extension_count = 0;
    handle_sdl_bool(SDL_Vulkan_GetInstanceExtensions(this->m_window, &extension_count, nullptr));

    this->m_data.instance_extensions.resize(extension_count);
    handle_sdl_bool(SDL_Vulkan_GetInstanceExtensions(this->m_window, &extension_count, this->m_data.instance_extensions.data()));
}

void ise::rendering::VulkanRenderer::sdl_create_surface()
{
    handle_sdl_bool(SDL_Vulkan_CreateSurface(this->m_window, this->m_data.instance, &this->m_data.surface));
}

void ise::rendering::VulkanRenderer::handle_sdl_bool(SDL_bool sdl_bool)
{
    if (sdl_bool == SDL_FALSE)
    {
        throw new std::runtime_error(std::string("Vulkan SDL(bool) failed with error: ") + SDL_GetError());
    }
}

void ise::rendering::VulkanRenderer::handle_sdl_int(int result)
{
    if (result != 0)
    {
        throw new std::runtime_error(std::format("Vulkan SDL(int) failed with error: {0} {1}", result, SDL_GetError()));
    }
}

int ise::rendering::VulkanRenderer::render_thread_handler(void* data)
{
    VulkanRenderer* renderer = (VulkanRenderer*)data;

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
    }

    SDL_CondSignal(renderer->m_finished);

    return 0;
}