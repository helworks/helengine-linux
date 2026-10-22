using helengine.linux.builder;

/// <summary>Exercises platform metadata, exact process argument boundaries and native failure propagation without editor or GUI dependencies.</summary>
internal static class Program {
    /// <summary>Runs the builder contract checks and returns a failing process status on any contract violation.</summary>
    static async Task<int> Main() {
        try {
            LinuxPlatformAssetBuilder builder = new();
            if (!builder.Definition.RuntimeGenerationContract.PortableInputPreprocessorSymbols.Contains("DESKTOP_PLATFORM")) { throw new InvalidOperationException("Linux generated input must retain the portable desktop keyboard contract."); }
            helengine.ShaderBackendRegistry shaderBackends = new();
            builder.RegisterShaderBackends(shaderBackends);
            if (!shaderBackends.ContainsTarget(helengine.ShaderCompileTarget.Vulkan)) { throw new InvalidOperationException("Linux builder must contribute the real Vulkan SPIR-V shader compiler."); }
            if (builder.Definition.PlatformId != "linux" || builder.Definition.GraphicsProfiles.Single().ProfileId != "vulkan") {
                throw new InvalidOperationException("Linux/Vulkan metadata was not published.");
            }
            foreach (int bits in new[] {16,32,64}) { builder.Definition.CodegenProfiles.Single().NumericCapabilities.RequireSupported(new helengine.NumericPrecisionPolicy(bits,bits)); }
            bool unsupportedMaterialPolicyRejected = false;
            try {
                builder.CookMaterial(new helengine.baseplatform.Requests.PlatformMaterialCookRequest("material","material.hemat","linux","debug","vulkan","standard-shader",new Dictionary<string,string> {
                    ["shader-asset-id"]="shader",["vertex-program"]="VS",["pixel-program"]="PS",["variant"]="default"
                },new helengine.NumericPrecisionPolicy(16,32)));
            } catch (NotSupportedException) { unsupportedMaterialPolicyRejected = true; }
            if (!unsupportedMaterialPolicyRejected) { throw new InvalidOperationException("Public Linux material cook accepted an unadvertised numeric policy."); }
            var command = LinuxNativeBuildExecutor.CreateConfigureCommand("/real checkout/ação", "/tmp/helengine/linux", "/generated core/á", "/script code/ü", "/content/字", "Profiler");
            if (command.UseShellExecute || !command.ArgumentList.Contains("-DHELENGINE_CORE_CPP_ROOT=/generated core/á") || !command.ArgumentList.Contains("-DHELENGINE_NATIVE_PROFILE=Profiler")) {
                throw new InvalidOperationException("CMake paths lost exact Unicode argument boundaries.");
            }
            bool rejected = false;
            try { LinuxNativeBuildExecutor.CreateConfigureCommand("/a","/b","/c","/d","/e","Unknown"); } catch (ArgumentException) { rejected = true; }
            if (!rejected) { throw new InvalidOperationException("Unknown native profile was accepted."); }
            using CancellationTokenSource cancelled = new(); cancelled.Cancel();
            bool cancellationPreserved = false;
            try { await LinuxNativeBuildExecutor.RunAsync(command,cancelled.Token); } catch (OperationCanceledException) { cancellationPreserved = true; }
            if (!cancellationPreserved) { throw new InvalidOperationException("Cancelled native build started unexpectedly."); }
            await StagingContractTests.RunAsync();
            Console.WriteLine("PASS Linux metadata policies16/32/64 Unicode-argument-boundaries profile-validation cancellation");
            return 0;
        } catch (Exception exception) { Console.Error.WriteLine(exception); return 1; }
    }
}
