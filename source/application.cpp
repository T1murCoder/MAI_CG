#include "application.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include "mesh.hpp"
#include "vk_buffer.hpp"

namespace application {

namespace {

// Данные, передаваемые в вершинный шейдер через uniform-буфер
struct UBO {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

// Геометрия куба (общая для обоих объектов) и два независимых uniform-буфера - по одному на куб
vk_buffer::Buffer vertex_buffer;
vk_buffer::Buffer index_buffer;
vk_buffer::Buffer uniform_buffer;
vk_buffer::Buffer uniform_buffer_2;

// Режим проекции первого куба: true — перспективная, false — ортографическая
bool use_perspective_projection = true;

// Ручные параметры трансформации первого куба, задаются слайдерами в UI
glm::vec3 transform_position = glm::vec3(0.0f);
glm::vec3 transform_rotation_degrees = glm::vec3(0.0f);
glm::vec3 transform_scale = glm::vec3(1.0f);

// Состояние анимации по траектории: включена/на паузе/радиус/скорости орбиты и вращения
bool animate_enabled = false;
bool animate_paused = false;
float trajectory_radius = 2.0f;
double last_update_time = -1.0; // -1 означает "ещё не было предыдущего кадра"

// Независимые накопленные углы — орбита и вращение по каждой оси не связаны друг с другом
double orbit_angle_radians = 0.0;
double spin_angle_x_degrees = 0.0; // горизонтальная ось (наклон вперёд/назад)
double spin_angle_y_degrees = 0.0; // вертикальная ось
double spin_angle_z_degrees = 0.0; // горизонтальная ось (наклон вбок)

float orbit_speed = 1.0f; // радиан в секунду; регулируется в UI ("Orbit Speed")

// Скорости вращения по каждой оси, градусов в секунду; регулируются в UI ("Rotation Speed X/Y/Z")
float spin_speed_x_degrees_per_sec = 0.0f;
float spin_speed_y_degrees_per_sec = 90.0f;
float spin_speed_z_degrees_per_sec = 0.0f;

// Цвет из UI (ColorEdit4); во фрагментном шейдере умножается на процедурный цвет вершин
glm::vec4 base_color = glm::vec4(1.0f);

// Строит матрицу модели первого куба: из траектории анимации либо из ручных UI-слайдеров
glm::mat4 computeModelMatrix() {
	glm::vec3 position = transform_position;
	glm::vec3 rotation_degrees = transform_rotation_degrees;

	// Пока анимация включена, позиция/поворот управляются траекторией, а не слайдерами
	if (animate_enabled) {
		position = glm::vec3(trajectory_radius * float(std::cos(orbit_angle_radians)), 0.0f,
							 trajectory_radius * float(std::sin(orbit_angle_radians)));
		rotation_degrees = glm::vec3(float(spin_angle_x_degrees), float(spin_angle_y_degrees),
									 float(spin_angle_z_degrees));
	}

	// Порядок TRS: перенос, повороты по X/Y/Z, масштаб
	glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
	model = glm::rotate(model, glm::radians(rotation_degrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::rotate(model, glm::radians(rotation_degrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
	model = glm::rotate(model, glm::radians(rotation_degrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
	model = glm::scale(model, transform_scale);
	return model;
}

// Один descriptor set layout на двоих и два независимых набора дескрипторов (по кубу на набор)
VkDescriptorSetLayout descriptor_set_layout;
VkDescriptorPool descriptor_pool;
VkDescriptorSet descriptor_set;
VkDescriptorSet descriptor_set_2;

// Второй куб: фиксированное смещение, независимое автономное вращение, фиксированный цвет —
// никак не связан с UI-состоянием (трансформацией/цветом/анимацией) первого куба
const glm::vec3 kSecondCubeOffset = glm::vec3(2.0f, 0.0f, 0.0f);
const glm::vec4 kSecondCubeColor = glm::vec4(1.0f, 0.55f, 0.15f, 1.0f);
constexpr float kSecondCubeSpinDegreesPerSecond = 30.0f;

// Pipeline и его layout общие для обоих кубов (различаются только дескрипторы и push constant)
VkPipelineLayout pipeline_layout;
VkPipeline pipeline;

// Читает скомпилированный SPIR-V файл и создаёт из него VkShaderModule
VkShaderModule loadShaderModule(const char* filename) {
	const std::string path = std::string(SHADER_DIR) + filename;

	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		std::cerr << "Failed to open shader file: " << path << '\n';
		return VK_NULL_HANDLE;
	}

	// Открыли файл с позицией в конце (ios::ate), поэтому tellg() сразу даёт его размер
	const size_t size = size_t(file.tellg());
	std::vector<char> code(size);
	file.seekg(0);
	file.read(code.data(), std::streamsize(size));
	file.close();

	const VkShaderModuleCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = code.size(),
		.pCode = reinterpret_cast<const uint32_t*>(code.data()),
	};

	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(graphics::internal::context.device, &create_info, nullptr,
							 &module) != VK_SUCCESS) {
		std::cerr << "Failed to create shader module: " << path << '\n';
		return VK_NULL_HANDLE;
	}

	return module;
}

// Создаёт вершинный, индексный и два uniform-буфера, сразу копируя в них данные куба
bool createBuffers() {
	// Вершинный и индексный буферы общие для обоих кубов — геометрия не дублируется
	const VkDeviceSize vertex_size = sizeof(mesh::cube_vertices[0]) * mesh::cube_vertices.size();
	if (!vk_buffer::create(vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer)) {
		return false;
	}
	std::memcpy(vertex_buffer.mapped, mesh::cube_vertices.data(), size_t(vertex_size));

	const VkDeviceSize index_size = sizeof(mesh::cube_indices[0]) * mesh::cube_indices.size();
	if (!vk_buffer::create(index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer)) {
		return false;
	}
	std::memcpy(index_buffer.mapped, mesh::cube_indices.data(), size_t(index_size));

	// Два uniform-буфера под MVP — по одному на каждый из двух кубов сцены
	if (!vk_buffer::create(sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer)) {
		return false;
	}

	if (!vk_buffer::create(sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer_2)) {
		return false;
	}

	return true;
}

// Создаёт layout, пул и два набора дескрипторов — по одному на каждый uniform-буфер/куб
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

	// Пул рассчитан на 2 набора дескрипторов — по числу кубов на сцене
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

	// Выделяем оба набора дескрипторов одним вызовом из одного layout
	const VkDescriptorSetLayout set_layouts[2] = { descriptor_set_layout, descriptor_set_layout };
	VkDescriptorSet sets[2] = {};

	const VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 2,
		.pSetLayouts = set_layouts,
	};

	if (vkAllocateDescriptorSets(context.device, &alloc_info, sets) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor sets\n";
		return false;
	}

	descriptor_set = sets[0];
	descriptor_set_2 = sets[1];

	// Привязываем каждый набор дескрипторов к своему uniform-буферу
	const VkDescriptorBufferInfo buffer_infos[2] = {
		{ .buffer = uniform_buffer.buffer, .offset = 0, .range = sizeof(UBO) },
		{ .buffer = uniform_buffer_2.buffer, .offset = 0, .range = sizeof(UBO) },
	};

	const VkWriteDescriptorSet writes[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_infos[0],
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptor_set_2,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_infos[1],
		},
	};

	vkUpdateDescriptorSets(context.device, 2, writes, 0, nullptr);

	return true;
}

// Создаёт graphics pipeline: шейдеры, формат вершин, растеризация, блендинг, layout
bool createPipeline() {
	auto& context = graphics::internal::context;

	const VkShaderModule vertex_shader = loadShaderModule("cube.vert.spv");
	const VkShaderModule fragment_shader = loadShaderModule("cube.frag.spv");
	if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) {
		return false;
	}

