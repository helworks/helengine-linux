#include "vulkan_mesh_batch_geometry.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace helengine::vulkan;
/// <summary>Checks real selected-policy engine arithmetic, inverse-transpose normals, UV preservation and matrix snapshots.</summary>
int main() {
    try {

        auto world=::float4x4::get_Identity(); world.M11=2.0f; world.M22=4.0f; world.M33=8.0f; world.M41=3.0f; world.M42=5.0f; world.M43=7.0f;
        const VulkanMeshBatchGeometry geometry(world); const VulkanMeshRenderer::Vertex vertex{{1,2,3},{1,1,1},{0.25f,0.75f}}; const auto baked=geometry.Bake(vertex);
        if(baked.Position[0]!=5 || baked.Position[1]!=13 || baked.Position[2]!=31 || std::abs(baked.Normal[0]-0.5f)>0.001f || std::abs(baked.Normal[1]-0.25f)>0.001f || std::abs(baked.Normal[2]-0.125f)>0.001f || baked.Uv[0]!=0.25f || baked.Uv[1]!=0.75f) throw std::runtime_error("Engine matrix baking differs from standard world/normal contract.");
        auto rotated=::float4x4::get_Identity(); rotated.M11=0; rotated.M12=1; rotated.M21=-1; rotated.M22=0;
        const auto result=VulkanMeshBatchGeometry(rotated).Bake(vertex);
        if(result.Position[0]!=-2 || result.Position[1]!=1 || result.Normal[0]!=-1 || result.Normal[1]!=1) throw std::runtime_error("Baking lost asymmetric engine row-vector rotation.");
        const auto key=geometry.Key(); world.M41=9;
        if(key==VulkanMeshBatchGeometry(world).Key() || geometry.Bake(vertex).Position[0]!=5) throw std::runtime_error("Geometry retained mutable transform state.");
        static_assert(sizeof(VulkanMeshRenderer::Vertex)==32);
        std::cout << "PASS generated-engine batching position normal rotation immutable-transform GPU32\n"; return 0;
    } catch(const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
