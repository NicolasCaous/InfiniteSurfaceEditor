#include "EventSystem.h"

#include <chrono>

#include "SDL2/SDL.h"

#include "rendering/WindowEvents.h"
#include "util/SafeQueue.hpp"

ise::EventSystem::EventSystem(ise::rendering::VulkanRenderer& renderer)
{
    this->m_event_data_injection.renderer = &renderer;

    this->m_event_processing_thread = SDL_CreateThread(ise::EventSystem::event_processing_thread_handler, "EventProcessingThread", &this->m_event_data_injection);

    SDL_AddEventWatch(ise::EventSystem::SDL_event_watcher, &this->m_event_data_injection);
}

ise::EventSystem::~EventSystem()
{
    this->m_event_data_injection.processing_events = false;
    SDL_WaitThread(m_event_processing_thread, NULL);

    SDL_DelEventWatch(ise::EventSystem::SDL_event_watcher, &this->m_event_data_injection);
}

int ise::EventSystem::SDL_event_watcher(void* data, SDL_Event* event)
{
    ise::EventDataInjection* injected_data = (ise::EventDataInjection*)data;

    if (event->type == SDL_QUIT)
    {
        injected_data->trigger_quit = true;
    }

    if (event->type == SDL_WINDOWEVENT)
    {
        injected_data->window_event_queue.push(event);
        injected_data->trigger_window_event = true;
    }

    if (event->type == SDL_KEYDOWN && event->key.keysym.scancode == SDL_SCANCODE_A)
    {
        injected_data->trigger_recreate_renderer = true;
        //SDL_SetWindowSize(renderer->m_data.window, 800, 800);
        //renderer.m_data.v_sync = true;
        //renderer.m_data.force_refresh = true;
    }

    /*if (event->type == SDL_KEYDOWN && event->key.keysym.scancode == SDL_SCANCODE_S)
    {
        //SDL_SetWindowSize(renderer->m_data.window, 400, 400);
        renderer.m_data.v_sync = false;
        renderer.m_data.force_refresh = true;
    }*/

    return 0;
}

int ise::EventSystem::event_processing_thread_handler(void* data)
{
    ise::EventDataInjection* injected_data = (ise::EventDataInjection*)data;

    while (injected_data->processing_events)
    {
        if (injected_data->trigger_window_event)
        {
            injected_data->trigger_window_event = false;
            std::vector<SDL_Event*> events = injected_data->window_event_queue.pop_all();

            for (SDL_Event* event : events)
            {
                ise::rendering::window_event_handler(*injected_data->renderer, event);
            }
        }
    }

    return 0;
}

void ise::EventSystem::wait_until_quit()
{
    while (true)
    {
        auto old_timestamp = std::chrono::high_resolution_clock::now();

        SDL_PumpEvents();

        // This is needed because the main thread needs to create the windows. Otherwise fun stuff happens in SDL
        if (this->m_event_data_injection.trigger_recreate_renderer)
        {
            this->m_event_data_injection.trigger_recreate_renderer = false;
            this->m_event_data_injection.renderer->recreate_renderer();
        }

        // This is needed to stop the main thread after SDL_Quit event
        if (this->m_event_data_injection.trigger_quit)
        {
            this->m_event_data_injection.renderer->stop();

            return;
        }

        auto new_timestamp = std::chrono::high_resolution_clock::now();
        auto elapsed_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(new_timestamp - old_timestamp).count();

        old_timestamp = new_timestamp;

        auto sleep_microseconds = (1000000 / this->m_max_event_pumps_per_second) - elapsed_microseconds;
        if (sleep_microseconds > 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_microseconds));
        }
    }
}