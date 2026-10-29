using helengine.linux.builder;

/// <summary>Verifies publishing a cached executable records this invocation without changing source bytes or executable permissions.</summary>
internal static class NativePlayerPublicationTests {
    /// <summary>Exercises the real file publication boundary with old source timestamps and preserves missing-source failures.</summary>
    public static void Run() {
        string root = Path.Combine(Environment.GetEnvironmentVariable("HELENGINE_LINUX_TEST_OUTPUT") ?? "/tmp/heldota-dota-resume/native-linux/publication-tests","native-player-publication-contract");
        Directory.CreateDirectory(root);
        string source = Path.Combine(root,"cached-player.bin");
        string destination = Path.Combine(root,"published-player.bin");
        byte[] payload = [127,69,76,70,1,2,3];
        try {
            File.WriteAllBytes(source,payload);
            File.WriteAllBytes(destination,[9]);
            if (!OperatingSystem.IsWindows()) { File.SetUnixFileMode(source,UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute | UnixFileMode.GroupRead | UnixFileMode.GroupExecute | UnixFileMode.OtherRead | UnixFileMode.OtherExecute); }
            File.SetLastWriteTimeUtc(source,new DateTime(2020,1,2,3,4,5,DateTimeKind.Utc));
            DateTime originalSourceTime = File.GetLastWriteTimeUtc(source);
            DateTime started = DateTime.UtcNow;
            LinuxRuntimeArtifactPublication.PublishPlayer(source,destination);
            DateTime completed = DateTime.UtcNow;
            DateTime published = File.GetLastWriteTimeUtc(destination);
            if (published < started || published > completed) { throw new InvalidOperationException("Cached native player retained its compilation timestamp instead of this invocation's publication time."); }
            if (!File.ReadAllBytes(source).SequenceEqual(payload) || !File.ReadAllBytes(destination).SequenceEqual(payload) || File.GetLastWriteTimeUtc(source) != originalSourceTime) { throw new InvalidOperationException("Native publication changed source metadata or player bytes."); }
            if (!OperatingSystem.IsWindows() && File.GetUnixFileMode(source) != File.GetUnixFileMode(destination)) { throw new InvalidOperationException("Published native player lost executable permissions."); }
            File.Delete(source);
            bool missingRejected = false;
            try { LinuxRuntimeArtifactPublication.PublishPlayer(source,destination); } catch (FileNotFoundException) { missingRejected = true; }
            if (!missingRejected || !File.ReadAllBytes(destination).SequenceEqual(payload) || File.GetLastWriteTimeUtc(destination) != published) { throw new InvalidOperationException("Missing native source was accepted or altered the existing publication."); }
        } finally { Directory.Delete(root,true); }
    }
}