	// Две стадии шейдеров: вершинная и фрагментная
	const VkPipelineShaderStageCreateInfo stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader,
			.pName = "main",
		},
	};

	const VkVertexInputBindingDescription binding = {
		.binding = 0,
		.stride = sizeof(mesh::Vertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	// Формат вершины для пайплайна: позиция (location 0) и цвет (location 1)
	const VkVertexInputAttributeDescription attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(mesh::Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(mesh::Vertex, color),
		},
	};

	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding,
		.vertexAttributeDescriptionCount = sizeof(attributes) / sizeof(attributes[0]),
		.pVertexAttributeDescriptions = attributes,
	};

	// Список треугольников (каждые 3 индекса — отдельный треугольник)
	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	// Сами значения viewport/scissor не заданы здесь — они динамические (см. dynamic_state ниже)
	const VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	// Отсечение задних граней; передние грани — с обходом против часовой стрелки (проверено эмпирически)
	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	// Мультисемплинг отключён — один сэмпл на пиксель
	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	// Тест и запись глубины включены — нужны для правильной отрисовки объёмного куба
	const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	// Блендинг отключён, пишем во все 4 канала (RGBA)
	const VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment,
	};

	// Viewport и scissor можно будет менять каждый кадр без пересоздания pipeline (важно при ресайзе окна)
	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
		.pDynamicStates = dynamic_states,
	};

	// Push constant с цветом из UI (vec4), доступен фрагментному шейдеру
	const VkPushConstantRange push_constant_range = {
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(glm::vec4),
	};

	const VkPipelineLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptor_set_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constant_range,
	};

	if (vkCreatePipelineLayout(context.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan pipeline layout\n";
		return false;
	}

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = sizeof(stages) / sizeof(stages[0]),
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blend,
		.pDynamicState = &dynamic_state,
		.layout = pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	const VkResult result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
													   nullptr, &pipeline);

	// Шейдерные модули больше не нужны после создания pipeline — сразу их уничтожаем
	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	if (result != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan graphics pipeline\n";
		return false;
	}

	return true;
}

} // namespace

