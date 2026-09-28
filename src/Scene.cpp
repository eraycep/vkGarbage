#include "Scene.hpp"

Scene::Scene()
{
    for (std::uint32_t i = 0; i < objectCount; ++i) {
        objectPositions_[i] = { (static_cast<float>(i) - 1.0f) * 3.0f, 0.0f, 0.0f };
    }
}

void Scene::rotateSelected(const glm::vec2& deltaRadians)
{
    objectRotations_[selectedObject_].x += deltaRadians.x;
    objectRotations_[selectedObject_].y += deltaRadians.y;
}

void Scene::moveCamera(float deltaZ)
{
    cameraPosition_.z += deltaZ;
}

void Scene::selectNext()
{
    selectedObject_ = (selectedObject_ + 1) % objectCount;
}

void Scene::selectPrevious()
{
    selectedObject_ = (selectedObject_ + objectCount - 1) % objectCount;
}

std::uint32_t Scene::selectedObject() const
{
    return selectedObject_;
}

glm::vec3 Scene::cameraPosition() const
{
    return cameraPosition_;
}

Scene::ShaderData Scene::shaderData(float aspectRatio) const
{
    ShaderData data{};
    data.projection = glm::perspective(glm::radians(fieldOfViewDegrees_), aspectRatio, nearPlane_, farPlane_);
    data.view = glm::translate(glm::mat4(1.0f), cameraPosition_);
    for (std::uint32_t i = 0; i < objectCount; ++i) {
        data.model[i] = glm::translate(glm::mat4(1.0f), objectPositions_[i]) *
            glm::mat4_cast(glm::quat(objectRotations_[i]));
    }
    data.lightPos = lightPosition_;
    data.selected = selectedObject_;
    return data;
}
