#include "vulkan_mesh_renderer.hpp"
#include "gpu_buffer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace helengine::linux {
static_assert(sizeof(VulkanMeshRenderer::Vertex)==32,"The native mesh vertex ABI must remain float32 position/normal/UV.");

/// <summary>Holds native resources under deterministic ownership; color targets and sampled textures remain borrowed from the host.</summary>
class VulkanMeshRenderer::State final {
public:
    /// <summary>Owns staged device-local geometry and immutable triangle count.</summary>
    struct Mesh {
        /// <summary>Owns the uploaded fixed-layout vertex buffer.</summary>
        std::unique_ptr<GpuBuffer> Vertices;
        /// <summary>Owns the uploaded uint32 index buffer.</summary>
        std::unique_ptr<GpuBuffer> Indices;
        /// <summary>Bounds every queued submesh draw.</summary>
        uint32_t IndexCount;
    };
    /// <summary>Owns one authored pipeline and its exact reflected descriptor contract.</summary>
    struct Material {
        /// <summary>Borrows the device which outlives this material.</summary>
        VkDevice Device;
        /// <summary>Owns the descriptor layout for engine register space zero.</summary>
        VkDescriptorSetLayout Descriptors=VK_NULL_HANDLE;
        /// <summary>Owns the compatible pipeline layout.</summary>
        VkPipelineLayout Layout=VK_NULL_HANDLE;
        /// <summary>Owns the native authored graphics pipeline.</summary>
        VkPipeline Pipeline=VK_NULL_HANDLE;
        /// <summary>Stores validated combined-stage descriptor requirements.</summary>
        std::vector<BindingLayout> Bindings;
        /// <summary>Initializes the borrowing relationship before Vulkan handle creation.</summary>
        explicit Material(VkDevice device):Device(device) {}
        /// <summary>Releases handles in dependency order, including partial construction failures.</summary>
        ~Material() {
            if(Pipeline) vkDestroyPipeline(Device,Pipeline,nullptr);
            if(Layout) vkDestroyPipelineLayout(Device,Layout,nullptr);
            if(Descriptors) vkDestroyDescriptorSetLayout(Device,Descriptors,nullptr);
        }
    };
    /// <summary>Owns temporary shader modules until pipeline creation finishes or fails.</summary>
    struct Modules {
        /// <summary>Borrows the logical device.</summary>
        VkDevice Device;
        /// <summary>Owns the authored vertex module.</summary>
        VkShaderModule Vertex=VK_NULL_HANDLE;
        /// <summary>Owns the authored fragment module.</summary>
        VkShaderModule Fragment=VK_NULL_HANDLE;
        /// <summary>Releases both modules after pipeline compilation.</summary>
        ~Modules() { if(Vertex) vkDestroyShaderModule(Device,Vertex,nullptr); if(Fragment) vkDestroyShaderModule(Device,Fragment,nullptr); }
    };
    /// <summary>Owns a correctly formatted depth attachment independently of borrowed color.</summary>
    struct Depth {
        /// <summary>Borrows the owning context.</summary>
        VulkanContext& Context;
        /// <summary>Owns the optimal-tiled depth image.</summary>
        VkImage Image=VK_NULL_HANDLE;
        /// <summary>Owns device-local depth memory.</summary>
        VkDeviceMemory Memory=VK_NULL_HANDLE;
        /// <summary>Owns the depth-only view.</summary>
        VkImageView View=VK_NULL_HANDLE;
        /// <summary>Creates a D32 depth image with explicit capability checks.</summary>
        Depth(VulkanContext& context,uint32_t width,uint32_t height):Context(context) {
            VkFormatProperties properties{}; vkGetPhysicalDeviceFormatProperties(context.GetPhysicalDevice(),VK_FORMAT_D32_SFLOAT,&properties);
            if(!(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) throw std::runtime_error("Vulkan mesh target requires D32_SFLOAT depth attachment support");
            VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; create.imageType=VK_IMAGE_TYPE_2D; create.format=VK_FORMAT_D32_SFLOAT;
            create.extent={width,height,1}; create.mipLevels=1; create.arrayLayers=1; create.samples=VK_SAMPLE_COUNT_1_BIT; create.tiling=VK_IMAGE_TILING_OPTIMAL; create.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            try {
                VulkanContext::Require(vkCreateImage(context.GetDevice(),&create,nullptr,&Image),"vkCreateImage(mesh depth)");
                VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(context.GetDevice(),Image,&requirements);
                VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize=requirements.size; allocate.memoryTypeIndex=context.FindMemoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                VulkanContext::Require(vkAllocateMemory(context.GetDevice(),&allocate,nullptr,&Memory),"vkAllocateMemory(mesh depth)");
                VulkanContext::Require(vkBindImageMemory(context.GetDevice(),Image,Memory,0),"vkBindImageMemory(mesh depth)");
                VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=Image; view.viewType=VK_IMAGE_VIEW_TYPE_2D; view.format=VK_FORMAT_D32_SFLOAT; view.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
                VulkanContext::Require(vkCreateImageView(context.GetDevice(),&view,nullptr,&View),"vkCreateImageView(mesh depth)");
            } catch(...) { Release(); throw; }
        }
        /// <summary>Releases all initialized native depth handles.</summary>
        void Release() noexcept { if(View) vkDestroyImageView(Context.GetDevice(),View,nullptr); if(Image) vkDestroyImage(Context.GetDevice(),Image,nullptr); if(Memory) vkFreeMemory(Context.GetDevice(),Memory,nullptr); }
        /// <summary>Releases depth resources after synchronous submission finishes.</summary>
        ~Depth() { Release(); }
    };
    /// <summary>Owns a queued draw and its copied uniform bytes while borrowing material, mesh and texture handles.</summary>
    struct Draw {
        /// <summary>Selects the owned mesh.</summary>
        size_t MeshId;
        /// <summary>Selects the owned authored pipeline.</summary>
        size_t MaterialId;
        /// <summary>Stores the validated start of the authored submesh.</summary>
        uint32_t FirstIndex;
        /// <summary>Stores the validated number of triangle indices.</summary>
        uint32_t IndexCount;
        /// <summary>Owns the exact per-draw uniform snapshots.</summary>
        std::vector<BufferBinding> Buffers;
        /// <summary>Borrows same-device native texture descriptors until submission completes.</summary>
        std::vector<ImageBinding> Images;
    };
    /// <summary>Owns transient frame descriptors, constants, depth and framebuffer until the submission fence completes.</summary>
    struct Frame {
        /// <summary>Borrows the device.</summary>
        VkDevice Device;
        /// <summary>Owns the temporary framebuffer referencing borrowed color and owned depth.</summary>
        VkFramebuffer Framebuffer=VK_NULL_HANDLE;
        /// <summary>Owns every per-draw descriptor set for this frame.</summary>
        VkDescriptorPool Pool=VK_NULL_HANDLE;
        /// <summary>Owns all uniform allocations used by pending descriptor sets.</summary>
        std::vector<std::unique_ptr<GpuBuffer>> Uniforms;
        /// <summary>Owns the depth attachment, released after the framebuffer.</summary>
        std::unique_ptr<Depth> DepthTarget;
        /// <summary>Releases frame handles after the context synchronizes successful or failed submission.</summary>
        ~Frame() { if(Framebuffer) vkDestroyFramebuffer(Device,Framebuffer,nullptr); if(Pool) vkDestroyDescriptorPool(Device,Pool,nullptr); }
    };
    /// <summary>Borrows the context which outlives all resource state.</summary>
    VulkanContext& Context;
    /// <summary>Owns the pass which explicitly discards and clears the previous color contents.</summary>
    VkRenderPass ClearPass=VK_NULL_HANDLE;
    /// <summary>Owns the compatible pass which loads color from the previous transfer-source pass.</summary>
    VkRenderPass LoadPass=VK_NULL_HANDLE;
    /// <summary>Owns uploaded meshes by stable identity.</summary>
    std::unordered_map<size_t,std::unique_ptr<Mesh>> Meshes;
    /// <summary>Owns compiled authored materials by stable identity.</summary>
    std::unordered_map<size_t,std::unique_ptr<Material>> Materials;
    /// <summary>Stores not-yet-submitted draw snapshots in authored order.</summary>
    std::vector<Draw> Draws;
    /// <summary>Allocates monotonic nonzero mesh identities.</summary>
    size_t NextMesh=1;
    /// <summary>Allocates monotonic nonzero material identities.</summary>
    size_t NextMaterial=1;

