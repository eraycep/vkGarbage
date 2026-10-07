import common;

struct VSInput {
    float3 Pos;
    float3 Normal;
	float2 UV;
};

[shader("vertex")]
float4 main(
    VSInput input,
    uniform FrameData* frameData,
    uint instanceIndex : SV_VulkanInstanceID) : SV_POSITION
{
    float4 worldPosition = mul(
        frameData->objectBuffer[instanceIndex].model,
        float4(input.Pos, 1.0));

    return mul(frameData->lightViewProjection, worldPosition);
}