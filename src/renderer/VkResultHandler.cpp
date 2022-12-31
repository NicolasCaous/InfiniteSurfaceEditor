#include "VkResultHandler.h"

#include <iostream>
#include <format>

void ise_handleVkResult(VkResult result)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(std::format("VkResult validation failed with code {0}", (int) result));
    }
}