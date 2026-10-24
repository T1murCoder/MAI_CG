#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

struct GLFWwindow;

namespace graphics::internal {

// Число выборок MSAA основного render pass при запуске: сглаживает «лесенку» на рёбрах геометрии.
// 4× поддерживается любым устройством Vulkan — спецификация требует его и для цветовых, и для depth-привязок
constexpr VkSampleCountFlagBits kDefaultSampleCount = VK_SAMPLE_COUNT_4_BIT;

struct Context {
	VkPhysicalDevice physical_device;
	VkDevice device;

	VmaAllocator allocator;

	VkQueue graphics_queue;
	uint32_t graphics_queue_index;

	VkFormat swapchain_format;
	VkExtent2D swapchain_extent;

	VkRenderPass render_pass;
	VkSampleCountFlagBits sample_count; // выборок MSAA в render_pass; с ним же создаются пайплайны под него
};

struct FrameData {
	VkFramebuffer framebuffer;
	VkCommandBuffer command_buffer;
};

extern Context context;

bool initialize(GLFWwindow* const window);
void shutdown();

void resize(uint32_t width, uint32_t height);

// Числа выборок MSAA, которые устройство поддерживает и для цвета, и для глубины (маска VkSampleCountFlagBits)
VkSampleCountFlags supportedSampleCounts();

// Пересоздаёт основной render pass, его мультисемпловые буферы и framebuffer'ы под samples выборок.
// Дожидается простоя GPU; вызывать вне кадра (не между prepare и submitAndPresent). Пайплайны,
// созданные под старый render pass, после этого нужно пересоздать
bool setSampleCount(VkSampleCountFlagBits samples);

// Ждёт предыдущий кадр и получает картинку swapchain. Пустой FrameData (command_buffer == VK_NULL_HANDLE) —
// кадр рисовать нельзя (например, окно свёрнуто): render и submitAndPresent для него не вызываются
FrameData prepare();
void submitAndPresent();

} // namespace graphics::internal