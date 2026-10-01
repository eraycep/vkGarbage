struct VSInput {
    float3 Pos;
    float3 Normal;
	float2 UV;
};

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

[shader("vertex")]
float4 main(
    VSInput input,
    uniform ShaderData* shaderData,
    uint instanceIndex : SV_VulkanInstanceID) : SV_POSITION
{
    float4 worldPosition = mul(
        shaderData->model[instanceIndex],
        float4(input.Pos, 1.0));

    return mul(shaderData->lightViewProjection, worldPosition);
}