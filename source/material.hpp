#pragma once

#include <array>

#include <glm/glm.hpp>

#include "texture.hpp"

// Стабильные индексы готовых материалов; параметры конкретной модели хранятся отдельно.
enum class MaterialId { UvGrid, Brick, Tiles, MaskedMetal, Count };

// Параметры материала по модели Блинн-Фонга
struct MaterialParameters {
	glm::vec3 albedo = glm::vec3(1.0f);   // линейный множитель albedo-текстуры (белый сохраняет PNG)
	glm::vec3 specular = glm::vec3(0.5f); // цвет блика Ks
	float shininess = 32.0f;              // коэффициент блеска α: чем больше, тем меньше и резче блик
	glm::vec2 uv_repeat = glm::vec2(1.0f); // независимые повторы U/V для всех карт выбранного материала
	bool specular_enabled = true;        // выключение обнуляет вклад блика, сохраняя параметры

	// Тонирование albedo, повторы U/V и параметры блика в текущем окне ImGui
	void drawUi();
};

// Общие изображения и один неизменяемый set 1 на материал: dynamic UBO + три карты.
// Создаётся вне render pass; UBO принадлежит сцене и должен жить дольше наборов каталога.
class MaterialCatalog {
public:
	bool create(const VkDescriptorBufferInfo& model_buffer);
	// GPU уже простаивает; сначала освобождаются наборы, затем изображения.
	void destroy();

	VkDescriptorSetLayout layout() const { return set_layout; }
	VkDescriptorSet descriptor(MaterialId id) const;
	void drawSelectionUi(MaterialId& selected) const;

private:
	static constexpr size_t kMaterialCount = static_cast<size_t>(MaterialId::Count);
	struct Resource {
		Texture albedo;
		Texture specular;
		Texture emissive;
		VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	};

	bool createTextures();
	bool createDescriptors(const VkDescriptorBufferInfo& model_buffer);

	std::array<Resource, kMaterialCount> resources;
	Texture black_texture; // общий линейный чёрный 1×1 для отсутствующих specular/emissive
	VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
};
