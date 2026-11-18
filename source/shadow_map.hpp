#pragma once

#include <span>

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

class Mesh;

// Карта глубины с точки зрения света. create/destroy вызываются при простаивающем GPU,
// record — вне основного render pass, recordDebug — внутри него после record.
class ShadowMap {
public:
	static constexpr uint32_t kResolution = 2048;

	struct Draw {
		const Mesh* mesh;
		glm::mat4 model;
	};

	struct DepthBias {
		static constexpr float kMaxConstant = 16.0f;
		static constexpr float kMaxSlope = 8.0f;
		float constant = 2.0f;
		float slope = 1.5f;

		void sanitize();
	};

	ShadowMap() = default;
	ShadowMap(const ShadowMap&) = delete;
	ShadowMap& operator=(const ShadowMap&) = delete;

	bool create();
	void destroy();

	// Дескриптор для sampler2DShadow; перед чтением карты нужно вызвать record().
	VkDescriptorImageInfo comparisonDescriptor() const;

	// Записывает геометрию без материалов и переводит карту в состояние для чтения шейдером.
	void record(VkCommandBuffer command_buffer, const glm::mat4& light_view_projection,
				std::span<const Draw> draws, DepthBias bias);
	// Показывает карту квадратом по центру окна, не растягивая её при ресайзе.
	void recordDebug(VkCommandBuffer command_buffer, VkExtent2D window_extent) const;

private:
	bool createImage();
	bool createSamplers();
	bool createDebugDescriptors();
	bool createPipelines();

	VkFormat format = VK_FORMAT_UNDEFINED;
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSampler comparison_sampler = VK_NULL_HANDLE;
	VkSampler debug_sampler = VK_NULL_HANDLE;
	bool readable = false;

	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkDescriptorSetLayout debug_set_layout = VK_NULL_HANDLE;
	VkDescriptorPool debug_pool = VK_NULL_HANDLE;
	VkDescriptorSet debug_set = VK_NULL_HANDLE;
	VkPipelineLayout debug_pipeline_layout = VK_NULL_HANDLE;
	VkPipeline debug_pipeline = VK_NULL_HANDLE;
};
