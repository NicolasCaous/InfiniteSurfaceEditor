#pragma once

#include "VulkanRendererUgly.h"
#include "SDL2/SDL.h"
#undef main

namespace ise
{
    namespace rendering
    {
        class VulkanRenderer
        {
        private:
            int m_max_frames_per_second = 90;
            int m_max_ticks_per_second = 2000;
            bool m_accepting_new_draw_call = true;
            bool m_already_started = false;
            VulkanRendererData m_data;
            SDL_cond* m_finished;
            SDL_mutex* m_mutex;
            SDL_Thread* m_render_thread;
        public:
            VulkanRenderer();
            ~VulkanRenderer();

            static int render_thread_handler(void* data);
            static int SDL_event_watcher(void* data, SDL_Event* event);
            void start();
            void stop();
            void wait_until_stop();
        };
    }
}