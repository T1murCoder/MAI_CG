#include "model.hpp"

#include <imgui.h>

using shader::ModelUniforms;

Model::Model(const Description& description)
	: name(description.name), mesh(description.mesh), transform(description.transform),
	  animation(description.animation), material(description.material) {}

void Model::update(double dt) {
	animation.update(dt);
}

// Матрица нормалей — обратная транспонированная к матрице модели: при неравномерном масштабе
// просто mat3(model) наклонила бы нормали, и они перестали бы быть перпендикулярны поверхности
ModelUniforms Model::uniforms() const {
	const glm::mat4 model = animation.apply(transform).matrix();
	return {
		.model = model,
		.normal_matrix = glm::transpose(glm::inverse(model)),
		.albedo = glm::vec4(material.albedo, 1.0f),
		.specular = glm::vec4(material.specular, material.shininess),
	};
}

// Меш привязывается заново для каждой модели — модели могут рисовать разные меши
void Model::draw(VkCommandBuffer command_buffer) const {
	mesh->bind(command_buffer);
	mesh->draw(command_buffer);
}

// PushID разделяет одинаковые подписи виджетов ("Position", "Animate", ...) разных моделей;
// ID берётся из имени, а не из адреса, чтобы состояние UI переживало переаллокацию вектора моделей
void Model::drawUi() {
	ImGui::PushID(name.c_str());
	if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
		transform.drawUi();
		animation.drawUi();
		material.drawUi();
	}
	ImGui::PopID();
}
