#pragma once

#include <string>

#include <vulkan/vulkan_core.h>

#include <glm/glm.hpp>

#include "animation.hpp"
#include "mesh.hpp"
#include "transform.hpp"
#include "vk_buffer.hpp"

// Объект сцены: меш (общий с другими моделями), положение, анимация, цвет
// и собственные uniform-буфер с набором дескрипторов
class Model {
public:
	// Начальные параметры модели; меш не принадлежит модели и должен жить дольше неё
	struct Description {
		std::string name; // должно быть уникальным в сцене — служит ID секции в UI
		const Mesh* mesh = nullptr;
		Transform transform;
		Animation animation;
		glm::vec4 color = glm::vec4(1.0f);
	};

	explicit Model(const Description& description);

	// Создаёт uniform-буфер модели и выделяет под него набор дескрипторов из pool по layout
	bool create(VkDescriptorSetLayout layout, VkDescriptorPool pool);

	// Уничтожает uniform-буфер; набор дескрипторов освобождается вместе с пулом
	void destroy();

	// Продвигает анимацию модели на dt секунд
	void update(double dt);

	// Записывает матрицы модели и камеры в uniform-буфер; вызывать только после ожидания fence кадра
	void uploadUniforms(const glm::mat4& view, const glm::mat4& proj);

	// Привязывает набор дескрипторов, цвет и меш модели и рисует её
	void draw(VkCommandBuffer command_buffer, VkPipelineLayout pipeline_layout) const;

	// Сворачиваемая секция с трансформацией, анимацией и цветом модели в текущем окне ImGui
	void drawUi();

private:
	std::string name;
	const Mesh* mesh;
	Transform transform;
	Animation animation;
	glm::vec4 color; // во фрагментном шейдере умножается на процедурный цвет вершин

	vk_buffer::Buffer uniform_buffer;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};
