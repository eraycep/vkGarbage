#include "Scene.hpp"
#include "Light.hpp"

Scene::Scene()
{
    objects_ = {{
        {.position = {-3, 0, 0}, .textureIndex = 0},
        {.position = { 0, 0, 0}, .textureIndex = 1},
        {.position = { 3, 0, 0}, .textureIndex = 2},
        {.position = {0, 1.5f, 0}, .scale = {8, 1, 8},
         .mesh = MeshId::Plane, .color = {0.65f, 0.65f, 0.65f}, .selectable = false}
    }};
}

void Scene::rotateSelected(const glm::vec2& deltaRadians)
{
    objects_[selectedObject_].rotation.x += deltaRadians.x;
    objects_[selectedObject_].rotation.y += deltaRadians.y;
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
    for (std::uint32_t step = 1; step <= objectCount; ++step) {
        const auto next = (selectedObject_ + step) % objectCount;
        if (objects_[next].selectable) { selectedObject_ = next; break; }
    }
}

void Scene::selectPrevious()
{
    for (std::uint32_t step = 1; step <= objectCount; ++step) {
        const auto next = (selectedObject_ + objectCount - step) % objectCount;
        if (objects_[next].selectable) { selectedObject_ = next; break; }
    }
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
        const auto& object = objects_[i];
        data.model[i] = glm::translate(glm::mat4(1.0f), object.position) *
            glm::mat4_cast(glm::quat(object.rotation)) *
            glm::scale(glm::mat4(1.0f), object.scale);
        data.normalMatrix[i] = glm::transpose(glm::inverse(data.view * data.model[i]));
        data.objectAppearance[i] = glm::vec4(object.color, static_cast<float>(object.textureIndex));
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
