using helengine.baseplatform.Builders;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine;

namespace helengine.linux.builder;

/// <summary>Publishes Linux Vulkan build metadata and routes resolved cook/build requests to the Linux-owned native pipeline.</summary>
public sealed class LinuxPlatformAssetBuilder : IPlatformAssetBuilder, IPlatformLayoutTargetProvider, IShaderBackendRegistryContributor {
    /// <summary>Identifies the native Linux builder without claiming editor Play integration.</summary>
    public PlatformBuilderDescriptor Descriptor { get; } = new("helengine.linux.builder", "1.0.0", "linux",
        new EngineCompatibilityRange("1.0.0","999.0.0"),new ManifestCompatibilityRange(1,1),["linux"],["debug","release","profiler"]);
    /// <summary>Exposes immutable profile/schema metadata consumed by the editor and portable CLI.</summary>
    public PlatformDefinition Definition { get; } = LinuxPlatformDefinitionFactory.Create();
    /// <summary>Registers the engine's existing HLSL-to-SPIR-V compiler without initializing its managed Vulkan renderer or Win32 surface code.</summary>
    public void RegisterShaderBackends(ShaderBackendRegistry shaderBackendRegistry) {
        ArgumentNullException.ThrowIfNull(shaderBackendRegistry);
        shaderBackendRegistry.Register(new helengine.vulkan.VulkanShaderBackend());
    }
    /// <summary>Preserves authored shader names, constant buffers and texture dependencies in the shared binary material contract.</summary>
    public PlatformMaterialCookResult CookMaterial(PlatformMaterialCookRequest request) { return LinuxMaterialCooker.Cook(request); }
    /// <summary>Stages the resolved content and runs the native player build, preserving compiler failures and cancellation.</summary>
    public Task<PlatformBuildReport> BuildAsync(PlatformBuildRequest request, IPlatformBuildProgressReporter progressReporter, IPlatformBuildDiagnosticReporter diagnosticReporter, CancellationToken cancellationToken) {
        return LinuxBuildWorkspace.BuildAsync(request, progressReporter, diagnosticReporter, cancellationToken);
    }
    /// <summary>Returns the requested desktop framebuffer and dynamic layout policy without querying a user's monitor.</summary>
    public PlatformLayoutTarget ResolveLayoutTarget(PlatformLayoutTargetRequest request) {
        ArgumentNullException.ThrowIfNull(request);
        return new PlatformLayoutTarget(ReadDimension(request.GraphicsOptionValues,"default-width"), ReadDimension(request.GraphicsOptionValues,"default-height"),1d,false,0d,1d);
    }
    /// <summary>Rejects absent or malformed required framebuffer dimensions before content layout is cooked.</summary>
    static int ReadDimension(IReadOnlyDictionary<string,string> values, string key) {
        if (!values.TryGetValue(key,out string value) || !int.TryParse(value,out int dimension) || dimension <= 0) { throw new ArgumentException($"Linux graphics option '{key}' must be a positive integer."); }
        return dimension;
    }
}
