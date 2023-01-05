#pragma once

#include <SDL2/SDL.h>

#include "VulkanRenderer.h"

namespace ise
{
    namespace rendering
    {
        void window_event_handler(VulkanRenderer& renderer, SDL_Event* event);
    }
}