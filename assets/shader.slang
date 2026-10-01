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

Sampler2D textures[];

struct ShaderData {
    float4x4 projection;
    float4x4 view;
    float4x4 model[3];
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
    float3 color = textures[NonUniformResourceIndex(input.InstanceIndex)].Sample(input.UV).rgb * input.Factor;
    float3 ambient = 0.025 * color;
    float3 direct = (diffuse * color + specular) * lightColor * cone;
    return float4(ambient + direct, 1.0);
}
