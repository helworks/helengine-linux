#include "vulkan_mesh_renderer.hpp"
#include "vulkan_renderer_2d.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

/// <summary>Loads the actual authored HLSL fixture compiled to aligned SPIR-V by the native build.</summary>
static std::vector<uint32_t> ReadShader(const std::string& path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if (!stream || stream.tellg()<=0 || stream.tellg()%4) throw std::runtime_error("Missing aligned mesh shader: "+path);
    std::vector<uint32_t> words(static_cast<size_t>(stream.tellg())/4);
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(words.data()),static_cast<std::streamsize>(words.size()*4))) throw std::runtime_error("Incomplete mesh shader: "+path);
    return words;
}

/// <summary>Proves real indexed geometry, depth rejection, separate descriptor namespaces, snapshots and GPU resource ownership without a desktop window.</summary>
int main(int argc,char** argv) {
    try {
        using namespace helengine::linux;
        VulkanContext context({},argc>1 && std::string(argv[1])=="--validation",VK_API_VERSION_1_2);
        VulkanRenderer2D output(context,64,64);
        VulkanMeshRenderer renderer(context);
        const auto vertex=ReadShader(std::string(HELENGINE_LINUX_SHADER_ROOT)+"/mesh.vert.spv");
        const auto fragment=ReadShader(std::string(HELENGINE_LINUX_SHADER_ROOT)+"/mesh.frag.spv");
        const std::array<VulkanMeshRenderer::BindingLayout,4> layout={{{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,64},{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_FRAGMENT_BIT,16},{100,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,VK_SHADER_STAGE_FRAGMENT_BIT,0},{200,VK_DESCRIPTOR_TYPE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,0}}};
        if(vertex[1]>0x00010300u) {
            VulkanContext legacy({},argc>1 && std::string(argv[1])=="--validation",VK_API_VERSION_1_1);
            VulkanMeshRenderer oldRenderer(legacy); bool incompatibleRejected=false;
            try { oldRenderer.UploadMaterial(vertex,"VSMain",fragment,"PSMain",layout); } catch(const std::invalid_argument&) { incompatibleRejected=true; }
            if(!incompatibleRejected || legacy.GetValidationErrorCount()) throw std::runtime_error("SPIR-V version incompatible with requested Vulkan API was not rejected before driver validation");
        }
        const VulkanMeshRenderer::MaterialState opaque{0,0,true,true};
        const auto material=renderer.UploadMaterial(vertex,"VSMain",fragment,"PSMain",layout,opaque);
        std::array<VulkanMeshRenderer::Vertex,4> vertices={{{{-1,-1,0.25f},{0,0,1},{0,0}},{{1,-1,0.25f},{0,0,1},{1,0}},{{1,1,0.25f},{0,0,1},{1,1}},{{-1,1,0.25f},{0,0,1},{0,1}}}};
        const std::array<uint32_t,6> indices={0,1,2,0,2,3};
        const auto nearMesh=renderer.UploadMesh(vertices,indices);
        for (auto& item:vertices) item.Position[2]=0.75f;
        const auto farMesh=renderer.UploadMesh(vertices,indices);
        bool rejected=false;
        try { const std::array<uint32_t,3> bad={0,1,99}; renderer.UploadMesh(vertices,bad); } catch(const std::invalid_argument&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Out-of-range mesh index accepted");
        const uint8_t texel[]={32,128,224,255}; const auto texture=output.CreateTexture(1,1,texel);
        const auto image=output.GetTextureDescriptor(texture);
        const std::array<VulkanMeshRenderer::ImageBinding,2> images={{{100,image},{200,image}}};
        const std::array<float,16> identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const std::array<float,4> tint={0.5f,1,0.25f,1};
        std::array<VulkanMeshRenderer::BufferBinding,2> buffers={{{0,std::vector<uint8_t>(64)},{1,std::vector<uint8_t>(16)}}};
        std::memcpy(buffers[0].Bytes.data(),identity.data(),64); std::memcpy(buffers[1].Bytes.data(),tint.data(),16);
        rejected=false; try { renderer.QueueDraw(nearMesh,material,buffers,{}); } catch(const std::invalid_argument&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Missing sampled image/sampler accepted before GPU");
        auto shortBuffers=buffers; shortBuffers[0].Bytes.resize(4);
        rejected=false; try { renderer.QueueDraw(nearMesh,material,shortBuffers,images); } catch(const std::invalid_argument&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Short reflected uniform accepted before GPU");
        renderer.QueueDraw(nearMesh,material,buffers,images);
        if (!renderer.ReferencesImage(image.imageView)) throw std::runtime_error("Queued texture ownership absent");
        rejected=false; try { renderer.ReleaseMesh(nearMesh); } catch(const std::logic_error&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Queued mesh release accepted");
        rejected=false; try { renderer.ReleaseMaterial(material); } catch(const std::logic_error&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Queued material release accepted");
        const std::array<float,4> green={0,1,0,1}; std::memcpy(buffers[1].Bytes.data(),green.data(),16);
        renderer.QueueDraw(farMesh,material,buffers,images);
        renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1});
        output.BeginFrame({0,0,0,1},true); auto pixels=output.EndFrameAndReadback();
        const size_t center=(32*64+32)*4;
        if (pixels[center]!=16 || pixels[center+1]!=128 || pixels[center+2]!=56 || pixels[center+3]!=255) throw std::runtime_error("Authored HLSL sampling/uniform snapshot/indexed depth GPU contract failed");
        if (renderer.HasPendingDraws() || renderer.ReferencesImage(image.imageView)) throw std::runtime_error("Completed draws still retain resource ownership");
        renderer.RenderFrame(output.GetOutputImageView(),64,64,{1,0,0,1},true);
        output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
        if (pixels[center]!=16 || pixels[center+1]!=128 || pixels[center+2]!=56) throw std::runtime_error("Camera color LOAD failed to preserve the previous frame");
        const VulkanMeshRenderer::MaterialState alpha{1,0,false,false};
        const auto transparent=renderer.UploadMaterial(vertex,"VSMain",fragment,"PSMain",layout,alpha);
        const std::array<float,4> halfRed={1,0,0,0.5f}; std::memcpy(buffers[1].Bytes.data(),halfRed.data(),16);
        renderer.QueueDraw(farMesh,transparent,buffers,images,3,3);
        renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,1,1},false,VkRect2D{{16,8},{32,24}});
        output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
        const size_t triangle=(24*64+20)*4;
        if (pixels[triangle]<15 || pixels[triangle]>17 || pixels[triangle+2]<127 || pixels[triangle+2]>129 || pixels[triangle+3]<127 || pixels[triangle+3]>129 || pixels[0]!=0 || pixels[2]!=255) throw std::runtime_error("Authored alpha/submesh/camera viewport contract failed");
        renderer.ReleaseMaterial(transparent);
        for(uint32_t cull=1;cull<=2;++cull) {
            const auto culled=renderer.UploadMaterial(vertex,"VSMain",fragment,"PSMain",layout,VulkanMeshRenderer::MaterialState{0,cull,true,true});
            renderer.QueueDraw(nearMesh,culled,buffers,images);
            renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1});
            output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
            if((cull==1 && pixels[center]!=32) || (cull==2 && pixels[center]!=0)) throw std::runtime_error("Authored front/back culling did not preserve the Vulkan framebuffer winding contract");
            renderer.ReleaseMaterial(culled);
        }
        auto arrays=layout; arrays[2].Count=2;
        rejected=false; try { renderer.UploadMaterial(vertex,"VSMain",fragment,"PSMain",arrays,opaque); } catch(const std::invalid_argument&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Unsupported descriptor array accepted");
        auto conflict=layout; conflict[2].Slot=0;
        rejected=false; try { renderer.UploadMaterial(vertex,"VSMain",fragment,"PSMain",conflict,opaque); } catch(const std::invalid_argument&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Conflicting unified resource slots accepted");
        auto spaces=layout; spaces[2].Set=1;
        rejected=false; try { renderer.UploadMaterial(vertex,"VSMain",fragment,"PSMain",spaces,opaque); } catch(const std::invalid_argument&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Unsupported descriptor set silently remapped to zero");
        std::vector<VulkanMeshRenderer::BindingLayout> overLimit(context.GetProperties().limits.maxPerStageDescriptorUniformBuffers+1);
        for(size_t index=0;index<overLimit.size();++index) overLimit[index]={static_cast<uint32_t>(index),VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,16};
        rejected=false; try { renderer.UploadMaterial(vertex,"VSMain",fragment,"PSMain",overLimit,opaque); } catch(const std::invalid_argument&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Physical device per-stage descriptor limit ignored");
        rejected=false; try { renderer.QueueDraw(nearMesh,material,buffers,images,3,6); } catch(const std::invalid_argument&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Submesh index overflow accepted");
        output.Resize(32,16); renderer.RenderFrame(output.GetOutputImageView(),32,16,{0,1,0,1});
        output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
        if (pixels.size()!=32*16*4 || pixels[1]!=255) throw std::runtime_error("Resized borrowed color/depth target failed");
        renderer.ReleaseMaterial(material); renderer.ReleaseMesh(nearMesh); renderer.ReleaseMesh(farMesh); output.ReleaseTexture(texture);
        if (context.GetValidationErrorCount()) throw std::runtime_error("Vulkan validation reported mesh errors");
        std::cout<<"PASS device="<<context.GetProperties().deviceName<<" indexed-depth authored-HLSL uniforms texture100 sampler200 snapshots color-load alpha culling submesh viewport resize device-limits release-validation rgba=16,128,56,255\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL mesh GPU: "<<error.what()<<'\n'; return 1; }
}
