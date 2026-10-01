#include "Scene.hpp"
#include "Light.hpp"

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

void Scene::rotateCamera(float x, float y)
{
    yaw += x;
    pitch += y;

    if (pitch > 89.0f) {
        pitch = 89.0f;
    }
    if (pitch < -89.0f) {
        pitch = -89.0f;
    }

    glm::vec3 front;
	front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
	front.y = sin(glm::radians(pitch));
	front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront_ = glm::normalize(front);

    glm::vec3 right = glm::cross(front, worldUp_);
    cameraUp_ = glm::cross(right, front);
}

void Scene::moveCamera(float forwardDistance)
{
    moveCamera(glm::vec3{0.0f, 0.0f, forwardDistance});
}

void Scene::moveCamera(glm::vec3 delta)
{
    const glm::vec3 right = glm::normalize(glm::cross(cameraFront_, worldUp_));
    const glm::vec3 up = glm::normalize(glm::cross(right, cameraFront_));
    cameraPosition_ += right * delta.x + up * delta.y + cameraFront_ * delta.z;
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
    data.view = glm::lookAt(cameraPosition_, cameraPosition_ + cameraFront_, cameraUp_);
    // data.view = glm::translate(glm::mat4(1.0f), cameraPosition_);
    for (std::uint32_t i = 0; i < objectCount; ++i) {
        data.model[i] = glm::translate(glm::mat4(1.0f), objectPositions_[i]) *
            glm::mat4_cast(glm::quat(objectRotations_[i]));
    }
    // Lighting uses view space; the editable light stays in world space.
    data.lightPos = data.view * glm::vec4(lights_[0].position, 1.0f);
    const glm::vec3 lightDirection = glm::normalize(
        glm::mat3(data.view) * lights_[0].direction);
    data.lightDirectionOuterCos = glm::vec4(lightDirection,
        glm::cos(glm::radians(lights_[0].outerConeDegrees)));
    // The shadow matrix transforms world-space positions, independently of the camera.
    const auto& light = lights_[0];
    const glm::vec3 worldLightDirection = glm::normalize(light.direction);
    const glm::vec3 lightUp = glm::abs(glm::dot(worldLightDirection, worldUp_)) > 0.99f
        ? glm::vec3{0.0f, 0.0f, 1.0f} : worldUp_;
    const glm::mat4 lightView = glm::lookAt(
        light.position, light.position + worldLightDirection, lightUp);
    const glm::mat4 lightProjection = glm::perspective(
        glm::radians(light.outerConeDegrees * 2.0f), 1.0f,
        light.shadowNearPlane, light.shadowFarPlane);
    data.lightViewProjection = lightProjection * lightView;
    data.lightCone = glm::vec4(glm::cos(glm::radians(lights_[0].innerConeDegrees)),
        0.0f, 0.0f, 0.0f);
    
    data.lightColorIntensity = glm::vec4(lights_[0].color, lights_[0].intensity);
    data.selected = selectedObject_;
    return data;
}
