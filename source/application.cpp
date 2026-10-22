#include "application.hpp"

#include <iostream>
#include <vector>

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <imgui.h>

#include "camera.hpp"
#include "mesh.hpp"
#include "model.hpp"
#include "pipeline.hpp"

namespace application {

namespace {

// Геометрия куба, общая для всех моделей сцены
Mesh cube_mesh;

// Камера, общая для всех моделей: даёт матрицы вида и проекции
Camera camera;

double last_update_time = -1.0; // -1 означает "ещё не было предыдущего кадра"

// Модели сцены; у каждой своя трансформация, анимация, цвет, uniform-буфер и набор дескрипторов
std::vector<Model> models;

// Один descriptor set layout на все модели и пул, из которого выделяются их наборы дескрипторов
VkDescriptorSetLayout descriptor_set_layout;
VkDescriptorPool descriptor_pool;

// Pipeline и его layout общие для всех моделей (различаются только дескрипторы и push constant)
VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

// Создаёт layout и пул дескрипторов; сами наборы выделяют модели в Model::create
bool createDescriptors() {
	auto& context = graphics::internal::context;

	// Привязка 0: один uniform-буфер, используется в вершинном шейдере
	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr,
									&descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layout\n";
		return false;
	}

	// Пул с запасом: по одному набору с одним uniform-буфером на модель
	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 4,
	};

	const VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 4,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	return true;
}

// Создаёт модели сцены и их GPU-ресурсы; меш и дескрипторы уже должны быть созданы
bool createModels() {
	// Первый куб: трансформация и анимация по орбите управляются из UI
	models.emplace_back(Model::Description{
		.name = "Cube",
		.mesh = &cube_mesh,
	});

	// Второй куб: стоит в точке (2, 0, 0) и постоянно вращается вокруг Y со скоростью 30°/с без орбиты
	models.emplace_back(Model::Description{
		.name = "Second Cube",
		.mesh = &cube_mesh,
		.transform = { .position = glm::vec3(2.0f, 0.0f, 0.0f) },
		.animation = Animation(true, 0.0f, 0.0f, glm::vec3(0.0f, 30.0f, 0.0f)),
		.color = glm::vec4(1.0f, 0.55f, 0.15f, 1.0f),
	});

	for (Model& model : models) {
		if (!model.create(descriptor_set_layout, descriptor_pool)) {
			return false;
		}
	}

	return true;
}

// Создаёт pipeline layout (набор дескрипторов + push constant с цветом) и сам graphics pipeline
bool createPipeline() {
	// Push constant с цветом модели (vec4), доступен фрагментному шейдеру
	const VkPushConstantRange push_constant_range = {
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(glm::vec4),
	};

	if (!pipeline::createLayout({ &descriptor_set_layout, 1 }, { &push_constant_range, 1 },
								pipeline_layout)) {
		return false;
	}

	return pipeline::create({
		.vertex_shader = "cube.vert.spv",
		.fragment_shader = "cube.frag.spv",
		.layout = pipeline_layout,
	}, pipeline);
}

} // namespace

// Инициализация ресурсов сцены: меш, дескрипторы, модели, pipeline
bool initialize() {
	if (!cube_mesh.create(geometry::makeCube())) {
		return false;
	}

	if (!createDescriptors()) {
		return false;
	}

	if (!createModels()) {
		return false;
	}

	if (!createPipeline()) {
		return false;
	}

	return true;
}

// Освобождает все Vulkan-ресурсы приложения перед завершением работы
void shutdown() {
	auto& context = graphics::internal::context;
	// Дожидаемся завершения всех операций на очереди, прежде чем уничтожать используемые ими ресурсы
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);

	for (Model& model : models) {
		model.destroy();
	}

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);

	cube_mesh.destroy();
}

// Обновляет состояние анимации и рисует все элементы управления ImGui
void update(double time) {
	// Дельта времени между кадрами; на самом первом кадре считаем её нулевой
	const double dt = (last_update_time >= 0.0) ? (time - last_update_time) : 0.0;
	last_update_time = time;

	for (Model& model : models) {
		model.update(dt);
	}

	ImGui::Begin("Cube Controls");

	camera.drawUi();

	for (Model& model : models) {
		model.drawUi();
	}

	ImGui::End();

	ImGui::ShowDemoWindow();
}

// Записывает команды отрисовки кадра: обновляет uniform-буферы и рисует все модели в одном render pass
void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	// Соотношение сторон окна нужно живым каждый кадр — окно может быть изменено в размере
	const float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);

	const glm::mat4 view = camera.view();
	const glm::mat4 proj = camera.projection(aspect);

	// Запись в uniform-буферы безопасна: prepare() уже дождался завершения предыдущего кадра
	for (Model& model : models) {
		model.uploadUniforms(view, proj);
	}

	// Command buffer нужно начинать и завершать заново каждый кадр — фреймворк это не делает сам
	const VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	vkBeginCommandBuffer(fd.command_buffer, &begin_info);

	// Цвет очистки фона и значение очистки буфера глубины (1.0 — максимально далеко)
	const VkClearValue clear_values[] = {
		{ .color = { { 0.02f, 0.02f, 0.03f, 1.0f } } },
		{ .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// Viewport и scissor заданы динамическими в pipeline, поэтому задаём их здесь на каждый кадр
	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

	const VkRect2D scissor = { .extent = context.swapchain_extent };
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	for (const Model& model : models) {
		model.draw(fd.command_buffer, pipeline_layout);
	}

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
