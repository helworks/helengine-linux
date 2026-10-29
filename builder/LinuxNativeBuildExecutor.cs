using System.Diagnostics;

namespace helengine.linux.builder;

/// <summary>Invokes CMake/Ninja directly with structured arguments so spaces, Unicode, failures and cancellation survive the host boundary.</summary>
public static class LinuxNativeBuildExecutor {
    /// <summary>Uses an explicit absolute fixed build root when requested, otherwise preserving the normal per-request cache location.</summary>
    public static string ResolveBuildRoot(string requestedRoot) {
        string configured = Environment.GetEnvironmentVariable("HELENGINE_LINUX_NATIVE_BUILD_ROOT");
        if (configured != null) {
            if (string.IsNullOrWhiteSpace(configured) || !Path.IsPathFullyQualified(configured)) { throw new ArgumentException("HELENGINE_LINUX_NATIVE_BUILD_ROOT must be an absolute build directory."); }
            return Path.GetFullPath(configured);
        }
        return string.IsNullOrWhiteSpace(requestedRoot) ? "/tmp/helengine/linux" : Path.GetFullPath(requestedRoot);
    }
    /// <summary>Creates an inspectable configure request without composing shell code or quoting arguments manually.</summary>
    public static ProcessStartInfo CreateConfigureCommand(string repositoryRoot, string buildRoot, string coreRoot, string codeRoot, string contentRoot, string profile, int width=1280, int height=720) {
        return CreateConfigureCommand(repositoryRoot,buildRoot,coreRoot,codeRoot,contentRoot,profile,width,height,LinuxMeshBatchingOptions.FromGraphicsOptions(new Dictionary<string,string>()));
    }
    /// <summary>Forwards validated graphics selections to CMake using exact structured arguments.</summary>
    public static ProcessStartInfo CreateConfigureCommand(string repositoryRoot, string buildRoot, string coreRoot, string codeRoot, string contentRoot, string profile, int width, int height, LinuxMeshBatchingOptions batching) {
        ArgumentNullException.ThrowIfNull(batching);
        if (profile != "Debug" && profile != "Release" && profile != "Profiler") { throw new ArgumentException("Native Linux profile must be Debug, Release, or Profiler.",nameof(profile)); }
        if (width<=0 || height<=0) { throw new ArgumentOutOfRangeException(nameof(width),"Resolved initial framebuffer dimensions must be positive."); }
        foreach (string path in new[] {repositoryRoot,buildRoot,coreRoot,codeRoot,contentRoot}) { ArgumentException.ThrowIfNullOrWhiteSpace(path); }
        ProcessStartInfo command = new("cmake") { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string argument in new[] {"-S",repositoryRoot,"-B",buildRoot,"-G","Ninja","-DHELENGINE_LINUX_BUILD_PLAYER=ON",
            $"-DHELENGINE_CORE_CPP_ROOT={coreRoot}",$"-DHELENGINE_CODE_ROOT={codeRoot}",$"-DHELENGINE_CONTENT_ROOT={contentRoot}",$"-DHELENGINE_NATIVE_PROFILE={profile}"}) { command.ArgumentList.Add(argument); }
        command.ArgumentList.Add($"-DHELENGINE_LINUX_INITIAL_WIDTH={width}"); command.ArgumentList.Add($"-DHELENGINE_LINUX_INITIAL_HEIGHT={height}");
        command.ArgumentList.Add("-DHELENGINE_LINUX_STATIC_MESH_BATCHING=" + (batching.StaticEnabled ? "ON" : "OFF"));
        command.ArgumentList.Add($"-DHELENGINE_LINUX_STATIC_MESH_BATCHING_BUDGET_MIB={batching.StaticBudgetMiB}");
        command.ArgumentList.Add("-DHELENGINE_LINUX_DYNAMIC_MESH_BATCHING=" + (batching.DynamicEnabled ? "ON" : "OFF"));
        command.ArgumentList.Add($"-DHELENGINE_LINUX_DYNAMIC_MESH_BATCHING_MAX_VERTICES={batching.DynamicMaxVertices}");
        command.ArgumentList.Add($"-DHELENGINE_LINUX_DYNAMIC_MESH_BATCHING_BUDGET_MIB={batching.DynamicBudgetMiB}");
        string vulkanRoot = Environment.GetEnvironmentVariable("HELENGINE_VULKAN_ROOT");
        if (!string.IsNullOrWhiteSpace(vulkanRoot)) { command.ArgumentList.Add($"-DHELENGINE_VULKAN_ROOT={Path.GetFullPath(vulkanRoot)}"); }
        string nativePrefix = Environment.GetEnvironmentVariable("HELENGINE_LINUX_NATIVE_PREFIX");
        if (!string.IsNullOrWhiteSpace(nativePrefix)) {
            nativePrefix = Path.GetFullPath(nativePrefix);
            command.ArgumentList.Add($"-DCMAKE_PREFIX_PATH={nativePrefix}");
            command.ArgumentList.Add($"-DVulkan_INCLUDE_DIR={Path.Combine(nativePrefix,"include")}");
            command.ArgumentList.Add("-DVulkan_LIBRARY=/lib/x86_64-linux-gnu/libvulkan.so.1");
            command.ArgumentList.Add($"-DHELENGINE_GLSLANG_VALIDATOR={Path.Combine(nativePrefix,"bin","glslangValidator")}");
        }
        return command;
    }
    /// <summary>Builds only the real player target and rejects missing output after a successful compiler process.</summary>
    public static async Task<string> BuildAsync(string repositoryRoot, string buildRoot, string coreRoot, string codeRoot, string contentRoot, string profile, int width, int height, CancellationToken cancellationToken) {
        return await BuildAsync(repositoryRoot,buildRoot,coreRoot,codeRoot,contentRoot,profile,width,height,LinuxMeshBatchingOptions.FromGraphicsOptions(new Dictionary<string,string>()),cancellationToken);
    }
    /// <summary>Compiles the real player with the exact validated editor batching selections.</summary>
    public static async Task<string> BuildAsync(string repositoryRoot, string buildRoot, string coreRoot, string codeRoot, string contentRoot, string profile, int width, int height, LinuxMeshBatchingOptions batching, CancellationToken cancellationToken) {
        ArgumentNullException.ThrowIfNull(batching);
        if (!OperatingSystem.IsLinux()) { throw new PlatformNotSupportedException("Native Linux player compilation requires the Linux host checkout."); }
        Directory.CreateDirectory(buildRoot);
        await RunAsync(CreateConfigureCommand(repositoryRoot,buildRoot,coreRoot,codeRoot,contentRoot,profile,width,height,batching),cancellationToken);
        ProcessStartInfo build = new("cmake") { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string argument in new[] {"--build",buildRoot,"--target","helengine_linux"}) { build.ArgumentList.Add(argument); }
        await RunAsync(build,cancellationToken);
        string player = Path.Combine(buildRoot,"helengine_linux");
        if (!File.Exists(player)) { throw new FileNotFoundException("Native Linux compiler returned success without producing the real player.",player); }
        return player;
    }
    /// <summary>Drains both output streams concurrently, kills the child process tree on cancellation and includes actual exit status in failure diagnostics.</summary>
    public static async Task RunAsync(ProcessStartInfo command, CancellationToken cancellationToken) {
        cancellationToken.ThrowIfCancellationRequested();
        using Process process = new() { StartInfo = command };
        if (!process.Start()) { throw new InvalidOperationException($"Could not start '{command.FileName}'."); }
        Task<string> stdout = process.StandardOutput.ReadToEndAsync();
        Task<string> stderr = process.StandardError.ReadToEndAsync();
        try { await process.WaitForExitAsync(cancellationToken); }
        catch (OperationCanceledException) {
            if (!process.HasExited) { process.Kill(entireProcessTree:true); }
            await process.WaitForExitAsync();
            await Task.WhenAll(stdout,stderr);
            throw;
        }
        string output = await stdout;
        string errors = await stderr;
        if (process.ExitCode != 0) { throw new InvalidOperationException($"'{command.FileName}' exited with code {process.ExitCode}.\n{output}\n{errors}"); }
    }
}
