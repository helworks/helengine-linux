using helengine.linux.builder;

/// <summary>Exercises platform metadata, exact process argument boundaries and native failure propagation without editor or GUI dependencies.</summary>
internal static class Program {
    /// <summary>Runs the builder contract checks and returns a failing process status on any contract violation.</summary>
    static async Task<int> Main() {
        try {
            LinuxPlatformAssetBuilder builder = new();
            if (builder.Definition.PlatformId != "linux" || builder.Definition.GraphicsProfiles.Single().ProfileId != "vulkan") {
                throw new InvalidOperationException("Linux/Vulkan metadata was not published.");
            }
            foreach (int bits in new[] {16,32,64}) { builder.Definition.CodegenProfiles.Single().NumericCapabilities.RequireSupported(new helengine.NumericPrecisionPolicy(bits,bits)); }
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
            Console.WriteLine("PASS Linux metadata policies16/32/64 Unicode-argument-boundaries profile-validation cancellation");
            return 0;
        } catch (Exception exception) { Console.Error.WriteLine(exception); return 1; }
    }
}
