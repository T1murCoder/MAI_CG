#pragma once

#include <span>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

// PNG-текстура RGBA8, один mip-уровень, линейная фильтрация и повторение.
// Владеет image, allocation, view и sampler; наборы дескрипторов только ссылаются на них.
class Texture {
public:
	enum class ColorSpace { Srgb, Linear };

	Texture() = default;
	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	// Декодирует PNG и синхронно загружает на GPU вне render pass при создании сцены.
	// При ошибке сообщает путь файла и освобождает частично созданные ресурсы.
	// Albedo/emissive — Srgb, specular (данные) — Linear.
	bool create(const char* path, ColorSpace color_space = ColorSpace::Srgb);
	// Вызывать после завершения GPU и освобождения использующих текстуру наборов.
	void destroy();

	VkDescriptorImageInfo descriptor() const;

private:
	bool createImage(uint32_t width, uint32_t height);
	bool upload(std::span<const unsigned char> pixels, uint32_t width, uint32_t height);
	bool createViewAndSampler();

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
};
