#include "VulkanRenderer.h"

#include <iostream>

#include "VkBootstrap.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_Vulkan.h"

#ifdef _WIN32
    #define VK_USE_PLATFORM_WIN32_KHR
    #define PLATFORM_SURFACE_EXTENSION_NAME VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif

bool ise_initRenderer()
{
    SDL_Init(SDL_INIT_EVERYTHING);
    SDL_Vulkan_LoadLibrary(nullptr);
    SDL_Window* window = SDL_CreateWindow("Example Vulkan Application", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);

    unsigned int extensionCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr);
    std::vector<const char*> extensionNames(extensionCount);
    SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensionNames.data());

    vkb::InstanceBuilder builder;
    auto inst_builder = builder.set_app_name("Example Vulkan Application")
        .request_validation_layers()
        .use_default_debug_messenger();

    for (auto extensionName : extensionNames)
    {
        inst_builder.enable_extension(extensionName);
    }

    auto inst_ret = inst_builder.build();
    if (!inst_ret) {
        std::cerr << "Failed to create Vulkan instance. Error: " << inst_ret.error().message() << "\n";
        return false;
    }
    vkb::Instance vkb_inst = inst_ret.value();

    VkSurfaceKHR surface;
    SDL_Vulkan_CreateSurface(window, vkb_inst, &surface);

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    auto phys_ret = selector.set_surface(surface)
        .set_minimum_version(1, 1) // require a vulkan 1.1 capable device
        .require_dedicated_transfer_queue()
        .select();
    if (!phys_ret) {
        std::cerr << "Failed to select Vulkan Physical Device. Error: " << phys_ret.error().message() << "\n";
        return false;
    }

    vkb::DeviceBuilder device_builder{ phys_ret.value() };
    // automatically propagate needed data from instance & physical device
    auto dev_ret = device_builder.build();
    if (!dev_ret) {
        std::cerr << "Failed to create Vulkan device. Error: " << dev_ret.error().message() << "\n";
        return false;
    }
    vkb::Device vkb_device = dev_ret.value();

    // Get the VkDevice handle used in the rest of a vulkan application
    VkDevice device = vkb_device.device;

    // Get the graphics queue with a helper function
    auto graphics_queue_ret = vkb_device.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret) {
        std::cerr << "Failed to get graphics queue. Error: " << graphics_queue_ret.error().message() << "\n";
        return false;
    }
    VkQueue graphics_queue = graphics_queue_ret.value();

    bool running = true;
    SDL_Event Event;

    while (running) {
        while (SDL_PollEvent(&Event)) {
            if (Event.type == SDL_QUIT) {
                running = false;
            }
        }
    }

    SDL_Quit();

    // Turned 400-500 lines of boilerplate into less than fifty.
    return true;
}