// Инициализация ресурсов сцены: буферы геометрии/uniform, дескрипторы, pipeline
bool initialize() {
	if (!createBuffers()) {
		return false;
	}

	if (!createDescriptors()) {
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

	vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);

	vk_buffer::destroy(uniform_buffer);
	vk_buffer::destroy(uniform_buffer_2);
	vk_buffer::destroy(vertex_buffer);
	vk_buffer::destroy(index_buffer);
}

// Обновляет состояние анимации и рисует все элементы управления ImGui
void update(double time) {
	// Дельта времени между кадрами; на самом первом кадре считаем её нулевой
	const double dt = (last_update_time >= 0.0) ? (time - last_update_time) : 0.0;
	last_update_time = time;

	// Орбита и вращение по каждой из трёх осей продвигаются независимо, только пока анимация включена и не на паузе
	if (animate_enabled && !animate_paused) {
		orbit_angle_radians += dt * double(orbit_speed);
		spin_angle_x_degrees += dt * double(spin_speed_x_degrees_per_sec);
		spin_angle_y_degrees += dt * double(spin_speed_y_degrees_per_sec);
		spin_angle_z_degrees += dt * double(spin_speed_z_degrees_per_sec);
	}

	ImGui::Begin("Cube Controls");

	// Переключатель между перспективной и ортографической проекцией
	ImGui::SeparatorText("Projection");
	if (ImGui::RadioButton("Perspective", use_perspective_projection)) {
		use_perspective_projection = true;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Orthographic", !use_perspective_projection)) {
		use_perspective_projection = false;
	}

	// Слайдеры позиции/поворота отключены во время анимации по траектории — ей отдан контроль
	ImGui::SeparatorText("Transform");
	ImGui::BeginDisabled(animate_enabled);
	ImGui::SliderFloat3("Position", &transform_position.x, -3.0f, 3.0f);
	ImGui::SliderFloat3("Rotation", &transform_rotation_degrees.x, -180.0f, 180.0f);
	ImGui::EndDisabled();
	ImGui::SliderFloat3("Scale", &transform_scale.x, 0.1f, 3.0f);

	// Кнопка паузы активна только при включённой анимации; замораживает elapsed_anim_time на месте
	ImGui::SeparatorText("Animation");
	ImGui::Checkbox("Animate", &animate_enabled);
	ImGui::BeginDisabled(!animate_enabled);
	ImGui::SameLine();
	if (ImGui::Button(animate_paused ? "Play" : "Pause")) {
		animate_paused = !animate_paused;
	}
	ImGui::EndDisabled();
	ImGui::SliderFloat("Orbit Speed", &orbit_speed, 0.0f, 5.0f);
	ImGui::SliderFloat("Radius", &trajectory_radius, 0.1f, 5.0f);
	ImGui::SliderFloat("Rotation Speed X", &spin_speed_x_degrees_per_sec, -360.0f, 360.0f);
	ImGui::SliderFloat("Rotation Speed Y", &spin_speed_y_degrees_per_sec, -360.0f, 360.0f);
	ImGui::SliderFloat("Rotation Speed Z", &spin_speed_z_degrees_per_sec, -360.0f, 360.0f);

	// Цвет, который во фрагментном шейдере умножается на процедурный цвет вершин
	ImGui::SeparatorText("Color");
	ImGui::ColorEdit4("Cube Color", &base_color.x);

	ImGui::End();

	ImGui::ShowDemoWindow();
}

