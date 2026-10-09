#include "Scene.hpp"
#include "Light.hpp"
#include "Assets.hpp"
#include <limits>
#include <cmath>

Scene::Scene(const Assets& assets)
{
    const auto& handles = assets.defaultSceneAssets();
    for (std::uint32_t i = 0; i < handles.monkeyMaterials.size(); ++i) {
        renderObjects_.push_back({handles.suzanne, handles.monkeyMaterials[i],
            {.position = {(static_cast<float>(i) - 1.0f) * 3.0f, 0, 0}}});
    }
    renderObjects_.push_back({handles.plane, handles.floorMaterial,
        {.position = {0, 1.5f, 0}, .scale = {8, 1, 8}}, false});
}

void Scene::rotateSelected(const glm::vec2& deltaRadians)
{
    renderObjects_[selectedObject_].transform.rotation.x += deltaRadians.x;
    renderObjects_[selectedObject_].transform.rotation.y += deltaRadians.y;
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
    for (std::uint32_t step = 1; step <= renderObjects_.size(); ++step) {
        const auto next = (selectedObject_ + step) % renderObjects_.size();
        if (renderObjects_[next].selectable) { selectedObject_ = next; break; }
    }
}

void Scene::selectPrevious()
{
    for (std::uint32_t step = 1; step <= renderObjects_.size(); ++step) {
        const auto next = (selectedObject_ + renderObjects_.size() - step) % renderObjects_.size();
        if (renderObjects_[next].selectable) { selectedObject_ = next; break; }
    }
}

std::optional<uint32_t> Scene::pickObject(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const Assets& assets) const
{
    std::optional<uint32_t> closest;
    float closestDistance = std::numeric_limits<float>::infinity();
    if (glm::dot(rayDirection, rayDirection) == 0.0f) return std::nullopt;
    for (uint32_t i = 0; i < renderObjects_.size(); ++i) {
        const auto& object = renderObjects_[i];
        if (!object.selectable || object.transform.scale.x == 0.0f ||
            object.transform.scale.y == 0.0f || object.transform.scale.z == 0.0f) continue;
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), object.transform.position) *
            glm::mat4_cast(glm::quat(object.transform.rotation)) *
            glm::scale(glm::mat4(1.0f), object.transform.scale);
        const glm::mat4 inverseModel = glm::inverse(model);
        const glm::vec3 localOrigin(inverseModel * glm::vec4(rayOrigin, 1.0f));
        // Do not normalize: preserve the world ray parameter across different scales.
        const glm::vec3 localDirection(inverseModel * glm::vec4(rayDirection, 0.0f));
        const auto& bounds = assets.mesh(object.mesh).range.boundingBox;
        const auto hit = bounds.intersectRay(localOrigin, localDirection);
        if (hit && *hit < closestDistance) {
            closestDistance = *hit;
            closest = i;
        }
    }
    return closest;
}

void Scene::selectObject(uint32_t index)
{
    if (index < renderObjects_.size() && renderObjects_[index].selectable) selectedObject_ = index;
}

std::uint32_t Scene::selectedObject() const
{
    return selectedObject_;
}

glm::vec3 Scene::cameraPosition() const
{
    return cameraPosition_;
}

FrameData Scene::frameData(float aspectRatio) const
{
    FrameData data{};

    data.projection = glm::perspective(glm::radians(fieldOfViewDegrees_), aspectRatio, nearPlane_, farPlane_);
    data.view = glm::lookAt(cameraPosition_, cameraPosition_ + cameraFront_, cameraUp_);

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
    data.cameraPosition = glm::vec4(cameraPosition_, 1.0f);
    data.lightCone = glm::vec4(glm::cos(glm::radians(lights_[0].innerConeDegrees)),
        0.0f, 0.0f, 0.0f);
    
    data.lightColorIntensity = glm::vec4(lights_[0].color, lights_[0].intensity);
    data.shadowNearPlane = light.shadowNearPlane;
    data.shadowFarPlane = light.shadowFarPlane;
    data.selected = selectedObject_;
    data.debugMode = static_cast<std::uint32_t>(debugMode_);
    return data;
}

std::vector<GPUObject> Scene::gpuObjects(const glm::mat4& view) const
{
    std::vector<GPUObject> gpuObjects;
    gpuObjects.reserve(renderObjects_.size());
    for (uint32_t i = 0; i < renderObjects_.size(); i++) {
        const auto& object = renderObjects_[i];
        GPUObject gpuObject;

        gpuObject.model = object.transform.matrix();
        gpuObject.normalMatrix = glm::transpose(glm::inverse(view * gpuObject.model));
        gpuObject.materialIndex = object.material;
        gpuObject.color = glm::vec4(1.0f);
        gpuObjects.push_back(gpuObject);
    }

    return gpuObjects;
}

std::vector<GPUMaterial> Scene::gpuMaterials(const Assets& assets) const
{
    std::vector<GPUMaterial> gpuMaterials;
    gpuMaterials.reserve(assets.materials().size());

    for (uint32_t i = 0; i < assets.materials().size(); i++) {
        const auto& material = assets.material(i);
        GPUMaterial gpuMaterial;

        gpuMaterial.baseColor = material.baseColorFactor;
        gpuMaterial.specularShininess = glm::vec4(
            material.specularColor * material.specularStrength,
            material.shininess
        );
        gpuMaterial.textureIndex = material.baseColorTexture == invalidHandle
                    ? -1
                    : static_cast<int32_t>(material.baseColorTexture); 

        gpuMaterials.push_back(gpuMaterial);
    }

    return gpuMaterials;
}
