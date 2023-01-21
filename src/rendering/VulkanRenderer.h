#pragma once

#include <SDL2/SDL.h>

#include "VulkanRendererUgly.h"

namespace ise
{
    namespace rendering
    {
        class VulkanRenderer
        {
        public:
            VulkanRenderer();
            ~VulkanRenderer();

            void start();
            void stop();
            void recreate_renderer();

            bool windows_match(SDL_Window* window);
            void handle_window_resize();

            void load_obj_with_texture(std::string obj_path, std::string texture_path);
        private:
            int m_max_frames_per_second = 90;
            std::atomic<bool> m_accepting_new_draw_call = false;
            std::atomic<bool> m_already_started = false;
            VulkanRendererData m_data;
            SDL_cond* m_finished;
            SDL_mutex* m_mutex;
            SDL_Thread* m_render_thread;
            SDL_Window* m_window;

            void sdl_create_window();
            void sdl_get_instance_extensions();
            void sdl_create_surface();
            void handle_sdl_bool(SDL_bool sdl_bool);
            void handle_sdl_int(int result);

            static int render_thread_handler(void* data);
        };
    }
}