cbuffer TransformBuffer : register(b0) { row_major float4x4 WorldViewProjection; };
cbuffer ColorBuffer : register(b1) { float4 Tint; };
Texture2D DiffuseTexture : register(t0);
SamplerState DiffuseSampler : register(s0);
struct VertexInput { float3 Position : POSITION; float3 Normal : NORMAL; float2 Uv : TEXCOORD0; };
struct VertexOutput { float4 Position : SV_Position; float2 Uv : TEXCOORD0; };
VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.Position=mul(float4(input.Position+input.Normal*0.0000001,1),WorldViewProjection);
    output.Uv=input.Uv;
    return output;
}
float4 PSMain(VertexOutput input) : SV_Target { return DiffuseTexture.Sample(DiffuseSampler,input.Uv)*Tint; }
