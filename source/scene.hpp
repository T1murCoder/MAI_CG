#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "camera.hpp"
#include "mesh.hpp"
#include "model.hpp"

// Сцена: камера, меши, модели и общие для них Vulkan-ресурсы (дескрипторы, pipeline)
class Scene {
public:
	// Создаёт меши, дескрипторы, модели и pipeline
	bool create();

	// Уничтожает все ресурсы сцены в порядке, обратном созданию; GPU уже должен простаивать
	void destroy();

	// Продвигает анимации всех моделей на dt секунд
	void update(double dt);

	// Окно ImGui с настройками камеры и всех моделей
	void drawUi();

	// Обновляет uniform-буферы и записывает команды отрисовки всех моделей;
	// вызывать внутри render pass после ожидания fence кадра
	void record(VkCommandBuffer command_buffer, float aspect);

private:
	// Сколько моделей можно создать — под это число рассчитан пул дескрипторов
	static constexpr uint32_t kMaxModels = 32;

	bool createDescriptors();
	bool createModels();
	bool createPipeline();

	Camera camera;

	// Геометрия куба, общая для всех моделей сцены
	Mesh cube_mesh;

	// У каждой модели своя трансформация, анимация, цвет, uniform-буфер и набор дескрипторов
	std::vector<Model> models;

	// Один descriptor set layout на все модели и пул, из которого выделяются их наборы дескрипторов
	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	// Pipeline и его layout общие для всех моделей (различаются только дескрипторы и push constant)
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
};
