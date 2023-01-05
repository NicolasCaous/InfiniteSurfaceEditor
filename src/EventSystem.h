#pragma once

#include <memory>
#include <SDL2/SDL.h>

#include "rendering/VulkanRenderer.h"
#include "util/SafeQueue.hpp"

namespace ise
{
    struct EventDataInjection
    {
        int max_event_pumps_per_second = 2000;
        bool processing_events = true;
        bool trigger_quit = false;
        bool trigger_recreate_renderer = false;
        bool trigger_window_event = false;
        ise::util::SafeQueue<SDL_Event*> window_event_queue;
        ise::rendering::VulkanRenderer* renderer;
    };

    class EventSystem
    {
    public:
        EventSystem(ise::rendering::VulkanRenderer& renderer);
        ~EventSystem();

        void wait_until_quit();

        static int sdl_event_watcher(void* data, SDL_Event* event);
        static int event_processing_thread_handler(void* data);
    private:
        SDL_Thread* m_event_processing_thread;
        EventDataInjection m_event_data_injection {};
    };
}