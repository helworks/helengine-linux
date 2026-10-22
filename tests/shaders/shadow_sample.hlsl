cbuffer ProbeBuffer : register(b1) { float4 ProbeDirectionAndAtlasU; };
Texture2D ShadowAtlas : register(t1);
TextureCube ShadowCube : register(t2);
SamplerState AtlasSampler : register(s1);
SamplerState CubeSampler : register(s2);
float4 PSMain(float2 uv : TEXCOORD0) : SV_Target {
    float atlas=ShadowAtlas.SampleLevel(AtlasSampler,float2(abs(ProbeDirectionAndAtlasU.w),ProbeDirectionAndAtlasU.w>=0 ? 0.25f : 0.75f),0).r;
    float cube=ShadowCube.SampleLevel(CubeSampler,ProbeDirectionAndAtlasU.xyz,0).r;
    return float4(atlas,cube,uv.x*0,1);
}
