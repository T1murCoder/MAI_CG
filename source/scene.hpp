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
// В шейдерах set 0 — данные сцены (общие для всех моделей), set 1 — данные конкретной модели
class Scene {
public:
	// Создаёт меши, дескрипторы, модели и pipeline
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
	// Сколько моделей можно создать — под это число рассчитан пул дескрипторов
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

	// У каждой модели своя трансформация, анимация, материал, uniform-буфер и набор дескрипторов
	std::vector<Model> models;

	// Layout набора сцены (set 0), layout наборов моделей (set 1) и пул, из которого выделяются все наборы
	VkDescriptorSetLayout scene_set_layout = VK_NULL_HANDLE;
	VkDescriptorSetLayout model_set_layout = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	// Uniform-буфер с общими данными сцены и его набор дескрипторов (set 0)
	vk_buffer::Buffer scene_uniform_buffer;
	VkDescriptorSet scene_descriptor_set = VK_NULL_HANDLE;

	// Один pipeline layout на все пайплайны сцены: set 0, set 1 и push constant визуализаций источников.
	// Общий layout совместим сам с собой, поэтому набор сцены остаётся привязанным при смене пайплайна.
	// Pipeline моделей общий для всех моделей (различаются только наборы дескрипторов set 1)
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
};
