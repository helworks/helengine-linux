#include "vulkan_mesh_batcher.hpp"
#include "vulkan_mesh_batch_geometry.hpp"
#include "vulkan_renderer_2d.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace helengine::vulkan;

/// <summary>Loads actual HLSL fixture binaries compiled by the standard native CMake shader target.</summary>
static std::vector<uint32_t> ReadShader(const std::string& path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if(!stream || stream.tellg()<20 || stream.tellg()%4) throw std::runtime_error("Missing compiled batching shader fixture.");
    std::vector<uint32_t> words(static_cast<size_t>(stream.tellg())/4); stream.seekg(0);
    if(!stream.read(reinterpret_cast<char*>(words.data()),static_cast<std::streamsize>(words.size()*4))) throw std::runtime_error("Truncated batching shader fixture.");
    return words;
}
/// <summary>Owns two genuinely different input meshes and compares whole numeric GPU readbacks across every feature mode.</summary>
class Fixture final {
public:
    /// <summary>Creates a validated actual Vulkan device without opening a desktop window.</summary>
    Fixture():Context({},true,VK_API_VERSION_1_2),Output(Context,64,64),Renderer(Context) {
        const std::array<helengine::vulkan::VulkanMeshRenderer::BindingLayout,4> layout={{{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,64},{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_FRAGMENT_BIT,16},{100,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,VK_SHADER_STAGE_FRAGMENT_BIT,0},{200,VK_DESCRIPTOR_TYPE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,0}}};
        Material=Renderer.UploadMaterial(ReadShader(std::string(HELENGINE_VULKAN_SHADER_ROOT)+"/mesh.vert.spv"),"VSMain",ReadShader(std::string(HELENGINE_VULKAN_SHADER_ROOT)+"/mesh.frag.spv"),"PSMain",layout,{0,0,true,true});
        const uint8_t texel[]={32,128,224,255}; Texture=Output.CreateTexture(1,1,texel);
        A=std::make_shared<helengine::vulkan::VulkanMeshBatcher::Geometry>(); B=std::make_shared<helengine::vulkan::VulkanMeshBatcher::Geometry>();
        A->Vertices={{{-0.5f,-0.5f,0.25f},{0,0,1},{0,0}},{{0.5f,-0.5f,0.25f},{0,0,1},{1,0}},{{0,0.5f,0.25f},{0,0,1},{0.5f,1}}}; A->Indices={0,1,2};
        B->Vertices={{{-0.25f,0.5f,0.5f},{0,0,1},{0,1}},{{0.25f,0.5f,0.5f},{0,0,1},{1,1}},{{0,-0.5f,0.5f},{0,0,1},{0.5f,0}}}; B->Indices={2,0,1};
        MeshA=Renderer.UploadMesh(A->Vertices,A->Indices); MeshB=Renderer.UploadMesh(B->Vertices,B->Indices);
    }
    /// <summary>Releases every fixture-owned graphics handle after synchronous use.</summary>
    ~Fixture() { Renderer.DiscardFrame(); Renderer.ReleaseMesh(MeshA); Renderer.ReleaseMesh(MeshB); Renderer.ReleaseMaterial(Material); Output.ReleaseTexture(Texture); }
    /// <summary>Runs two current-transform frames, static cache reuse and all independent settings combinations.</summary>
    void Run() {
        std::vector<uint8_t> firstBaseline,secondBaseline;
        for(int mode=0;mode<4;++mode) {
            helengine::vulkan::VulkanMeshBatcher batcher({bool(mode&1),1024,bool(mode&2),3,1024},{
                [this](const auto& geometry) { return Renderer.UploadMesh(geometry.Vertices,geometry.Indices); },
                [this](size_t mesh,const auto& geometry) { return Renderer.UpdateDynamicMesh(mesh,geometry.Vertices,geometry.Indices); },
                [this](size_t mesh) { Renderer.ReleaseMesh(mesh); }});
            for(int frame=0;frame<3;++frame) {
                auto worldA=::float4x4::get_Identity(),worldB=::float4x4::get_Identity(); worldA.M41=-0.5f; worldB.M41=frame==2?0.25f:0.5f;
                std::vector<helengine::vulkan::VulkanMeshBatcher::Input> inputs{Input(MeshA,A,worldA),Input(MeshB,B,worldB)};
                batcher.BeginFrame(); const auto draws=batcher.Build(inputs);
                const auto descriptor=Output.GetTextureDescriptor(Texture); const std::array<helengine::vulkan::VulkanMeshRenderer::ImageBinding,2> images{{{100,descriptor},{200,descriptor}}};
                for(const auto& draw:draws) {
                    const auto world=draw.Baked?::float4x4::get_Identity():(draw.InputIndex==0?worldA:worldB);
                    const std::array<float,16> matrix{world.M11.ToSingle(),world.M12.ToSingle(),world.M13.ToSingle(),world.M14.ToSingle(),world.M21.ToSingle(),world.M22.ToSingle(),world.M23.ToSingle(),world.M24.ToSingle(),world.M31.ToSingle(),world.M32.ToSingle(),world.M33.ToSingle(),world.M34.ToSingle(),world.M41.ToSingle(),world.M42.ToSingle(),world.M43.ToSingle(),world.M44.ToSingle()};
                    const std::array<float,4> tint{1,1,1,1}; std::vector<helengine::vulkan::VulkanMeshRenderer::BufferBinding> buffers{{0,std::vector<uint8_t>(64)},{1,std::vector<uint8_t>(16)}};
                    std::memcpy(buffers[0].Bytes.data(),matrix.data(),64); std::memcpy(buffers[1].Bytes.data(),tint.data(),16);
                    Renderer.QueueDraw(draw.Mesh,Material,buffers,images,draw.First,draw.Count);
                }
                if(Renderer.GetQueuedDrawCount()!=size_t(mode?1:2)) throw std::runtime_error("Batching did not reduce actual recorded GPU draws from two to one.");
                if(mode && frame==0 && !draws[0].Baked) throw std::runtime_error("Synthetic test failed to exercise real cross-mesh geometry.");
                if(mode==1 && frame==1 && batcher.GetCounters().CacheHits!=1) throw std::runtime_error("GPU static test did not reuse cached geometry.");
                if(mode==2 && batcher.GetCounters().DynamicBatches!=1) throw std::runtime_error("GPU dynamic test did not rebuild geometry.");
                Renderer.RenderFrame(Output.GetOutputImageView(),64,64,{0,0,0,1}); Output.BeginFrame({0,0,0,1},true); const auto pixels=Output.EndFrameAndReadback();
                if(!Renderer.GetLastVisibleSamples()) throw std::runtime_error("GPU batching fixture drew no visible samples.");
                if(mode==0) { if(frame==0) firstBaseline=pixels; if(frame==2) secondBaseline=pixels; }
                else if(pixels!=(frame==2?secondBaseline:firstBaseline)) throw std::runtime_error("Batching changed complete numeric Vulkan readback.");
                batcher.EndFrame();
            }
        }
        if(firstBaseline==secondBaseline) throw std::runtime_error("Moving-transform fixture produced an unchanged image.");
        if(Context.GetValidationErrorCount()) throw std::runtime_error("Mesh batching produced Vulkan validation errors.");
    }
private:
    /// <summary>Captures a real generated matrix adapter for one current source transform.</summary>
    static helengine::vulkan::VulkanMeshBatcher::Input Input(size_t mesh,const std::shared_ptr<helengine::vulkan::VulkanMeshBatcher::Geometry>& source,::float4x4 world) {
        const helengine::vulkan::VulkanMeshBatchGeometry geometry(world); helengine::vulkan::VulkanMeshBatcher::Input input; input.Mesh=mesh; input.Source=source; input.Count=3; input.Static=true; input.Supported=true; input.State="fixture-shader-texture-constants"; input.Transform=geometry.Key(); input.Bake=[geometry](const auto& vertex) { return geometry.Bake(vertex); }; return input;
    }
    /// <summary>Outlives every borrowed Vulkan resource.</summary>
    helengine::vulkan::VulkanContext Context;
    /// <summary>Owns the numeric readback target and texture.</summary>
    helengine::vulkan::VulkanRenderer2D Output;
    /// <summary>Owns actual indexed Vulkan mesh and transient allocations.</summary>
    helengine::vulkan::VulkanMeshRenderer Renderer;
    /// <summary>Retains different immutable source triangles.</summary>
    std::shared_ptr<helengine::vulkan::VulkanMeshBatcher::Geometry> A,B;
    /// <summary>Identifies owned source meshes.</summary>
    size_t MeshA,MeshB;
    /// <summary>Identifies the authored fixture pipeline.</summary>
    size_t Material;
    /// <summary>Identifies the sampled fixture texture.</summary>
    size_t Texture;
};
/// <summary>Runs complete numeric readback comparisons without screenshots or saved image output.</summary>
int main() {
    try { Fixture fixture; fixture.Run(); std::cout << "PASS Vulkan batching off static dynamic both cross-mesh moving-transforms cache-reuse draws=2-to-1 readback-equal validation=0\n"; return 0; }
    catch(const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
