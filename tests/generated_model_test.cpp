#include "linux_render_manager_3d.hpp"
#include "linux_render_manager_2d.hpp"
#include "vulkan_renderer_2d.hpp"
#include "ModelAsset.hpp"
#include "RuntimeModel.hpp"
#include "ModelSubmeshAsset.hpp"
#include "RuntimeSubmesh.hpp"
#include <iostream>
#include <stdexcept>

/// <summary>Exercises real generated model ownership, fixed GPU32 conversion and malformed input rejection on a validated native device.</summary>
int main() {
    try {
        helengine::linux::VulkanContext context({},true,VK_API_VERSION_1_2);
        helengine::linux::VulkanRenderer2D graphics(context,32,32);
        helengine::linux::LinuxRenderManager2D ui(graphics);
        helengine::linux::LinuxRenderManager3D scene(graphics,ui);
        ::ModelAsset asset; asset.set_Id("test:generated-model");
        Array<::float3> positions(3),normals(3); Array<::float2> coordinates(3); Array<uint16_t> indices(3);
        positions[0]=::float3(0.0f,0.0f,0.0f); positions[1]=::float3(1.0f,0.0f,0.0f); positions[2]=::float3(0.0f,1.0f,0.0f);
        for (int32_t index=0;index<3;++index) { normals[index]=::float3(0.0f,0.0f,1.0f); coordinates[index]=::float2(0.0f,0.0f); indices[index]=index; }
        asset.Positions=&positions; asset.Normals=&normals; asset.TexCoords=&coordinates; asset.Indices16=&indices;
        auto* model=scene.BuildModelFromRaw(&asset);
        if (!model || model->get_Id()!="test:generated-model" || !model->get_Submeshes() || model->get_Submeshes()->Length!=1 || (*model->get_Submeshes())[0]->get_IndexCount()!=3) { throw std::runtime_error("Native model did not preserve generated identity and resolved submesh range."); }
        scene.ReleaseModel(model);
        Array<::float3> malformedNormals(1); asset.Normals=&malformedNormals;
        bool rejected=false; try { scene.BuildModelFromRaw(&asset); } catch (const std::invalid_argument&) { rejected=true; }
        if (!rejected) { throw std::runtime_error("Mismatched model attributes were silently truncated."); }
        scene.Dispose(); ui.Dispose();
        if (context.GetValidationErrorCount()) { throw std::runtime_error("Generated model upload/release emitted Vulkan validation errors."); }
        std::cout << "PASS generated-model identity submesh GPU32 upload release malformed-attributes\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
      catch (const std::exception* error) { std::cerr << "FAIL generated " << (error ? error->what() : "null exception") << '\n'; delete error; return 1; }
}
