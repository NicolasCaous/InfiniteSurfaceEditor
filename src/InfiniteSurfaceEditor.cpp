#include "InfiniteSurfaceEditor.h"

#include <chrono>
#include <iostream>

#include "renderer/VulkanRenderer.h"

using namespace std;

int main()
{
    ise::rendering::VulkanRenderer renderer;

    renderer.start();
    renderer.wait_until_stop();

    return 0;
}
