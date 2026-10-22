using System.Text.Json;
using helengine.baseplatform.Builders;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;

namespace helengine.linux.builder;

/// <summary>Stages resolved content without resetting unrelated output and invokes the real Linux CMake target after ABI validation.</summary>
public static class LinuxBuildWorkspace {
    /// <summary>Validates immutable numeric contracts, stages all scene/asset entries and preserves native failure instead of returning a diagnostic host as a player.</summary>
    public static async Task<PlatformBuildReport> BuildAsync(PlatformBuildRequest request, IPlatformBuildProgressReporter progressReporter, IPlatformBuildDiagnosticReporter diagnosticReporter, CancellationToken cancellationToken) {
        ArgumentNullException.ThrowIfNull(request); ArgumentNullException.ThrowIfNull(progressReporter); ArgumentNullException.ThrowIfNull(diagnosticReporter);
        LinuxPlatformDefinitionFactory.Create().CodegenProfiles.Single().NumericCapabilities.RequireSupported(request.NumericPolicy);
        if (request.Manifest.NumericPolicy.Identity != request.NumericPolicy.Identity) { throw new InvalidOperationException("Linux build request and content manifest have different numeric policies."); }
        foreach (PlatformBuildCodeModule module in request.Manifest.CodeModules) { module.RequireNumericPolicy(request.NumericPolicy); }
        string profile = request.SelectedBuildProfileId switch {"debug" => "Debug", "release" => "Release", "profiler" => "Profiler", _ => throw new ArgumentException("Unknown Linux build profile.")};
        bool assetsOnly = request.ExecutionScope == PlatformBuildExecutionScope.AssetsOnly;
        string content = Path.GetFullPath(assetsOnly ? Path.Combine(request.WorkingRoot,"content") : request.OutputRoot);
        Directory.CreateDirectory(content);
        List<PlatformBuildItemOutcome> scenes = [];
        List<PlatformBuildItemOutcome> assets = [];
        int total = request.Manifest.Scenes.Length + request.Manifest.LooseAssets.Length + request.Manifest.CookedArtifacts.Length + request.Manifest.PlatformCookWorkItems.Length;
        int completed = 0;
        foreach (PlatformBuildScene scene in request.Manifest.Scenes) {
            cancellationToken.ThrowIfCancellationRequested(); CopyPayload(content,scene.SourceIdentity,scene.SourceIdentity);
            scenes.Add(new PlatformBuildItemOutcome(scene.SceneId,PlatformBuildItemOutcomeKind.Succeeded));
            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Payloads",scene.SceneId,++completed,total,$"Staged scene '{scene.SceneName}'."));
        }
        foreach (PlatformBuildAsset asset in request.Manifest.LooseAssets) {
            cancellationToken.ThrowIfCancellationRequested(); CopyPayload(content,asset.SourceIdentity,asset.SourceIdentity);
            assets.Add(new PlatformBuildItemOutcome(asset.AssetId,PlatformBuildItemOutcomeKind.Succeeded));
            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Payloads",asset.AssetId,++completed,total,$"Staged asset '{asset.AssetName}'."));
        }
        foreach (PlatformBuildArtifact artifact in request.Manifest.CookedArtifacts) {
            cancellationToken.ThrowIfCancellationRequested();
            CopyDeclaredArtifact(content,artifact.RelativePath);
            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Cooked Artifacts",artifact.LogicalArtifactId,++completed,total,$"Staged declared runtime artifact '{artifact.RelativePath}'."));
        }
        foreach (PlatformCookWorkItem item in request.Manifest.PlatformCookWorkItems) {
            cancellationToken.ThrowIfCancellationRequested();
            if (item.TargetPlatformId != "linux") { throw new InvalidOperationException($"Cook work item '{item.WorkItemId}' targets '{item.TargetPlatformId}', not Linux."); }
            CopyPayload(content,item.SourceAssetPath,item.OutputRelativePath);
            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Builder Assets",item.WorkItemId,++completed,total,"Staged builder-owned payload."));
        }
        Directory.CreateDirectory(request.WorkingRoot);
        await File.WriteAllTextAsync(Path.Combine(request.WorkingRoot,"linux-build-manifest.json"),JsonSerializer.Serialize(new {
            Target = "linux", NumericPolicy = request.NumericPolicy.Identity, request.Manifest.StartupSceneId,
            Scenes = request.Manifest.Scenes.Select(scene => scene.SceneId).ToArray(),
            PlayerValidated = false, NativeState = assetsOnly ? "assets-only" : "requested"
        },new JsonSerializerOptions { WriteIndented = true }),cancellationToken);
        if (!assetsOnly) {
            string repository = ResolveRepositoryRoot();
            string nativeRoot = string.IsNullOrWhiteSpace(request.NativeObjectCacheRoot) ? "/tmp/helengine/linux" : Path.GetFullPath(request.NativeObjectCacheRoot);
            string player = await LinuxNativeBuildExecutor.BuildAsync(repository,nativeRoot,request.GeneratedCoreCppRootPath,Path.Combine(Directory.GetCurrentDirectory(),"code"),content,profile,cancellationToken);
            File.Copy(player,Path.Combine(content,"helengine_linux"),true);
            CopyNativeRuntime(nativeRoot,content);
        }
        return new PlatformBuildReport(true,[],scenes.ToArray(),assets.ToArray());
    }
    /// <summary>Finds the actual sibling checkout by walking assembly ancestors, requiring the Linux CMake and dependency-lock identity.</summary>
    static string ResolveRepositoryRoot() {
        string configured = Environment.GetEnvironmentVariable("HELENGINE_LINUX_REPOSITORY_ROOT");
        if (!string.IsNullOrWhiteSpace(configured)) {
            string root = Path.GetFullPath(configured);
            if (!File.Exists(Path.Combine(root,"CMakeLists.txt")) || !File.Exists(Path.Combine(root,"dependencies.lock.json"))) { throw new DirectoryNotFoundException($"Linux repository root is invalid: '{root}'."); }
            return root;
        }
        DirectoryInfo directory = new(Path.GetDirectoryName(typeof(LinuxBuildWorkspace).Assembly.Location));
        while (directory != null) {
            if (File.Exists(Path.Combine(directory.FullName,"CMakeLists.txt")) && File.Exists(Path.Combine(directory.FullName,"dependencies.lock.json"))) { return directory.FullName; }
            directory = directory.Parent;
        }
        throw new DirectoryNotFoundException("Set HELENGINE_LINUX_REPOSITORY_ROOT to the actual helengine-linux checkout.");
    }
    /// <summary>Copies a staged source to its runtime cooked identity and rejects paths that would escape the package root.</summary>
    static void CopyPayload(string contentRoot, string sourceIdentity, string relativeIdentity) {
        string source = Path.GetFullPath(sourceIdentity.Replace('\\',Path.DirectorySeparatorChar));
        if (!File.Exists(source)) { throw new FileNotFoundException($"Required staged Linux payload is unavailable: '{source}'.",source); }
        string relative = relativeIdentity.Replace('\\','/');
        if (Path.IsPathRooted(relative)) { relative = Path.GetFileName(relative); }
        if (relative.StartsWith("generated/",StringComparison.Ordinal)) { relative = relative.Substring("generated/".Length); }
        if (!relative.StartsWith("cooked/",StringComparison.Ordinal)) { relative = "cooked/" + relative; }
        string destination = Path.GetFullPath(Path.Combine(contentRoot,relative));
        if (!destination.StartsWith(contentRoot + Path.DirectorySeparatorChar,StringComparison.Ordinal)) { throw new InvalidOperationException($"Linux payload destination escapes content root: '{relativeIdentity}'."); }
        Directory.CreateDirectory(Path.GetDirectoryName(destination));
        if (!string.Equals(source,destination,StringComparison.Ordinal)) { File.Copy(source,destination,true); }
    }
    /// <summary>Copies a graph-declared runtime dependency using its exact relative identity; absent entries fail even if stale output exists.</summary>
    static void CopyDeclaredArtifact(string contentRoot, string relativePath) {
        string relative = relativePath.Replace('\\',Path.DirectorySeparatorChar).Replace('/',Path.DirectorySeparatorChar);
        if (Path.IsPathRooted(relative)) { throw new InvalidOperationException($"Cooked artifact must have a runtime-relative identity: '{relativePath}'."); }
        string stagingRoot = Path.GetFullPath(Directory.GetCurrentDirectory());
        string source = Path.GetFullPath(Path.Combine(stagingRoot,relative));
        string destination = Path.GetFullPath(Path.Combine(contentRoot,relative));
        if (!source.StartsWith(stagingRoot + Path.DirectorySeparatorChar,StringComparison.Ordinal)
            || !destination.StartsWith(contentRoot + Path.DirectorySeparatorChar,StringComparison.Ordinal)) { throw new InvalidOperationException($"Cooked artifact escapes its declared package root: '{relativePath}'."); }
        if (!File.Exists(source)) { throw new FileNotFoundException($"Required declared Linux cooked artifact is unavailable: '{relativePath}'.",source); }
        Directory.CreateDirectory(Path.GetDirectoryName(destination));
        if (!string.Equals(source,destination,StringComparison.Ordinal)) { File.Copy(source,destination,true); }
    }
    /// <summary>Packages the required UI SPIR-V and SDL3 runtime, declaring the system Vulkan loader and C++ runtime dependencies explicitly.</summary>
    static void CopyNativeRuntime(string nativeRoot,string contentRoot) {
        Directory.CreateDirectory(Path.Combine(contentRoot,"shaders"));
        foreach (string shader in new[] {"quad.vert.spv","quad.frag.spv"}) {
            string source=Path.Combine(nativeRoot,"shaders",shader);
            if (!File.Exists(source)) { throw new FileNotFoundException("Required Linux UI shader was not produced.",source); }
            File.Copy(source,Path.Combine(contentRoot,"shaders",shader),true);
        }
        string prefix=Environment.GetEnvironmentVariable("HELENGINE_LINUX_NATIVE_PREFIX");
        if (string.IsNullOrWhiteSpace(prefix)) { throw new InvalidOperationException("HELENGINE_LINUX_NATIVE_PREFIX is required to package the locked SDL3 runtime."); }
        string sdl=Path.Combine(prefix,"lib","x86_64-linux-gnu","libSDL3.so.0");
        if (!File.Exists(sdl)) { throw new FileNotFoundException("The locked SDL3 runtime is missing from the native tool prefix.",sdl); }
        Directory.CreateDirectory(Path.Combine(contentRoot,"lib")); File.Copy(sdl,Path.Combine(contentRoot,"lib","libSDL3.so.0"),true);
        string license=Path.Combine(prefix,"share","doc","libsdl3-0","copyright");
        if (!File.Exists(license)) { throw new FileNotFoundException("The locked SDL3 package must include its redistribution license.",license); }
        Directory.CreateDirectory(Path.Combine(contentRoot,"licenses")); File.Copy(license,Path.Combine(contentRoot,"licenses","SDL3.txt"),true);
        File.WriteAllText(Path.Combine(contentRoot,"linux-runtime-dependencies.json"),JsonSerializer.Serialize(new {
            VulkanMinimumApi="1.2", Bundled=new[] {"lib/libSDL3.so.0","licenses/SDL3.txt","shaders/quad.vert.spv","shaders/quad.frag.spv"},
            System=new[] {"libvulkan.so.1","libstdc++.so.6","libgcc_s.so.1","libc.so.6","libm.so.6"}
        },new JsonSerializerOptions {WriteIndented=true}));
    }
}
