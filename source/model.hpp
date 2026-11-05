#pragma once

#include <string>

#include <vulkan/vulkan_core.h>

#include <glm/glm.hpp>

#include "animation.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "shader_types.hpp"
#include "transform.hpp"

// Объект сцены: меш (общий с другими моделями), положение, анимация, материал
// GPU-данные и общий набор дескрипторов моделей принадлежат сцене
class Model {
public:
	// Начальные параметры модели; меш не принадлежит модели и должен жить дольше неё
	struct Description {
		std::string name; // должно быть уникальным в сцене — служит ID секции в UI
		const Mesh* mesh = nullptr;
		Transform transform;
		Animation animation;
		Material material;
	};

	explicit Model(const Description& description);

	// Продвигает анимацию модели на dt секунд
	void update(double dt);

	// Формирует матрицы и материал для записи сцены в общий dynamic UBO
	shader::ModelUniforms uniforms() const;

	// Привязывает меш и рисует его; сцена уже выбрала запись модели через dynamic offset в set 1
	void draw(VkCommandBuffer command_buffer) const;

	// Сворачиваемая секция с трансформацией, анимацией и материалом модели в текущем окне ImGui
	void drawUi();

private:
	std::string name;
	const Mesh* mesh;
	Transform transform;
	Animation animation;
	Material material;
};
