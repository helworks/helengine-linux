using System.Text.Json;
using helengine.baseplatform.Manifest;

namespace helengine.linux.builder;

/// <summary>Records actual packaged runtime identities after native compilation and runtime staging succeed.</summary>
public static class LinuxRuntimeManifestWriter {
    /// <summary>Serializes resolved scene metadata, loose assets and declared cooked artifacts, requiring their actual packaged files.</summary>
    public static async Task WriteAsync(string contentRoot, PlatformBuildManifest manifest, CancellationToken cancellationToken) {
        ArgumentException.ThrowIfNullOrWhiteSpace(contentRoot);
        ArgumentNullException.ThrowIfNull(manifest);
        string content = Path.GetFullPath(contentRoot);
        var scenes = manifest.Scenes.Select(scene => new {
            scene.SceneId,
            SourceIdentity = RequireRuntimeFile(content,scene.ResolvedMetadata.Single(entry => entry.Key == PlatformBuildSceneMetadataKeys.CookedRelativePath).Value)
        }).ToArray();
        var assets = manifest.LooseAssets.Select(asset => new {
            asset.AssetId,
            SourceIdentity = RequireRuntimeFile(content,ResolvePayloadIdentity(asset.SourceIdentity))
        }).ToArray();
        var artifacts = manifest.CookedArtifacts.Select(artifact => new {
            artifact.LogicalArtifactId,
            RelativePath = RequireRuntimeFile(content,artifact.RelativePath)
        }).ToArray();
        await File.WriteAllTextAsync(Path.Combine(content,"build-manifest.json"),JsonSerializer.Serialize(new {
            manifest.PlatformName, manifest.StartupSceneId, Scenes = scenes, LooseAssets = assets, CookedArtifacts = artifacts
        },new JsonSerializerOptions { WriteIndented = true }),cancellationToken);
    }

    /// <summary>Matches the existing staged payload transformation while preserving its runtime-relative cooked identity.</summary>
    static string ResolvePayloadIdentity(string sourceIdentity) {
        string relative = sourceIdentity.Replace('\\','/');
        if (Path.IsPathRooted(relative)) { relative = Path.GetFileName(relative); }
        if (relative.StartsWith("generated/",StringComparison.Ordinal)) { relative = relative.Substring("generated/".Length); }
        if (!relative.StartsWith("cooked/",StringComparison.Ordinal)) { relative = "cooked/" + relative; }
        return relative;
    }

    /// <summary>Rejects missing files and noncanonical runtime paths instead of recording authored or escaping identities.</summary>
    static string RequireRuntimeFile(string contentRoot, string relativePath) {
        ArgumentException.ThrowIfNullOrWhiteSpace(relativePath);
        string relative = relativePath.Replace('\\','/');
        if (Path.IsPathRooted(relative) || relative.Split('/').Any(segment => segment.Length == 0 || segment == "." || segment == "..")) { throw new InvalidOperationException($"Runtime manifest path is not canonical: '{relativePath}'."); }
        string file = Path.GetFullPath(Path.Combine(contentRoot,relative));
        if (!file.StartsWith(contentRoot + Path.DirectorySeparatorChar,StringComparison.Ordinal)) { throw new InvalidOperationException($"Runtime manifest path escapes package: '{relativePath}'."); }
        if (!File.Exists(file)) { throw new FileNotFoundException("Runtime manifest requires an actual packaged artifact.",file); }
        return relative;
    }
}
