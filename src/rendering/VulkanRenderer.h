#pragma once

#include "VulkanRendererUgly.h"
#include "SDL2/SDL.h"

namespace ise
{
    namespace rendering
    {
        class VulkanRenderer
        {
        private:
            int m_max_frames_per_second = 90;
            std::atomic<bool> m_accepting_new_draw_call = false;
            std::atomic<bool> m_already_started = false;
            VulkanRendererData m_data;
            SDL_cond* m_finished;
            SDL_mutex* m_mutex;
            SDL_Thread* m_render_thread;
        public:
            VulkanRenderer();
            ~VulkanRenderer();

            static int render_thread_handler(void* data);
            void start();
            void stop();
            void recreate_renderer();

            bool windows_match(SDL_Window* window);
            void handle_window_resize();
        };
    }
}