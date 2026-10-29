namespace helengine.linux.builder;

/// <summary>Publishes the native player independently of whether the compiler reused an existing executable.</summary>
public static class LinuxRuntimeArtifactPublication {
    /// <summary>Copies the player with its existing bytes and permissions, then records this invocation as its publication time; copy failures remain fatal.</summary>
    public static void PublishPlayer(string source, string destination) {
        File.Copy(source,destination,true);
        File.SetLastWriteTimeUtc(destination,DateTime.UtcNow);
    }
}
