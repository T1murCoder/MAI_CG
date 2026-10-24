#pragma once

#include <string>

#include <vulkan/vulkan_core.h>

#include <glm/glm.hpp>

#include "animation.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "transform.hpp"
#include "vk_buffer.hpp"

// Объект сцены: меш (общий с другими моделями), положение, анимация, материал
// и собственные uniform-буфер с набором дескрипторов (set 1 в шейдерах)
class Model {
public:
	// Начальные параметры модели; меш не принадлежит модели и должен жить дольше неё
	struct Description {
		std::string name; // должно быть уникальным в сцене — служит ID секции в UI
		const Mesh* mesh = nullptr;
		Transform transform;
		Animation animation;
		Material material;
		bool cast_shadows = true;
	};

	explicit Model(const Description& description);

	// Создаёт uniform-буфер модели и выделяет под него набор дескрипторов из pool по layout модели
	bool create(VkDescriptorSetLayout model_set_layout, VkDescriptorPool pool);

	// Уничтожает uniform-буфер; набор дескрипторов освобождается вместе с пулом
	void destroy();

	// Продвигает анимацию модели на dt секунд
	void update(double dt);

	// Записывает матрицы модели и нормалей и материал в uniform-буфер; вызывать только после ожидания fence кадра
	void uploadUniforms();

	// Привязывает набор дескрипторов модели (set 1) и её меш и рисует её
	void draw(VkCommandBuffer command_buffer, VkPipelineLayout pipeline_layout) const;

	// Сворачиваемая секция с трансформацией, анимацией, материалом и флагом теней модели в текущем окне ImGui
	void drawUi();

	// Попадает ли модель в карты теней (отбрасывает ли тени); тени на неё падают в любом случае
	bool castsShadows() const;

private:
	std::string name;
	const Mesh* mesh;
	Transform transform;
	Animation animation;
	Material material;
	bool cast_shadows;

	vk_buffer::Buffer uniform_buffer;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};
