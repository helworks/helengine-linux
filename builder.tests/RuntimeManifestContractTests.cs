using System.Text.Json;
using helengine.baseplatform.Manifest;
using helengine.linux.builder;

/// <summary>Verifies runtime identities come from resolved metadata and native root overrides remain explicit.</summary>
internal static class RuntimeManifestContractTests {
    /// <summary>Validates runtime paths using test-owned cooked files, without creating a native player substitute.</summary>
    public static async Task RunAsync() {
        string root = Path.Combine(Environment.GetEnvironmentVariable("HELENGINE_LINUX_TEST_OUTPUT") ?? "/tmp/helengine/linux", "runtime-manifest-contract");
        Directory.CreateDirectory(Path.Combine(root,"cooked","scenes"));
        Directory.CreateDirectory(Path.Combine(root,"cooked","textures"));
        string previousOverride = Environment.GetEnvironmentVariable("HELENGINE_LINUX_NATIVE_BUILD_ROOT");
        try {
            File.WriteAllBytes(Path.Combine(root,"cooked","scenes","actual.hele"),[1,2,3]);
            File.WriteAllBytes(Path.Combine(root,"cooked","textures","actual.hele"),[4,5,6]);
            PlatformBuildManifest manifest = new(1,"project","1.0.0","1.0.0","linux","1.0.0","scene-id",
                [new("scene-id","Scene","Scenes/Author.helen",[],[new(PlatformBuildSceneMetadataKeys.CookedRelativePath,"cooked/scenes/actual.hele")])],
                [new("texture-id","Texture","generated/cooked/textures/actual.hele",new("texture-id","source"),[])],
                [new("cooked/textures/actual.hele","texture-id","hash","texture","linux")],[],[],new("linux",[]),[],PlatformBuildRuntimeFeatureManifest.Empty);
            await LinuxRuntimeManifestWriter.WriteAsync(root,manifest,CancellationToken.None);
            using JsonDocument document = JsonDocument.Parse(File.ReadAllText(Path.Combine(root,"build-manifest.json")));
            JsonElement value = document.RootElement;
            if (value.GetProperty("PlatformName").GetString()!="linux" || value.GetProperty("StartupSceneId").GetString()!="scene-id"
                || value.GetProperty("Scenes")[0].GetProperty("SourceIdentity").GetString()!="cooked/scenes/actual.hele"
                || value.GetProperty("LooseAssets")[0].GetProperty("SourceIdentity").GetString()!="cooked/textures/actual.hele"
                || value.GetProperty("CookedArtifacts")[0].GetProperty("RelativePath").GetString()!="cooked/textures/actual.hele") { throw new InvalidOperationException("Runtime manifest substituted authored paths or lost artifact entries."); }
            Environment.SetEnvironmentVariable("HELENGINE_LINUX_NATIVE_BUILD_ROOT",root);
            if (LinuxNativeBuildExecutor.ResolveBuildRoot("/ignored/cache")!=Path.GetFullPath(root)) { throw new InvalidOperationException("Explicit native build root was not honored exactly."); }
            Environment.SetEnvironmentVariable("HELENGINE_LINUX_NATIVE_BUILD_ROOT","relative/native");
            bool rejected = false;
            try { LinuxNativeBuildExecutor.ResolveBuildRoot("/ignored/cache"); } catch (ArgumentException) { rejected=true; }
            if (!rejected) { throw new InvalidOperationException("Relative native override was silently accepted."); }
            File.Delete(Path.Combine(root,"cooked","scenes","actual.hele"));
            rejected=false;
            try { await LinuxRuntimeManifestWriter.WriteAsync(root,manifest,CancellationToken.None); } catch (FileNotFoundException) { rejected=true; }
            if (!rejected) { throw new InvalidOperationException("Runtime manifest accepted a missing cooked scene."); }
        } finally { Environment.SetEnvironmentVariable("HELENGINE_LINUX_NATIVE_BUILD_ROOT",previousOverride); Directory.Delete(root,true); }
    }
}
