using cs2.core;
using cs2.cpp;
using helengine.baseplatform.Definitions;

/// <summary>Checks native profile metadata through the actual converter's prefix resolver and scope emitter.</summary>
internal static class GeneratedProfilingMetadataTests {
    /// <summary>Requires both shared and profiler-specific defaults to select maintained engine owners without matching neighboring types.</summary>
    public static void Run(PlatformDefinition definition, [System.Runtime.CompilerServices.CallerFilePath] string sourceFile = "") {
        PlatformCodegenProfileDefinition codegen = definition.CodegenProfiles.Single();
        string key = CPPCodegenOptionNames.GeneratedFunctionProfilingMaintainedSymbolPrefixes;
        string shared = codegen.Settings.Single(setting => setting.SettingId == key).DefaultValue;
        string profiler = definition.BuildProfiles.Single(profile => profile.ProfileId == "profiler").CodegenSettingDefaultValues[key];
        VerifyScopes(shared, sourceFile);
        VerifyScopes(profiler, sourceFile);
    }

    /// <summary>Exercises semicolon parsing and qualified maintained-symbol matching with the real generated profiling manifest.</summary>
    static void VerifyScopes(string configuredPrefixes, string sourceFile) {
        CPPConversionOptions options = CPPConversionOptions.CreateDefault();
        options.PlatformOptionValues = new Dictionary<string, string> {
            [CPPCodegenOptionNames.GeneratedFunctionProfiling] = "true",
            [CPPCodegenOptionNames.GeneratedFunctionProfilingMaintainedSymbolPrefixes] = configuredPrefixes
        };
        IReadOnlyList<string> prefixes = CPPGeneratedFunctionProfilingOptionResolver.ResolveMaintainedSymbolPrefixes(options);
        CPPGeneratedFunctionProfilingManifest manifest = new();
        CPPGeneratedFunctionProfilingScopeEmitter emitter = new(manifest, prefixes);
        using StringWriter source = new();
        string[] selected = [
            typeof(helengine.Core).FullName + ".Draw()",
            typeof(helengine.SceneManager).FullName + ".CommitPendingOperationsAtFrameBoundary()",
            typeof(helengine.RenderCommandListBuilder2D).FullName + ".Build()"
        ];
        foreach (string symbol in selected) {
            emitter.Write(source, "scope-contract.cpp", new ConversionSourceLocation("helengine.core", symbol, sourceFile, 1));
        }
        emitter.Write(source, "scope-contract.cpp", new ConversionSourceLocation("helengine.core", typeof(helengine.CoreInitializationOptions).FullName + ".Normalize()", sourceFile, 1));
        if (manifest.Entries.Count != 3 || !manifest.Entries.Select(scope => scope.SourceLocation.MaintainedSymbol).SequenceEqual(selected)) {
            throw new InvalidOperationException("Native profiling metadata must select exactly the maintained Core, SceneManager and RenderCommandListBuilder2D owners through the converter contract; resolved prefixes: " + string.Join(" | ", prefixes));
        }
        if (!source.ToString().Contains("tracy::ScopedZone", StringComparison.Ordinal)) {
            throw new InvalidOperationException("Selected native profile metadata emitted no actual generated scopes.");
        }
    }
}
