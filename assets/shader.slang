/* Copyright (c) 2025-2026, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

import common;

struct VSInput {
    float3 Pos;
    float3 Normal;
	float2 UV;
};

[[vk::binding(0, 0)]] Sampler2D textures[];
[[vk::binding(0, 1)]] Sampler2D shadowMap;

struct VSOutput {
    float4 Pos : SV_POSITION;
    float3 Normal;
    float2 UV;
    float3 Factor;
    float3 LightVec;
    float4 LightClipPosition;
    float3 ViewVec;
    uint32_t InstanceIndex;
};

[shader("vertex")]
VSOutput main(VSInput input, uniform ShaderData *shaderData, uint instanceIndex : SV_VulkanInstanceID) {
    VSOutput output;
    float4x4 modelMat = shaderData->model[instanceIndex];
    output.Normal = mul((float3x3)shaderData->normalMatrix[instanceIndex], input.Normal);
    output.UV = input.UV;
    output.Pos = mul(shaderData->projection, mul(shaderData->view, mul(modelMat, float4(input.Pos.xyz, 1.0))));
    output.Factor = (shaderData->selected == instanceIndex ? 3.0f : 1.0f);
    output.InstanceIndex = instanceIndex;
    // Calculate view vectors required for lighting
    float4 fragPos = mul(mul(shaderData->view, modelMat), float4(input.Pos.xyz, 1.0));
    output.LightVec = shaderData->lightPos.xyz - fragPos.xyz;
    float4 worldPosition = mul(shaderData->model[instanceIndex], float4(input.Pos, 1.0));
    output.LightClipPosition = mul(shaderData->lightViewProjection, worldPosition);
    output.ViewVec = -fragPos.xyz;
    return output;
}

// The shadow pass uses a positive-height viewport and zero-to-one depth.
float shadowVisibility(float4 lightClipPosition, float normalDotLight) {
    if (lightClipPosition.w <= 0.0) {
        return 0.0;
    }
    float3 ndc = lightClipPosition.xyz / lightClipPosition.w;
    if (any(ndc.xy < -1.0) || any(ndc.xy > 1.0) || ndc.z < 0.0 || ndc.z > 1.0) {
        return 0.0;
    }
    float2 uv = ndc.xy * 0.5 + 0.5;
    // Tune for the light's depth range; larger bias can detach shadows.
    float bias = max(0.0005 * (1.0 - normalDotLight), 0.00005);
    float storedDepth = shadowMap.SampleLevel(uv, 0.0).r;
    return ndc.z - bias <= storedDepth ? 1.0 : 0.0;
}

[shader("fragment")]
float4 main(VSOutput input, uniform ShaderData *shaderData) {
    // Phong lighting
    float3 N = normalize(input.Normal);
    if (shaderData->debugMode == 1) {
        // View-space normals: map [-1, 1] into RGB [0, 1].
        return float4(N * 0.5 + 0.5, 1.0);
    }
    float3 L = input.LightVec / max(length(input.LightVec), 0.0001);
    float3 V = normalize(input.ViewVec);
    float3 R = reflect(-L, N);
    float diffuse = max(dot(N, L), 0.0);
    float visibility = shadowVisibility(input.LightClipPosition, diffuse);
    if (shaderData->debugMode == 2) {
        // Outside the light frustum is also black, matching shadowVisibility().
        return float4(visibility, visibility, visibility, 1.0);
    }
    float4 material = shaderData->objectSpecular[input.InstanceIndex];
    float3 specular = diffuse > 0.0
        ? material.rgb * pow(max(dot(R, V), 0.0), material.w)
        : float3(0.0);
    // -L points from the light toward this surface.
    float coneCos = dot(-L, normalize(shaderData->lightDirectionOuterCos.xyz));
    float cone = smoothstep(shaderData->lightDirectionOuterCos.w,
                            shaderData->lightCone.x, coneCos);
    float3 lightColor = shaderData->lightColorIntensity.rgb * shaderData->lightColorIntensity.w;
    float4 appearance = shaderData->objectAppearance[input.InstanceIndex];
    float3 color = appearance.rgb * input.Factor;
    int textureIndex = int(appearance.w);
    if (textureIndex >= 0) {
        color *= textures[NonUniformResourceIndex(textureIndex)].Sample(input.UV).rgb;
    }
    float3 ambient = 0.025 * color;
    float3 direct = (diffuse * color + specular) * lightColor * cone;
    return float4(ambient + visibility * direct, 1.0);
}
