using System.Diagnostics;

namespace helengine.linux.builder;

/// <summary>Invokes CMake/Ninja directly with structured arguments so spaces, Unicode, failures and cancellation survive the host boundary.</summary>
public static class LinuxNativeBuildExecutor {
    /// <summary>Creates an inspectable configure request without composing shell code or quoting arguments manually.</summary>
    public static ProcessStartInfo CreateConfigureCommand(string repositoryRoot, string buildRoot, string coreRoot, string codeRoot, string contentRoot, string profile) {
        if (profile != "Debug" && profile != "Release" && profile != "Profiler") { throw new ArgumentException("Native Linux profile must be Debug, Release, or Profiler.",nameof(profile)); }
        foreach (string path in new[] {repositoryRoot,buildRoot,coreRoot,codeRoot,contentRoot}) { ArgumentException.ThrowIfNullOrWhiteSpace(path); }
        ProcessStartInfo command = new("cmake") { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string argument in new[] {"-S",repositoryRoot,"-B",buildRoot,"-G","Ninja","-DHELENGINE_LINUX_BUILD_PLAYER=ON",
            $"-DHELENGINE_CORE_CPP_ROOT={coreRoot}",$"-DHELENGINE_CODE_ROOT={codeRoot}",$"-DHELENGINE_CONTENT_ROOT={contentRoot}",$"-DHELENGINE_NATIVE_PROFILE={profile}"}) { command.ArgumentList.Add(argument); }
        return command;
    }
    /// <summary>Builds only the real player target and rejects missing output after a successful compiler process.</summary>
    public static async Task<string> BuildAsync(string repositoryRoot, string buildRoot, string coreRoot, string codeRoot, string contentRoot, string profile, CancellationToken cancellationToken) {
        if (!OperatingSystem.IsLinux()) { throw new PlatformNotSupportedException("Native Linux player compilation requires the Linux host checkout."); }
        Directory.CreateDirectory(buildRoot);
        await RunAsync(CreateConfigureCommand(repositoryRoot,buildRoot,coreRoot,codeRoot,contentRoot,profile),cancellationToken);
        ProcessStartInfo build = new("cmake") { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (string argument in new[] {"--build",buildRoot,"--target","helengine_linux","--parallel"}) { build.ArgumentList.Add(argument); }
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