    /// <summary>Initializes compatible render passes with explicit inter-pass color dependencies.</summary>
    explicit State(VulkanContext& context):Context(context) {
        try { ClearPass=CreatePass(false); LoadPass=CreatePass(true); }
        catch(...) { if(ClearPass) vkDestroyRenderPass(context.GetDevice(),ClearPass,nullptr); throw; }
    }
    /// <summary>Releases owned resources after all synchronous rendering has finished.</summary>
    ~State() {
        vkDeviceWaitIdle(Context.GetDevice());
        Materials.clear(); Meshes.clear();
        if(LoadPass) vkDestroyRenderPass(Context.GetDevice(),LoadPass,nullptr);
        if(ClearPass) vkDestroyRenderPass(Context.GetDevice(),ClearPass,nullptr);
    }
    /// <summary>Creates one clear/load color pass with independent depth and a color-attachment final layout for UI composition.</summary>
    VkRenderPass CreatePass(bool load) {
        std::array<VkAttachmentDescription,2> attachments{};
        attachments[0].format=VK_FORMAT_R8G8B8A8_UNORM; attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp=load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout=load ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED; attachments[0].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1].format=VK_FORMAT_D32_SFLOAT; attachments[1].samples=VK_SAMPLE_COUNT_1_BIT; attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachments[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; attachments[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount=1; subpass.pColorAttachments=&color; subpass.pDepthStencilAttachment=&depth;
        std::array<VkSubpassDependency,2> dependencies{};
        dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL; dependencies[0].dstSubpass=0; dependencies[0].srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass=0; dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL; dependencies[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dependencies[1].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo create{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; create.attachmentCount=attachments.size(); create.pAttachments=attachments.data(); create.subpassCount=1; create.pSubpasses=&subpass; create.dependencyCount=dependencies.size(); create.pDependencies=dependencies.data();
        VkRenderPass pass=VK_NULL_HANDLE; VulkanContext::Require(vkCreateRenderPass(Context.GetDevice(),&create,nullptr,&pass),"vkCreateRenderPass(mesh)"); return pass;
    }
    /// <summary>Uploads caller-owned bytes using a temporary coherent staging buffer and a vertex/index visibility barrier.</summary>
    std::unique_ptr<GpuBuffer> Upload(const void* data,size_t bytes,VkBufferUsageFlags usage,VkAccessFlags access) {
        GpuBuffer staging(Context,bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::memcpy(staging.Map(),data,bytes); staging.Unmap();
        auto target=std::make_unique<GpuBuffer>(Context,bytes,usage|VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        auto commands=Context.BeginCommands(); VkBufferCopy copy{0,0,bytes}; vkCmdCopyBuffer(commands,staging.Get(),target->Get(),1,&copy);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=access;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.buffer=target->Get(); barrier.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,0,nullptr,1,&barrier,0,nullptr);
        Context.SubmitAndWait(commands); return target;
    }
    /// <summary>Creates a shader module from an intact SPIR-V word stream without source or binary rewriting.</summary>
    VkShaderModule CreateModule(std::span<const uint32_t> words) {
        if(words.size()<5 || words[0]!=0x07230203u) throw std::invalid_argument("Authored Vulkan mesh program requires an intact SPIR-V module");
        const uint32_t api=Context.GetApiVersion();
        const uint32_t maximumSpirv=api>=VK_API_VERSION_1_3 ? 0x00010600u : (api>=VK_API_VERSION_1_2 ? 0x00010500u : 0x00010300u);
        if(words[1]<0x00010000u || words[1]>maximumSpirv) throw std::invalid_argument("Authored mesh SPIR-V version exceeds the requested Vulkan context API; engine shaders require Vulkan 1.2 or newer");
        VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; create.codeSize=words.size_bytes(); create.pCode=words.data();
        VkShaderModule module=VK_NULL_HANDLE; VulkanContext::Require(vkCreateShaderModule(Context.GetDevice(),&create,nullptr,&module),"vkCreateShaderModule(authored mesh)"); return module;
    }
    /// <summary>Normalizes same-slot stage declarations and validates exactly the supported reflected descriptor contract against physical device limits.</summary>
    std::vector<BindingLayout> ValidateLayout(std::span<const BindingLayout> input) const {
        std::vector<BindingLayout> layout;
        const auto& limits=Context.GetProperties().limits;
        for(const auto& binding:input) {
            if(!binding.Stages || (binding.Stages&~(VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT)) || binding.Count!=1 || binding.Set!=0) throw std::invalid_argument("Vulkan mesh reflection requires scalar set-zero descriptors in vertex/fragment stages");
            if(binding.Type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                if(!binding.MinimumBytes || binding.MinimumBytes>limits.maxUniformBufferRange) throw std::invalid_argument("Vulkan mesh uniform reflection exceeds the device byte range");
            } else if(binding.Type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || binding.Type==VK_DESCRIPTOR_TYPE_SAMPLER) {
                if(binding.MinimumBytes) throw std::invalid_argument("Vulkan image/sampler reflection cannot require uniform bytes");
            } else throw std::invalid_argument("Vulkan mesh reflection supports only uniform buffers, separate textures and separate samplers");
            auto previous=std::find_if(layout.begin(),layout.end(),[&binding](const auto& item){return item.Slot==binding.Slot;});
            if(previous==layout.end()) layout.push_back(binding);
            else {
                if(previous->Type!=binding.Type) throw std::invalid_argument("Vulkan shader reflection assigns conflicting resource kinds to one unified descriptor slot");
                previous->Stages|=binding.Stages; previous->MinimumBytes=std::max(previous->MinimumBytes,binding.MinimumBytes);
            }
        }
        for(const auto stage:{VK_SHADER_STAGE_VERTEX_BIT,VK_SHADER_STAGE_FRAGMENT_BIT}) {
            uint32_t uniforms=0,images=0,samplers=0;
            for(const auto& binding:layout) if(binding.Stages&stage) { if(binding.Type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) ++uniforms; else if(binding.Type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) ++images; else ++samplers; }
            if(uniforms>limits.maxPerStageDescriptorUniformBuffers || images>limits.maxPerStageDescriptorSampledImages || samplers>limits.maxPerStageDescriptorSamplers || uniforms+images+samplers>limits.maxPerStageResources) throw std::invalid_argument("Vulkan mesh reflected descriptors exceed per-stage device limits");
        }
        uint32_t uniforms=0,images=0,samplers=0;
        for(const auto& binding:layout) { if(binding.Type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) ++uniforms; else if(binding.Type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) ++images; else ++samplers; }
        if(uniforms>limits.maxDescriptorSetUniformBuffers || images>limits.maxDescriptorSetSampledImages || samplers>limits.maxDescriptorSetSamplers) throw std::invalid_argument("Vulkan mesh descriptors exceed set-zero device limits");
        return layout;
    }
};

VulkanMeshRenderer::VulkanMeshRenderer(VulkanContext& context):Resources(std::make_unique<State>(context)) {}
VulkanMeshRenderer::~VulkanMeshRenderer()=default;

size_t VulkanMeshRenderer::UploadMesh(std::span<const Vertex> vertices,std::span<const uint32_t> indices) {
    if(vertices.empty() || indices.empty() || indices.size()%3 || indices.size()>UINT32_MAX || vertices.size()>UINT32_MAX) throw std::invalid_argument("Vulkan mesh upload requires nonempty indexed triangles within uint32 limits");
    for(const auto& vertex:vertices) {
        for(float value:vertex.Position) if(!std::isfinite(value)) throw std::invalid_argument("Vulkan mesh positions must be finite float32 values");
        for(float value:vertex.Normal) if(!std::isfinite(value)) throw std::invalid_argument("Vulkan mesh normals must be finite float32 values");
        for(float value:vertex.Uv) if(!std::isfinite(value)) throw std::invalid_argument("Vulkan mesh UVs must be finite float32 values");
    }
    for(uint32_t index:indices) if(index>=vertices.size()) throw std::invalid_argument("Vulkan mesh index exceeds its authored vertex range");
    auto mesh=std::make_unique<State::Mesh>();
    mesh->Vertices=Resources->Upload(vertices.data(),vertices.size_bytes(),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
    mesh->Indices=Resources->Upload(indices.data(),indices.size_bytes(),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,VK_ACCESS_INDEX_READ_BIT); mesh->IndexCount=static_cast<uint32_t>(indices.size());
    const size_t identity=Resources->NextMesh++; Resources->Meshes.emplace(identity,std::move(mesh)); return identity;
}

size_t VulkanMeshRenderer::UploadMaterial(std::span<const uint32_t> vertex,std::string vertexEntry,std::span<const uint32_t> fragment,std::string fragmentEntry,std::span<const BindingLayout> bindings) {
    return UploadMaterial(vertex,std::move(vertexEntry),fragment,std::move(fragmentEntry),bindings,MaterialState{});
}

size_t VulkanMeshRenderer::UploadMaterial(std::span<const uint32_t> vertex,std::string vertexEntry,std::span<const uint32_t> fragment,std::string fragmentEntry,std::span<const BindingLayout> bindings,MaterialState renderState) {
    if(vertexEntry.empty() || fragmentEntry.empty() || vertexEntry.find('\0')!=std::string::npos || fragmentEntry.find('\0')!=std::string::npos) throw std::invalid_argument("Vulkan mesh shader programs require explicit intact authored entry points");
    if(renderState.BlendMode>1 || renderState.CullMode>2) throw std::invalid_argument("Vulkan authored material has an unsupported engine blend or cull enum value");
    auto& state=*Resources; const auto device=state.Context.GetDevice(); auto material=std::make_unique<State::Material>(device); material->Bindings=state.ValidateLayout(bindings);
    std::vector<VkDescriptorSetLayoutBinding> nativeBindings;
    for(const auto& binding:material->Bindings) nativeBindings.push_back({binding.Slot,binding.Type,1,binding.Stages,nullptr});
    VkDescriptorSetLayoutCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; descriptors.bindingCount=static_cast<uint32_t>(nativeBindings.size()); descriptors.pBindings=nativeBindings.data();
    VulkanContext::Require(vkCreateDescriptorSetLayout(device,&descriptors,nullptr,&material->Descriptors),"vkCreateDescriptorSetLayout(authored mesh reflection)");
    VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pipelineLayout.setLayoutCount=1; pipelineLayout.pSetLayouts=&material->Descriptors;
    VulkanContext::Require(vkCreatePipelineLayout(device,&pipelineLayout,nullptr,&material->Layout),"vkCreatePipelineLayout(authored mesh reflection)");
    State::Modules modules{device}; modules.Vertex=state.CreateModule(vertex); modules.Fragment=state.CreateModule(fragment);
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=modules.Vertex; stages[0].pName=vertexEntry.c_str(); stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=modules.Fragment; stages[1].pName=fragmentEntry.c_str();
    VkVertexInputBindingDescription vertexBinding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array<VkVertexInputAttributeDescription,3> attributes={{{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,Position)},{1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,Normal)},{2,0,VK_FORMAT_R32G32_SFLOAT,offsetof(Vertex,Uv)}}};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; input.vertexBindingDescriptionCount=1; input.pVertexBindingDescriptions=&vertexBinding; input.vertexAttributeDescriptionCount=attributes.size(); input.pVertexAttributeDescriptions=attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount=1; viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=renderState.CullMode==0 ? VK_CULL_MODE_NONE : (renderState.CullMode==1 ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT); raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; depth.depthTestEnable=renderState.DepthTestEnabled; depth.depthWriteEnable=renderState.DepthWriteEnabled; depth.depthCompareOp=VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState color{}; color.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    color.blendEnable=renderState.BlendMode==1; color.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; color.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; color.colorBlendOp=VK_BLEND_OP_ADD;
    color.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE; color.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO; color.alphaBlendOp=VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount=1; blend.pAttachments=&color;
    const VkDynamicState dynamicStates[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dynamic.dynamicStateCount=2; dynamic.pDynamicStates=dynamicStates;
    VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; create.stageCount=stages.size(); create.pStages=stages.data(); create.pVertexInputState=&input; create.pInputAssemblyState=&assembly; create.pViewportState=&viewport; create.pRasterizationState=&raster; create.pMultisampleState=&multisample; create.pDepthStencilState=&depth; create.pColorBlendState=&blend; create.pDynamicState=&dynamic; create.layout=material->Layout; create.renderPass=state.ClearPass;
    VulkanContext::Require(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&create,nullptr,&material->Pipeline),"vkCreateGraphicsPipelines(authored mesh; check engine reflection, entry points and SPIR-V interface)");
    const size_t identity=state.NextMaterial++; state.Materials.emplace(identity,std::move(material)); return identity;
}

void VulkanMeshRenderer::QueueDraw(size_t meshId,size_t materialId,std::span<const BufferBinding> buffers,std::span<const ImageBinding> images,uint32_t firstIndex,uint32_t indexCount) {
    auto& state=*Resources; auto mesh=state.Meshes.find(meshId); auto material=state.Materials.find(materialId);
    if(mesh==state.Meshes.end() || material==state.Materials.end()) throw std::invalid_argument("Vulkan indexed draw requires live mesh and material owners");
    if(!indexCount && firstIndex<mesh->second->IndexCount) indexCount=mesh->second->IndexCount-firstIndex;
    if(!indexCount || indexCount%3 || firstIndex%3 || firstIndex>mesh->second->IndexCount || indexCount>mesh->second->IndexCount-firstIndex) throw std::invalid_argument("Vulkan submesh draw exceeds its indexed triangle range");
    const auto& layout=material->second->Bindings;
    for(size_t index=0;index<buffers.size();++index) {
        const auto& buffer=buffers[index]; auto required=std::find_if(layout.begin(),layout.end(),[&buffer](const auto& item){return item.Slot==buffer.Slot;});
        if(required==layout.end() || required->Type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || buffer.Bytes.size()<required->MinimumBytes || buffer.Bytes.size()>state.Context.GetProperties().limits.maxUniformBufferRange) throw std::invalid_argument("Vulkan draw has unknown, short or oversized reflected uniform bytes");
        for(size_t previous=0;previous<index;++previous) if(buffers[previous].Slot==buffer.Slot) throw std::invalid_argument("Vulkan draw repeats a uniform binding slot");
    }
    for(size_t index=0;index<images.size();++index) {
        const auto& image=images[index]; auto required=std::find_if(layout.begin(),layout.end(),[&image](const auto& item){return item.Slot==image.Slot;});
        if(required==layout.end() || required->Type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) throw std::invalid_argument("Vulkan draw has an unknown reflected image/sampler slot");
        if(required->Type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && (!image.Image.imageView || image.Image.imageLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) throw std::invalid_argument("Vulkan draw requires a live sampled image in shader-read layout");
        if(required->Type==VK_DESCRIPTOR_TYPE_SAMPLER && !image.Image.sampler) throw std::invalid_argument("Vulkan draw requires a live reflected sampler");
        for(size_t previous=0;previous<index;++previous) if(images[previous].Slot==image.Slot) throw std::invalid_argument("Vulkan draw repeats an image/sampler binding slot");
    }
    for(const auto& required:layout) {
        const bool present=required.Type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ? std::any_of(buffers.begin(),buffers.end(),[&required](const auto& item){return item.Slot==required.Slot;}) : std::any_of(images.begin(),images.end(),[&required](const auto& item){return item.Slot==required.Slot;});
        if(!present) throw std::invalid_argument("Vulkan draw is missing an engine-reflected required descriptor");
    }
    State::Draw draw{meshId,materialId,firstIndex,indexCount,{},{}}; draw.Buffers.assign(buffers.begin(),buffers.end()); draw.Images.assign(images.begin(),images.end()); state.Draws.push_back(std::move(draw));
}

void VulkanMeshRenderer::RenderFrame(VkImageView target,uint32_t width,uint32_t height,std::array<float,4> clear,bool loadExistingColor) {
    RenderFrame(target,width,height,clear,loadExistingColor,VkRect2D{{0,0},{width,height}});
}

void VulkanMeshRenderer::RenderFrame(VkImageView target,uint32_t width,uint32_t height,std::array<float,4> clear,bool loadExistingColor,VkRect2D cameraViewport) {
    auto& state=*Resources; const auto device=state.Context.GetDevice(); const auto& limits=state.Context.GetProperties().limits;
    if(!target || !width || !height || width>limits.maxFramebufferWidth || height>limits.maxFramebufferHeight || state.Draws.size()>UINT32_MAX) throw std::invalid_argument("Vulkan mesh pass requires a valid borrowed RGBA8 target within device framebuffer limits");
    if(cameraViewport.offset.x<0 || cameraViewport.offset.y<0 || !cameraViewport.extent.width || !cameraViewport.extent.height || static_cast<uint32_t>(cameraViewport.offset.x)>=width || static_cast<uint32_t>(cameraViewport.offset.y)>=height || cameraViewport.extent.width>width-static_cast<uint32_t>(cameraViewport.offset.x) || cameraViewport.extent.height>height-static_cast<uint32_t>(cameraViewport.offset.y) || cameraViewport.extent.width>limits.maxViewportDimensions[0] || cameraViewport.extent.height>limits.maxViewportDimensions[1]) throw std::invalid_argument("Vulkan camera viewport must remain inside the borrowed target and device limits");
    for(float value:clear) if(!std::isfinite(value)) throw std::invalid_argument("Vulkan mesh clear color must be finite");
    State::Frame frame{device}; frame.DepthTarget=std::make_unique<State::Depth>(state.Context,width,height);
    const std::array<VkImageView,2> attachments={target,frame.DepthTarget->View};
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; framebuffer.renderPass=state.ClearPass; framebuffer.attachmentCount=attachments.size(); framebuffer.pAttachments=attachments.data(); framebuffer.width=width; framebuffer.height=height; framebuffer.layers=1;
    VulkanContext::Require(vkCreateFramebuffer(device,&framebuffer,nullptr,&frame.Framebuffer),"vkCreateFramebuffer(mesh borrowed target)");
    std::vector<VkDescriptorSet> sets(state.Draws.size());
    if(!state.Draws.empty()) {
        std::array<uint32_t,3> counts{}; std::vector<VkDescriptorSetLayout> layouts;
        for(const auto& draw:state.Draws) {
            const auto& material=*state.Materials.at(draw.MaterialId); layouts.push_back(material.Descriptors);
            for(const auto& binding:material.Bindings) { auto& count=counts[binding.Type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ? 0 : (binding.Type==VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ? 1 : 2)]; if(count==UINT32_MAX) throw std::length_error("Vulkan mesh frame exceeds descriptor allocation limits"); ++count; }
        }
        std::vector<VkDescriptorPoolSize> sizes; const VkDescriptorType types[]={VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,VK_DESCRIPTOR_TYPE_SAMPLER};
        for(size_t index=0;index<counts.size();++index) if(counts[index]) sizes.push_back({types[index],counts[index]});
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets=static_cast<uint32_t>(sets.size()); pool.poolSizeCount=static_cast<uint32_t>(sizes.size()); pool.pPoolSizes=sizes.data();
        VulkanContext::Require(vkCreateDescriptorPool(device,&pool,nullptr,&frame.Pool),"vkCreateDescriptorPool(mesh frame)");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; allocate.descriptorPool=frame.Pool; allocate.descriptorSetCount=static_cast<uint32_t>(sets.size()); allocate.pSetLayouts=layouts.data();
        VulkanContext::Require(vkAllocateDescriptorSets(device,&allocate,sets.data()),"vkAllocateDescriptorSets(mesh frame)");
        for(size_t drawIndex=0;drawIndex<state.Draws.size();++drawIndex) {
            const auto& draw=state.Draws[drawIndex]; const auto& material=*state.Materials.at(draw.MaterialId);
            for(const auto& binding:material.Bindings) {
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet=sets[drawIndex]; write.dstBinding=binding.Slot; write.descriptorCount=1; write.descriptorType=binding.Type;
                VkDescriptorBufferInfo bufferInfo{}; VkDescriptorImageInfo imageInfo{};
                if(binding.Type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                    const auto& buffer=*std::find_if(draw.Buffers.begin(),draw.Buffers.end(),[&binding](const auto& item){return item.Slot==binding.Slot;});
                    auto uniform=std::make_unique<GpuBuffer>(state.Context,buffer.Bytes.size(),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                    std::memcpy(uniform->Map(),buffer.Bytes.data(),buffer.Bytes.size()); uniform->Unmap(); bufferInfo={uniform->Get(),0,buffer.Bytes.size()}; frame.Uniforms.push_back(std::move(uniform)); write.pBufferInfo=&bufferInfo;
                } else {
                    imageInfo=std::find_if(draw.Images.begin(),draw.Images.end(),[&binding](const auto& item){return item.Slot==binding.Slot;})->Image; write.pImageInfo=&imageInfo;
                }
                vkUpdateDescriptorSets(device,1,&write,0,nullptr);
            }
        }
    }
    const auto commands=state.Context.BeginCommands(); std::array<VkClearValue,2> values{}; std::copy(clear.begin(),clear.end(),values[0].color.float32); values[1].depthStencil={1,0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; begin.renderPass=loadExistingColor ? state.LoadPass : state.ClearPass; begin.framebuffer=frame.Framebuffer; begin.renderArea.extent={width,height}; begin.clearValueCount=values.size(); begin.pClearValues=values.data();
    vkCmdBeginRenderPass(commands,&begin,VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{static_cast<float>(cameraViewport.offset.x),static_cast<float>(cameraViewport.offset.y),static_cast<float>(cameraViewport.extent.width),static_cast<float>(cameraViewport.extent.height),0,1}; vkCmdSetViewport(commands,0,1,&viewport); vkCmdSetScissor(commands,0,1,&cameraViewport);
    for(size_t index=0;index<state.Draws.size();++index) {
        const auto& draw=state.Draws[index]; const auto& mesh=*state.Meshes.at(draw.MeshId); const auto& material=*state.Materials.at(draw.MaterialId);
        const auto vertex=mesh.Vertices->Get(); const VkDeviceSize offset=0;
        vkCmdBindPipeline(commands,VK_PIPELINE_BIND_POINT_GRAPHICS,material.Pipeline); vkCmdBindVertexBuffers(commands,0,1,&vertex,&offset); vkCmdBindIndexBuffer(commands,mesh.Indices->Get(),0,VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(commands,VK_PIPELINE_BIND_POINT_GRAPHICS,material.Layout,0,1,&sets[index],0,nullptr); vkCmdDrawIndexed(commands,draw.IndexCount,1,draw.FirstIndex,0,0);
    }
    vkCmdEndRenderPass(commands); state.Context.SubmitAndWait(commands); state.Draws.clear();
}

void VulkanMeshRenderer::ReleaseMesh(size_t mesh) {
    if(std::any_of(Resources->Draws.begin(),Resources->Draws.end(),[mesh](const auto& draw){return draw.MeshId==mesh;})) throw std::logic_error("Vulkan mesh is still referenced by queued draws");
    if(!Resources->Meshes.erase(mesh)) throw std::invalid_argument("Vulkan mesh release references an absent owner");
}
void VulkanMeshRenderer::ReleaseMaterial(size_t material) {
    if(std::any_of(Resources->Draws.begin(),Resources->Draws.end(),[material](const auto& draw){return draw.MaterialId==material;})) throw std::logic_error("Vulkan material is still referenced by queued draws");
    if(!Resources->Materials.erase(material)) throw std::invalid_argument("Vulkan material release references an absent owner");
}
bool VulkanMeshRenderer::ReferencesImage(VkImageView image) const {
    return image && std::any_of(Resources->Draws.begin(),Resources->Draws.end(),[image](const auto& draw){return std::any_of(draw.Images.begin(),draw.Images.end(),[image](const auto& binding){return binding.Image.imageView==image;});});
}
bool VulkanMeshRenderer::HasPendingDraws() const { return !Resources->Draws.empty(); }
void VulkanMeshRenderer::DiscardFrame() { Resources->Draws.clear(); }
}
