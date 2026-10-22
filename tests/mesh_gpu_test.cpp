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
        if (!renderer.GetLastVisibleSamples()) throw std::runtime_error("Indexed GPU geometry produced no hardware occlusion samples");
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
        auto translated=identity; translated[0]=translated[5]=0.5f; translated[13]=0.5f;
        std::memcpy(buffers[0].Bytes.data(),translated.data(),64); std::memcpy(buffers[1].Bytes.data(),tint.data(),16);
        renderer.QueueDraw(nearMesh,material,buffers,images); renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1});
        const uint8_t white[]={255,255,255,255}; const auto overlay=output.CreateTexture(1,1,white);
        output.BeginFrame({0,0,0,1},true); output.DrawRect(overlay,4,44,8,8,{0,0,1,1}); pixels=output.EndFrameAndReadback();
        if(pixels[(16*64+32)*4]!=16 || pixels[(48*64+32)*4]!=0 || pixels[(48*64+8)*4+2]!=255) throw std::runtime_error("Authored asymmetric row-vector camera matrix and physical-pixel UI disagree about framebuffer Y orientation");
        output.ReleaseTexture(overlay); std::memcpy(buffers[0].Bytes.data(),identity.data(),64);
        std::memcpy(buffers[1].Bytes.data(),halfRed.data(),16);
        for(uint32_t cull=1;cull<=2;++cull) {
            const auto culled=renderer.UploadMaterial(vertex,"VSMain",fragment,"PSMain",layout,VulkanMeshRenderer::MaterialState{0,cull,true,true});
            renderer.QueueDraw(nearMesh,culled,buffers,images);
            renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1});
            if ((cull==1 && renderer.GetLastVisibleSamples()!=0) || (cull==2 && renderer.GetLastVisibleSamples()==0)) throw std::runtime_error("Hardware samples did not distinguish culled geometry from actual visible meshes");
            output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
            if((cull==1 && pixels[center]!=0) || (cull==2 && pixels[center]!=32)) throw std::runtime_error("Authored CCW front/back culling did not preserve engine clip-space winding");
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
        const auto atlas=renderer.CreateDepthShadow(32,32),cube=renderer.CreatePointShadow(32);
        const auto atlasInfo=renderer.GetShadowDescriptor(atlas),cubeInfo=renderer.GetShadowDescriptor(cube);
        rejected=false; try { renderer.RenderShadowPass(cube,6); } catch(const std::invalid_argument&) { rejected=true; }
        if(!rejected) throw std::runtime_error("Out-of-range cube face accepted");
        renderer.QueueDraw(nearMesh,material,buffers,images);
        rejected=false; try { renderer.RenderShadowPass(atlas); } catch(const std::invalid_argument&) { rejected=true; }
        if(!rejected) throw std::runtime_error("Forward color pipeline accepted by the depth-only shadow target");
        renderer.DiscardFrame();
        auto probeLayout=layout; probeLayout[1].MinimumBytes=16; probeLayout[2].Slot=101; probeLayout[3].Slot=201;
        std::vector<VulkanMeshRenderer::BindingLayout> shadowBindings(probeLayout.begin(),probeLayout.end());
        shadowBindings.push_back({102,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,VK_SHADER_STAGE_FRAGMENT_BIT,0}); shadowBindings.back().ViewType=VK_IMAGE_VIEW_TYPE_CUBE;
        shadowBindings.push_back({202,VK_DESCRIPTOR_TYPE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,0});
        const auto probeShader=ReadShader(std::string(HELENGINE_LINUX_SHADER_ROOT)+"/mesh_shadow_sample.frag.spv");
        const auto probeMaterial=renderer.UploadMaterial(vertex,"VSMain",probeShader,"PSMain",shadowBindings,opaque);
        const std::array<VulkanMeshRenderer::ImageBinding,4> shadowImages={{{101,atlasInfo},{201,atlasInfo},{102,cubeInfo,VK_IMAGE_VIEW_TYPE_CUBE},{202,cubeInfo}}};
        const std::array<float,4> direction={1,0,0,0.25f}; std::memcpy(buffers[1].Bytes.data(),direction.data(),16);
        auto wrongDimension=shadowImages; wrongDimension[2].ViewType=VK_IMAGE_VIEW_TYPE_2D;
        rejected=false; try { renderer.QueueDraw(nearMesh,probeMaterial,buffers,wrongDimension); } catch(const std::invalid_argument&) { rejected=true; }
        if(!rejected) throw std::runtime_error("Cube shader accepted a 2D view contract");
        renderer.QueueDraw(nearMesh,probeMaterial,buffers,shadowImages);
        rejected=false; try { renderer.ReleaseShadowTexture(cube); } catch(const std::logic_error&) { rejected=true; }
        if(!rejected) throw std::runtime_error("Queued sampled cube owner was released");
        renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1}); output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
        if(pixels[center]!=255 || pixels[center+1]!=255) throw std::runtime_error("Real cleared depth/cube descriptors did not sample neutral depth one");
        const auto depthVertex=ReadShader(std::string(HELENGINE_LINUX_SHADER_ROOT)+"/mesh_shadow_depth.vert.spv"),depthFragment=ReadShader(std::string(HELENGINE_LINUX_SHADER_ROOT)+"/mesh_shadow_depth.frag.spv");
        const auto pointVertex=ReadShader(std::string(HELENGINE_LINUX_SHADER_ROOT)+"/mesh_point_shadow.vert.spv"),pointFragment=ReadShader(std::string(HELENGINE_LINUX_SHADER_ROOT)+"/mesh_point_shadow.frag.spv");
        const std::array<VulkanMeshRenderer::BindingLayout,1> depthLayout={{{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,80}}};
        auto pointLayout=depthLayout; pointLayout[0].MinimumBytes=144;
        const VulkanMeshRenderer::MaterialState caster{0,2,true,true};
        const auto depthMaterial=renderer.UploadMaterial(depthVertex,"VS",depthFragment,"PS",depthLayout,caster,VulkanMeshRenderer::PassKind::DepthShadow);
        const auto pointMaterial=renderer.UploadMaterial(pointVertex,"VS",pointFragment,"PS",pointLayout,caster,VulkanMeshRenderer::PassKind::PointShadow);
        std::array<VulkanMeshRenderer::BufferBinding,1> depthBuffers={{{0,std::vector<uint8_t>(80)}}},pointBuffers={{{0,std::vector<uint8_t>(144)}}};
        std::memcpy(depthBuffers[0].Bytes.data(),identity.data(),64); std::memcpy(pointBuffers[0].Bytes.data(),identity.data(),64); std::memcpy(pointBuffers[0].Bytes.data()+64,identity.data(),64);
        const std::array<uint32_t,6> reverseIndices={0,2,1,0,3,2}; const auto reverseMesh=renderer.UploadMesh(vertices,reverseIndices);
        const std::array<float,4> initialLight={0,0,0,1}; std::memcpy(pointBuffers[0].Bytes.data()+128,initialLight.data(),16);
        renderer.QueueDraw(reverseMesh,depthMaterial,depthBuffers,{}); renderer.RenderShadowPass(atlas,0,true);
        renderer.QueueDraw(reverseMesh,pointMaterial,pointBuffers,{}); renderer.RenderShadowPass(cube,0,true);
        renderer.QueueDraw(nearMesh,probeMaterial,buffers,shadowImages); renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1}); output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
        if(pixels[center]!=255 || pixels[center+1]!=255) throw std::runtime_error("Authored Back culling wrote reverse-wound atlas or point-cube casters");
        renderer.ReleaseMesh(reverseMesh);
        renderer.QueueDraw(nearMesh,depthMaterial,depthBuffers,{});
        renderer.RenderShadowPass(atlas,0,false,VkRect2D{{0,0},{16,32}});
        const std::array<std::array<float,3>,6> axes={{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
        for(uint32_t face=0;face<6;++face) {
            const std::array<float,4> light={0,0,0,static_cast<float>(face+1)}; std::memcpy(pointBuffers[0].Bytes.data()+128,light.data(),16);
            renderer.QueueDraw(nearMesh,pointMaterial,pointBuffers,{}); renderer.RenderShadowPass(cube,face,true);
        }
        for(uint32_t face=0;face<6;++face) {
            const std::array<float,4> probe={axes[face][0],axes[face][1],axes[face][2],0.25f}; std::memcpy(buffers[1].Bytes.data(),probe.data(),16);
            renderer.QueueDraw(nearMesh,probeMaterial,buffers,shadowImages); renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1}); output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
            const int radial=static_cast<int>(64.0f/(face+1));
            if(std::abs(static_cast<int>(pixels[center])-64)>2 || std::abs(static_cast<int>(pixels[center+1])-radial)>2) throw std::runtime_error("Real builtin atlas/cube caster passes failed depth/radial sampling for face "+std::to_string(face));
        }
        const std::array<float,4> outsideAtlas={1,0,0,0.75f}; std::memcpy(buffers[1].Bytes.data(),outsideAtlas.data(),16);
        renderer.QueueDraw(nearMesh,probeMaterial,buffers,shadowImages); renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1}); output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
        if(pixels[center]!=255) throw std::runtime_error("Atlas tile render modified untouched depth outside its viewport");
        renderer.RenderShadowPass(atlas,0,true);
        auto upperVertices=vertices; for(auto& item:upperVertices) { item.Position[1]=(item.Position[1]+1)*0.5f; item.Position[2]=0.25f; }
        const auto upperMesh=renderer.UploadMesh(upperVertices,indices);
        renderer.QueueDraw(upperMesh,depthMaterial,depthBuffers,{}); renderer.RenderShadowPass(atlas,0,false,VkRect2D{{0,0},{16,32}});
        for(const float atlasU:{0.25f,-0.25f}) {
            const std::array<float,4> probe={1,0,0,atlasU}; std::memcpy(buffers[1].Bytes.data(),probe.data(),16);
            renderer.QueueDraw(nearMesh,probeMaterial,buffers,shadowImages); renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1}); output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
            const int expected=atlasU>0 ? 64 : 255;
            if(std::abs(static_cast<int>(pixels[center])-expected)>2) throw std::runtime_error("Shadow atlas raster Y disagrees with the existing ForwardStandardShader clip-to-UV contract");
        }
        const std::array<float,4> offsetLight={0,0.25f,0,1}; std::memcpy(pointBuffers[0].Bytes.data()+128,offsetLight.data(),16);
        renderer.QueueDraw(nearMesh,pointMaterial,pointBuffers,{}); renderer.RenderShadowPass(cube,0,true);
        for(const float directionY:{0.5f,-0.5f}) {
            const std::array<float,4> probe={1,directionY,0,0.25f}; std::memcpy(buffers[1].Bytes.data(),probe.data(),16);
            renderer.QueueDraw(nearMesh,probeMaterial,buffers,shadowImages); renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1}); output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
            const int expected=directionY>0 ? 90 : 202;
            if(std::abs(static_cast<int>(pixels[center+1])-expected)>5) throw std::runtime_error("Point shadow face raster Y disagrees with the native cube sampling direction contract");
        }
        const std::array<VulkanMeshRenderer::Vertex,8> box={{{{-1,-1,-0.25f},{0,0,1},{0,0}},{{1,-1,-0.25f},{0,0,1},{1,0}},{{1,1,-0.25f},{0,0,1},{1,1}},{{-1,1,-0.25f},{0,0,1},{0,1}},{{-1,-1,-2.25f},{0,0,-1},{0,0}},{{1,-1,-2.25f},{0,0,-1},{1,0}},{{1,1,-2.25f},{0,0,-1},{1,1}},{{-1,1,-2.25f},{0,0,-1},{0,1}}}};
        const std::array<uint32_t,36> boxIndices={0,1,2,0,2,3,4,6,5,4,7,6,0,3,7,0,7,4,1,5,6,1,6,2,3,2,6,3,6,7,0,4,5,0,5,1};
        const auto boxMesh=renderer.UploadMesh(box,boxIndices); auto boxProjection=identity; boxProjection[10]=-0.25f;
        std::memcpy(depthBuffers[0].Bytes.data(),boxProjection.data(),64); std::memcpy(pointBuffers[0].Bytes.data()+64,boxProjection.data(),64); std::memcpy(pointBuffers[0].Bytes.data()+128,initialLight.data(),16);
        renderer.QueueDraw(boxMesh,depthMaterial,depthBuffers,{}); renderer.RenderShadowPass(atlas,0,true);
        renderer.QueueDraw(boxMesh,pointMaterial,pointBuffers,{}); renderer.RenderShadowPass(cube,0,true);
        std::memcpy(buffers[1].Bytes.data(),direction.data(),16); renderer.QueueDraw(nearMesh,probeMaterial,buffers,shadowImages); renderer.RenderFrame(output.GetOutputImageView(),64,64,{0,0,0,1}); output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
        if(std::abs(static_cast<int>(pixels[center])-16)>2 || std::abs(static_cast<int>(pixels[center+1])-64)>3) throw std::runtime_error("Closed procedural cube Back caster sampled its back face instead of the authored front face");
        renderer.ReleaseMesh(boxMesh);
        renderer.ReleaseMesh(upperMesh);
        renderer.ReleaseMaterial(depthMaterial); renderer.ReleaseMaterial(pointMaterial); renderer.ReleaseMaterial(probeMaterial); renderer.ReleaseShadowTexture(atlas); renderer.ReleaseShadowTexture(cube);
        output.Resize(32,16); renderer.RenderFrame(output.GetOutputImageView(),32,16,{0,1,0,1});
        output.BeginFrame({0,0,0,1},true); pixels=output.EndFrameAndReadback();
        if (pixels.size()!=32*16*4 || pixels[1]!=255) throw std::runtime_error("Resized borrowed color/depth target failed");
        renderer.ReleaseMaterial(material); renderer.ReleaseMesh(nearMesh); renderer.ReleaseMesh(farMesh); output.ReleaseTexture(texture);
        if (context.GetValidationErrorCount()) throw std::runtime_error("Vulkan validation reported mesh errors");
        std::cout<<"PASS device="<<context.GetProperties().deviceName<<" indexed-depth authored-HLSL uniforms texture100 sampler200 snapshots color-load alpha culling submesh viewport resize device-limits release-validation sampled-D32-atlas R32-cube-six-faces builtin-shadow-passes atlas-tiles shadow-UV-orientation Back-casters reverse-winding camera-matrix-UI-Y rgba=16,128,56,255\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL mesh GPU: "<<error.what()<<'\n'; return 1; }
}
