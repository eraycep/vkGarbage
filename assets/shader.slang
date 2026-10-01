/* Copyright (c) 2025-2026, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

struct VSInput {
    float3 Pos;
    float3 Normal;
	float2 UV;
};

[[vk::binding(0, 0)]] Sampler2D textures[];
[[vk::binding(0, 1)]] Sampler2D shadowMap;

struct ShaderData {
    float4x4 projection;
    float4x4 view;
    float4x4 model[4];
    float4 lightPos;
    float4 lightColorIntensity;
    float4 lightDirectionOuterCos;
    float4x4 lightViewProjection;
    float4 lightCone;
    uint32_t selected;
};

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
    output.Normal = mul((float3x3)mul(shaderData->view, modelMat), input.Normal);
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
    float3 L = input.LightVec / max(length(input.LightVec), 0.0001);
    float3 V = normalize(input.ViewVec);
    float3 R = reflect(-L, N);
    float diffuse = max(dot(N, L), 0.0);
    float specular = diffuse > 0.0 ? pow(max(dot(R, V), 0.0), 16.0) * 0.75 : 0.0;
    // -L points from the light toward this surface.
    float coneCos = dot(-L, normalize(shaderData->lightDirectionOuterCos.xyz));
    float cone = smoothstep(shaderData->lightDirectionOuterCos.w,
                            shaderData->lightCone.x, coneCos);
    float3 lightColor = shaderData->lightColorIntensity.rgb * shaderData->lightColorIntensity.w;
    // Sample from texture
    // The floor has no texture descriptor; use a neutral shadow receiver.
    float3 color;
    if (input.InstanceIndex == 3) {
        color = float3(0.65, 0.65, 0.65);
    } else {
        color = textures[NonUniformResourceIndex(input.InstanceIndex)].Sample(input.UV).rgb * input.Factor;
    }
    float3 ambient = 0.025 * color;
    float3 direct = (diffuse * color + specular) * lightColor * cone;
    float visibility = shadowVisibility(input.LightClipPosition, diffuse);
    return float4(ambient + visibility * direct, 1.0);
}
