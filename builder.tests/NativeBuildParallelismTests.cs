using helengine.linux.builder;

/// <summary>Checks the real executor process boundary preserves the caller's bounded CMake concurrency.</summary>
internal static class NativeBuildParallelismTests {
    /// <summary>Records the native build command through an isolated fake CMake and restores inherited process settings afterwards.</summary>
    public static async Task RunAsync() {
        if (!OperatingSystem.IsLinux()) { return; }
        string root = Path.Combine(Environment.GetEnvironmentVariable("HELENGINE_LINUX_TEST_OUTPUT") ?? "/tmp/heldota-dota-resume/native-linux","parallelism-contract");
        string binaryRoot = Path.Combine(root,"bin");
        string buildRoot = Path.Combine(root,"native build ação");
        string record = Path.Combine(root,"cmake-arguments.txt");
        Dictionary<string,string> previous = new();
        foreach (string name in new[] {"PATH","CMAKE_BUILD_PARALLEL_LEVEL","HELENGINE_LINUX_PARALLEL_TEST_RECORD"}) {
            previous.Add(name,Environment.GetEnvironmentVariable(name));
        }
        Directory.CreateDirectory(binaryRoot);
        string cmake = Path.Combine(binaryRoot,"cmake");
        File.WriteAllText(cmake,"""
            #!/bin/sh
            printf '%s\n' "$CMAKE_BUILD_PARALLEL_LEVEL" > "$HELENGINE_LINUX_PARALLEL_TEST_RECORD"
            printf '%s\n' "$@" >> "$HELENGINE_LINUX_PARALLEL_TEST_RECORD"
            if [ "$1" = "--build" ]; then
                : > "$2/helengine_linux"
            fi
            """ + "\n");
        File.SetUnixFileMode(cmake,UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        try {
            Environment.SetEnvironmentVariable("PATH",binaryRoot + Path.PathSeparator + previous["PATH"]);
            Environment.SetEnvironmentVariable("CMAKE_BUILD_PARALLEL_LEVEL","4");
            Environment.SetEnvironmentVariable("HELENGINE_LINUX_PARALLEL_TEST_RECORD",record);
            string player = await LinuxNativeBuildExecutor.BuildAsync("/actual repository",buildRoot,"/generated core","/script code","/content","Release",1280,720,CancellationToken.None);
            string[] arguments = File.ReadAllLines(record);
            if (!arguments.SequenceEqual(new[] {"4","--build",buildRoot,"--target","helengine_linux"})) {
                throw new InvalidOperationException("Native executor overrode inherited CMAKE_BUILD_PARALLEL_LEVEL with explicit parallel arguments: " + string.Join(" | ",arguments));
            }
            if (player != Path.Combine(buildRoot,"helengine_linux")) { throw new InvalidOperationException("Native executor changed the actual player output identity."); }
        } finally {
            foreach (KeyValuePair<string,string> entry in previous) { Environment.SetEnvironmentVariable(entry.Key,entry.Value); }
            Directory.Delete(root,true);
        }
    }
}
