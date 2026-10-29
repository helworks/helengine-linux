using helengine.baseplatform.Builders;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;

/// <summary>Requires invalid renderer selections to fail at the public build boundary before staging any files.</summary>
internal sealed class MeshBatchingBuildValidationTests : IPlatformBuildProgressReporter, IPlatformBuildDiagnosticReporter {
    /// <summary>Exercises actual Linux workspace requests with malformed flags and limits without launching native tools.</summary>
    public static void Run() {
        string root = Path.Combine(Environment.GetEnvironmentVariable("HELENGINE_LINUX_TEST_OUTPUT") ?? "/tmp/heldota-dota-resume/native-linux", "batching-invalid-request");
        if (Directory.Exists(root)) throw new InvalidOperationException("Invalid batching build fixture must start absent.");
        foreach (string key in new[] { "static-mesh-batching-enabled", "static-mesh-batching-budget-mib", "dynamic-mesh-batching-enabled", "dynamic-mesh-batching-max-vertices", "dynamic-mesh-batching-budget-mib" }) {
            PlatformBuildManifest manifest = new(1,"project","1.0.0","1.0.0","linux","1.0.0","",[],[],[],[],[],new("linux-loose-files",[]),[],PlatformBuildRuntimeFeatureManifest.Empty);
            PlatformBuildRequest request = new(manifest,[new("linux","Linux","vulkan","default")],
                [new("default","Default",new("vulkan","rgba32","pcm","hele",PlatformSerializationEndianness.LittleEndian))],
                Path.Combine(root,"output"),Path.Combine(root,"work"),"release","vulkan","default",
                new Dictionary<string,string>(),new Dictionary<string,string> { [key] = "invalid", ["default-width"] = "1280", ["default-height"] = "720" },new Dictionary<string,string>());
            bool rejected = false;
            MeshBatchingBuildValidationTests reporter = new();
            try { helengine.linux.builder.LinuxBuildWorkspace.BuildAsync(request,reporter,reporter,CancellationToken.None).GetAwaiter().GetResult(); }
            catch (ArgumentException error) { rejected = error.Message.Contains(key,StringComparison.Ordinal); }
            if (!rejected || Directory.Exists(root)) throw new InvalidOperationException("Invalid batching settings started staging or lost contextual failure: " + key);
        }
        Console.WriteLine("PASS actual build request rejects invalid batching settings before any writes");
    }
    /// <summary>Rejects progress because invalid selections must fail before the first staging operation.</summary>
    public void Report(PlatformBuildProgressUpdate update) { throw new InvalidOperationException("Invalid batching build started progress."); }
    /// <summary>Preserves unexpected build diagnostics as a test failure.</summary>
    public void Report(PlatformBuildDiagnostic diagnostic) { throw new InvalidOperationException(diagnostic.Message); }
}
