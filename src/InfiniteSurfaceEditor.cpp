#include "InfiniteSurfaceEditor.h"

#include <chrono>
#include <iostream>

#include "rendering/VulkanRenderer.h"
#include "EventSystem.h"

int main()
{
    SDL_Init(SDL_INIT_EVERYTHING);

    ise::rendering::VulkanRenderer renderer;
    ise::EventSystem event_system(renderer);

    renderer.start();

    event_system.wait_until_quit();

    SDL_Quit();

    return 0;
}
