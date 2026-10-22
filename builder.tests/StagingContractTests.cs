using helengine.baseplatform.Manifest;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Targets;
using helengine.linux.builder;

/// <summary>Verifies actual assets-only requests retain dependencies declared exclusively in CookedArtifacts.</summary>
internal static class StagingContractTests {
    /// <summary>Runs against a fixed temporary package root and deletes only the test-owned fixture afterwards.</summary>
    public static async Task RunAsync() {
        string root = OperatingSystem.IsWindows() ? "C:/temp/helengine/linux/package-contract" : "/tmp/helengine/linux/package-contract";
        string previous = Directory.GetCurrentDirectory();
        Directory.CreateDirectory(Path.Combine(root,"cooked","textures"));
        try {
            Directory.SetCurrentDirectory(root);
            File.WriteAllBytes("cooked/menu.hele",[1,2,3]);
            File.WriteAllBytes("cooked/textures/logo.hele",[4,5,6,7]);
            PlatformBuildManifest manifest = new(1,"project","1.0.0","1.0.0","linux","1.0.0","menu",
                [new PlatformBuildScene("menu","Menu","cooked/menu.hele",[],[])],[],
                [new PlatformBuildArtifact("cooked/textures/logo.hele","logo","hash","texture","linux")],[],[],new PlatformContainerWritePlan("linux",[]),[],PlatformBuildRuntimeFeatureManifest.Empty);
            PlatformBuildRequest request = new(manifest,[new PlatformBuildTargetVariant("linux","linux","vulkan","default")],
                [new PlatformCookProfile("default","Default",new PlatformCookProfileCapabilities("vulkan","rgba32","pcm","hele",PlatformSerializationEndianness.LittleEndian))],
                Path.Combine(root,"output"),Path.Combine(root,"work"),"debug","vulkan","default",new Dictionary<string,string>(),new Dictionary<string,string>(),new Dictionary<string,string>(),executionScope:PlatformBuildExecutionScope.AssetsOnly);
            ContractReporter reporter = new();
            await new LinuxPlatformAssetBuilder().BuildAsync(request,reporter,reporter,CancellationToken.None);
            string destination = Path.Combine(root,"work","content","cooked","textures","logo.hele");
            if (!File.Exists(destination) || !File.ReadAllBytes(destination).SequenceEqual(new byte[] {4,5,6,7})) { throw new InvalidOperationException("Assets-only Linux build omitted a CookedArtifacts-only texture dependency."); }
            File.Delete("cooked/textures/logo.hele");
            bool missingRejected = false;
            try { await new LinuxPlatformAssetBuilder().BuildAsync(request,reporter,reporter,CancellationToken.None); } catch (FileNotFoundException) { missingRejected = true; }
            if (!missingRejected) { throw new InvalidOperationException("Missing declared cooked artifact was reported as a successful assets-only build."); }
        } finally { Directory.SetCurrentDirectory(previous); Directory.Delete(root,true); }
    }
}
