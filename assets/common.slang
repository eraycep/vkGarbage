module common;

public struct GPUObject {
    public float4x4 model;
    public float4x4 normalMatrix;
    public float4 color;
    public uint32_t materialIndex;
    public uint32_t padding[3];
};

public struct GPUMaterial {
    public float4 baseColor;
    public float4 specularShininess;
    public int32_t textureIndex;
    public uint32_t padding[3];
};

public struct FrameData {
    public float4x4 projection;
    public float4x4 view;
    public float4x4 lightViewProjection;
    public float4 cameraPosition;
    public float4 lightPos;
    public float4 lightColorIntensity;
    public float4 lightDirectionOuterCos;
    public float4 lightCone;
    public GPUObject* objectBuffer;
    public GPUMaterial* materialBuffer;
    public uint32_t objectCount;
    public uint32_t materialCount;
    public uint32_t selected;
    public uint32_t debugMode;
    public float shadowNearPlane;
    public float shadowFarPlane;
    public uint32_t padding[2];
};
