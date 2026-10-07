import common;

[[vk::binding(0, 1)]] Sampler2D shadowMap;

struct PreviewVertex {
    float4 position : SV_POSITION;
    float2 uv;
};

[shader("vertex")]
PreviewVertex main(uint vertexIndex : SV_VertexID) {
    PreviewVertex output;
    // One oversized triangle covers the viewport without vertex buffers.
    float2 uv = float2((vertexIndex << 1) & 2, vertexIndex & 2);
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}

[shader("fragment")]
float4 main(PreviewVertex input, uniform FrameData* frameData) {
    float depth = shadowMap.SampleLevel(input.uv, 0.0).r;
    float nearPlane = frameData->shadowNearPlane;
    float farPlane = frameData->shadowFarPlane;
    float distance = nearPlane * farPlane /
        (farPlane - depth * (farPlane - nearPlane));
    float gray = saturate((distance - nearPlane) / (farPlane - nearPlane));
    return float4(gray, gray, gray, 1.0);
}
