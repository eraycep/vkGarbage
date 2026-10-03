module common;

public struct ShaderData {
    public float4x4 projection;
    public float4x4 view;
    public float4x4 model[4];
    public float4x4 normalMatrix[4];
    public float4 objectAppearance[4];
    public float4 objectSpecular[4];
    public float4 lightPos;
    public float4 lightColorIntensity;
    public float4 lightDirectionOuterCos;
    public float4x4 lightViewProjection;
    public float4 lightCone;
    public uint32_t selected;
    public uint32_t debugMode;
    public float shadowNearPlane;
    public float shadowFarPlane;
};
