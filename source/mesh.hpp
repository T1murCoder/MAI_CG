#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include "geometry.hpp"
#include "vk_buffer.hpp"

// Геометрия меша в памяти GPU: вершинный и индексный буферы; один Mesh могут рисовать несколько моделей
class Mesh {
public:
	// Создаёт вершинный и индексный буферы и копирует в них данные
	bool create(const geometry::MeshData& data);
	void destroy();

	// Привязывает вершинный и индексный буферы к command buffer
	void bind(VkCommandBuffer command_buffer) const;

	// Рисует все треугольники меша одним экземпляром; буферы должны быть привязаны через bind()
	void draw(VkCommandBuffer command_buffer) const;

private:
	vk_buffer::Buffer vertex_buffer;
	vk_buffer::Buffer index_buffer;
	uint32_t index_count = 0;
};
