#pragma once

#include <functional>

#include <vulkan/vulkan_core.h>

namespace vk_upload {

// Записывает и отправляет разовые команды в graphics queue, ждёт fence и освобождает
// временный command pool. Ресурсы команд принадлежат вызывающему коду и живут до возврата.
// Только для загрузки ресурсов вне render pass, с эксклюзивным доступом к очереди.
bool submit(const std::function<void(VkCommandBuffer)>& record);

} // namespace vk_upload