// Записывает команды отрисовки кадра: считает матрицы и рисует оба куба в одном render pass
void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	// Соотношение сторон окна нужно живым каждый кадр — окно может быть изменено в размере
	const float aspect = float(context.swapchain_extent.width) / float(context.swapchain_extent.height);

	UBO ubo{};
	ubo.model = computeModelMatrix();
	ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

	if (use_perspective_projection) {
		ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
	} else {
		// half_extent подобран так, чтобы куб был примерно того же размера, что и в перспективе
		constexpr float half_extent = 1.7f;
		ubo.proj = glm::ortho(-half_extent * aspect, half_extent * aspect, -half_extent, half_extent,
							  0.1f, 100.0f);
	}
	// В Vulkan clip space ось Y направлена вниз, в отличие от OpenGL, под который заточен GLM
	ubo.proj[1][1] *= -1.0f;

	// Копируем MVP первого куба напрямую в замапленную память его uniform-буфера
	std::memcpy(uniform_buffer.mapped, &ubo, sizeof(ubo));

	// Второй куб: независимая трансформация — фиксированное смещение плюс автономное вращение по времени
	UBO ubo2{};
	ubo2.model = glm::rotate(glm::translate(glm::mat4(1.0f), kSecondCubeOffset),
							 glm::radians(float(last_update_time) * kSecondCubeSpinDegreesPerSecond),
							 glm::vec3(0.0f, 1.0f, 0.0f));
	ubo2.view = ubo.view;
	ubo2.proj = ubo.proj;

	std::memcpy(uniform_buffer_2.mapped, &ubo2, sizeof(ubo2));

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

	// Первый куб: свой набор дескрипторов, цвет из UI, общие вершинный/индексный буферы
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
							&descriptor_set, 0, nullptr);

	vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					   sizeof(glm::vec4), &base_color);

	const VkBuffer vertex_buffers[] = { vertex_buffer.buffer };
	const VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, vertex_buffers, offsets);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);

	vkCmdDrawIndexed(fd.command_buffer, uint32_t(mesh::cube_indices.size()), 1, 0, 0, 0);

	// Второй куб: другой набор дескрипторов и фиксированный цвет; геометрия та же, буферы уже привязаны
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
							&descriptor_set_2, 0, nullptr);

	vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
					   sizeof(glm::vec4), &kSecondCubeColor);

	vkCmdDrawIndexed(fd.command_buffer, uint32_t(mesh::cube_indices.size()), 1, 0, 0, 0);

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application
