#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include "vk_buffer.hpp"

// Массив элементов одинакового размера в storage-буфере, привязанном к binding набора дескрипторов.
// Ёмкость растёт удвоением: буфер пересоздаётся, дескриптор переписывается, поэтому в шейдере
// не нужно задавать максимальное число элементов
class StorageArray {
public:
	// Создаёт буфер начальной ёмкости для элементов размером size и привязывает его к set_binding набора set
	bool create(VkDeviceSize size, VkDescriptorSet set, uint32_t set_binding);
	void destroy();

	// Копирует count элементов в буфер, при необходимости увеличивая его. Пересоздание буфера и
	// перезапись дескриптора безопасны, только пока set не используется GPU и не привязан в текущем
	// command buffer: вызывать после ожидания fence кадра и до vkCmdBindDescriptorSets.
	// Возвращает, сколько элементов загружено: меньше count, если не удалось выделить память
	uint32_t upload(const void* elements, uint32_t count);

private:
	// Создаёт буфер на new_capacity элементов и переводит на него дескриптор; старый буфер
	// уничтожается только после успешного создания нового
	bool reallocate(uint32_t new_capacity);

	static constexpr uint32_t kInitialCapacity = 4;

	VkDeviceSize element_size = 0;
	uint32_t capacity = 0;

	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	uint32_t binding = 0;

	vk_buffer::Buffer buffer = {};
};
