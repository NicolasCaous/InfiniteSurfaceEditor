#include "WindowEvents.h"

#include "SDL2/SDL.h"

#include "VulkanRenderer.h"

void ise::rendering::window_event_handler(VulkanRenderer& renderer, SDL_Event* event)
{
    if (event->window.event == SDL_WINDOWEVENT_RESIZED)
    {
        SDL_Window* win = SDL_GetWindowFromID(event->window.windowID);

        if (renderer.windows_match(win)) {
            renderer.handle_window_resize();
        }
    }
}