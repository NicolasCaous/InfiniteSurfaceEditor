#pragma once

#include <memory>
#include "SDL2/SDL.h"

#include "rendering/VulkanRenderer.h"
#include "util/SafeQueue.hpp"

namespace ise
{
    struct EventDataInjection
    {
        bool processing_events = true;
        bool trigger_quit = false;
        bool trigger_recreate_renderer = false;
        bool trigger_window_event = false;
        ise::util::SafeQueue<SDL_Event*> window_event_queue;
        ise::rendering::VulkanRenderer* renderer;
    };

    class EventSystem
    {
    private:
        SDL_Thread* m_event_processing_thread;
        int m_max_event_pumps_per_second = 2000;
        EventDataInjection m_event_data_injection {};
    public:
        EventSystem(ise::rendering::VulkanRenderer& renderer);
        ~EventSystem();

        static int SDL_event_watcher(void* data, SDL_Event* event);
        static int event_processing_thread_handler(void* data);
        void wait_until_quit();
    };
}