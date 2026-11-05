#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "camera.hpp"
#include "light_gizmos.hpp"
#include "lighting.hpp"
#include "mesh.hpp"
#include "model.hpp"
#include "vk_buffer.hpp"

// Сцена: камера, источники света, меши, модели и общие для них Vulkan-ресурсы (дескрипторы, pipeline).
// В шейдерах set 0 — данные сцены, set 1 — материал и общий dynamic UBO моделей
class Scene {
public:
	// Создаёт меши, дескрипторы (вместе со storage-буферами источников света), модели,
	// pipeline моделей и пайплайны визуализации источников
	bool create();

	// Уничтожает все ресурсы сцены в порядке, обратном созданию; GPU уже должен простаивать
	void destroy();

	// Обрабатывает ввод для камеры и продвигает анимации источников света и моделей на dt секунд
	void update(double dt);

	// Окно ImGui с настройками камеры, освещения, визуализации источников и всех моделей
	void drawUi();

	// Обновляет uniform-буферы и записывает команды отрисовки всех моделей и визуализаций источников;
	// вызывать внутри render pass после ожидания fence кадра
	void record(VkCommandBuffer command_buffer, float aspect);

private:
	// Верхняя граница числа моделей сцены
	static constexpr uint32_t kMaxModels = 32;

	bool createDescriptors();
	bool createModels();
	bool createPipeline();

	Camera camera;
	Lighting lighting;
	LightGizmos light_gizmos;

	// Геометрия, общая для всех моделей сцены: радужный куб из ЛР1, белый куб (цвет только
	// от материала) и плоскость (пол и стены)
	Mesh cube_mesh;
	Mesh white_cube_mesh;
	Mesh plane_mesh;
	MaterialCatalog materials;

	// У каждой модели своя трансформация, анимация и материал; GPU-записи хранятся вместе
	std::vector<Model> models;
	double albedo_effect_time = 0.0; // CPU-часы эффекта; время и включение идут только через push constants

	// Layout и пул набора сцены (set 0); set 1 принадлежит каталогу материалов
	VkDescriptorSetLayout scene_set_layout = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	// Uniform-буфер с общими данными сцены и его набор дескрипторов (set 0)
	vk_buffer::Buffer scene_uniform_buffer;
	VkDescriptorSet scene_descriptor_set = VK_NULL_HANDLE;

	// Общий dynamic UBO моделей: запись i начинается по адресу i * model_uniform_stride.
	// Диапазон дескриптора — одна ModelUniforms, stride учитывает выравнивание GPU.
	vk_buffer::Buffer model_uniform_buffer;
	VkDeviceSize model_uniform_stride = 0;

	// Один pipeline layout: set 0, set 1 и общий push constant диапазон для gizmos и эффекта albedo.
	// Общий layout совместим сам с собой, поэтому набор сцены остаётся привязанным при смене пайплайна.
	// Pipeline моделей (lit.*) общий — различаются набор материала и dynamic offset модели
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
};
