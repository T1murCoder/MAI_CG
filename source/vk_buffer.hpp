#pragma once

#include <cstddef>
#include <span>

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace vk_buffer {

// GPU-буфер, выделенный через VMA; mapped заполнен только для host-visible пути
struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
};

// Создаёт host-visible буфер размера size с флагами usage и мапит его в out.mapped
bool create(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out);
// Создаёт device-local буфер и загружает данные через staging. Синхронно ждёт GPU;
// вызывать при загрузке ресурсов, вне render pass, с эксклюзивным доступом к graphics queue
bool createDeviceLocal(std::span<const std::byte> data, VkBufferUsageFlags usage, Buffer& out);
// Делает CPU-записи видимыми GPU, включая non-coherent память; вызывать до отправки кадра
bool flush(const Buffer& buffer, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
// Уничтожает буфер и освобождает его память через VMA
void destroy(Buffer& buffer);

} // namespace vk_buffer
