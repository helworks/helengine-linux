using helengine;
using helengine.baseplatform.Requests;
using helengine.files;
using helengine.linux.builder;

/// <summary>Deserializes cooked material bytes to verify fixed GPU32 defaults and authored texture/color identities independently of configurable numeric widths.</summary>
internal static class MaterialCookContractTests {
    /// <summary>Checks all advertised policies retain the same required emissive payload and explicit authored material resources.</summary>
    public static void Run() {
        foreach (int bits in new[] {16,32,64}) {
            Dictionary<string,string> fields = new() { ["shader-asset-id"]="shader",["vertex-program"]="VS",["pixel-program"]="PS",["variant"]="default",["normal-texture-id"]="normal-authored",["emissive-texture-id"]="emissive-authored" };
            var request = new PlatformMaterialCookRequest("material","material.hemat","linux","debug","vulkan","standard-shader",fields,new NumericPrecisionPolicy(bits,bits));
            ShaderMaterialAsset material = (ShaderMaterialAsset)AssetSerializer.DeserializeFromBytes(LinuxMaterialCooker.Cook(request).CookedMaterialBytes);
            byte[] emissive = material.ConstantBuffers.Single(buffer => buffer.Name=="EmissiveColorBuffer").Data;
            if (emissive.Length!=16 || BitConverter.ToSingle(emissive,0)!=1f || BitConverter.ToSingle(emissive,4)!=1f || BitConverter.ToSingle(emissive,8)!=1f || BitConverter.ToSingle(emissive,12)!=0f) { throw new InvalidOperationException("Linux cooked emissive default must preserve white tint and zero strength in the fixed GPU32 float4 contract."); }
            if (material.NormalTextureAssetId!="normal-authored" || material.EmissiveTextureAssetId!="emissive-authored") { throw new InvalidOperationException("Linux material cooking discarded authored normal/emissive texture identities."); }
            fields["emissive-color"]="#20408080";
            material = (ShaderMaterialAsset)AssetSerializer.DeserializeFromBytes(LinuxMaterialCooker.Cook(request).CookedMaterialBytes);
            emissive = material.ConstantBuffers.Single(buffer => buffer.Name=="EmissiveColorBuffer").Data;
            if (Math.Abs(BitConverter.ToSingle(emissive,0)-32f/255f)>0.002f || Math.Abs(BitConverter.ToSingle(emissive,12)-128f/255f)>0.002f) { throw new InvalidOperationException("Linux material cooking lost authored emissive tint/strength."); }
        }
        Console.WriteLine("PASS Linux cooked material emissive-default authored-color resource-identities fixed-GPU32 policies16/32/64");
    }
}
