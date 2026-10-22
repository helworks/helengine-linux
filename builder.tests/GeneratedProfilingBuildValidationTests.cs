using helengine.baseplatform.Builders;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Targets;

/// <summary>Requires unsupported generated profiling/profile combinations to fail before native work or output staging.</summary>
internal sealed class GeneratedProfilingBuildValidationTests : IPlatformBuildProgressReporter, IPlatformBuildDiagnosticReporter {
    /// <summary>Exercises real full-build requests through the public builder entry point without launching compilers or a player.</summary>
    public static void Run(IPlatformAssetBuilder builder) {
        string target = builder.Definition.PlatformId;
        if (target != "linux" && target != "macos" && target != "ios") { throw new InvalidOperationException("Unknown test target."); }
        string root = Path.GetFullPath(Path.Combine(OperatingSystem.IsWindows() ? "C:/temp/helengine" : "/tmp/helengine", target, "profiling-policy-test"));
        if (Directory.Exists(root)) { throw new InvalidOperationException("Profiling policy test requires its fixed output path to be absent: " + root); }
        GeneratedProfilingBuildValidationTests reporter = new();
        try {
            foreach (string profile in new[] { "debug", "release" }) {
                PlatformBuildManifest manifest = new(1, "project", "1.0.0", "1.0.0", target, "1.0.0", "", [], [], [], [], [], new(target + "-loose-files", []), [], PlatformBuildRuntimeFeatureManifest.Empty);
                PlatformBuildRequest request = new(manifest, [new(target, target, builder.Definition.GraphicsProfiles[0].ProfileId, "default")],
                    [new("default", "Default", new(builder.Definition.GraphicsProfiles[0].ProfileId, "rgba32", "pcm", "hele", PlatformSerializationEndianness.LittleEndian))],
                    Path.Combine(root, "output"), Path.Combine(root, "work"), profile, builder.Definition.GraphicsProfiles[0].ProfileId, "default",
                    new Dictionary<string,string>(), new Dictionary<string,string>(), new Dictionary<string,string> { ["codegen-generated-function-profiling"] = "true" });
                Exception failure = null;
                try { builder.BuildAsync(request, reporter, reporter, CancellationToken.None).GetAwaiter().GetResult(); }
                catch (Exception error) { failure = error; }
                if (failure is not NotSupportedException || !failure.Message.Contains(target, StringComparison.OrdinalIgnoreCase)
                    || !failure.Message.Contains(profile, StringComparison.OrdinalIgnoreCase)
                    || !failure.Message.Contains("codegen-generated-function-profiling", StringComparison.Ordinal)
                    || !failure.Message.Contains("profiler", StringComparison.OrdinalIgnoreCase)) {
                    throw new InvalidOperationException("Generated profiling must reject native " + target + "/" + profile + " contextually before staging.", failure);
                }
                if (Directory.Exists(root)) { throw new InvalidOperationException("Unsupported profiling started output staging before validation."); }
            }
        } finally {
            string expected = Path.GetFullPath(Path.Combine(OperatingSystem.IsWindows() ? "C:/temp/helengine" : "/tmp/helengine", target, "profiling-policy-test"));
            if (root != expected) { throw new InvalidOperationException("Refusing to clean an unexpected profiling test path."); }
            if (Directory.Exists(root)) { Directory.Delete(root, true); }
        }
    }
    /// <summary>Consumes progress only when a regression incorrectly starts the build pipeline.</summary>
    public void Report(PlatformBuildProgressUpdate update) { }
    /// <summary>Exposes native diagnostics rather than masking an unexpected build attempt.</summary>
    public void Report(PlatformBuildDiagnostic diagnostic) { throw new InvalidOperationException(diagnostic.Message); }
}
