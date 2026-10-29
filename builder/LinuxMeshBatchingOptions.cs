using System.Globalization;
using helengine.baseplatform.Definitions;

namespace helengine.linux.builder;

/// <summary>Resolves editor graphics selections into the bounded native mesh-batching build contract.</summary>
public sealed class LinuxMeshBatchingOptions {
    /// <summary>Enables renderer-owned persistent batches for entities explicitly marked Static.</summary>
    public bool StaticEnabled { get; }
    /// <summary>Bounds additional resident static batch geometry, expressed in binary mebibytes.</summary>
    public int StaticBudgetMiB { get; }
    /// <summary>Enables per-frame batches rebuilt from current small-mesh transforms.</summary>
    public bool DynamicEnabled { get; }
    /// <summary>Bounds referenced vertices per eligible dynamic input.</summary>
    public int DynamicMaxVertices { get; }
    /// <summary>Bounds reusable transient geometry, expressed in binary mebibytes.</summary>
    public int DynamicBudgetMiB { get; }

    /// <summary>Creates an immutable validated selection without depending on the process environment.</summary>
    LinuxMeshBatchingOptions(bool staticEnabled, int staticBudgetMiB, bool dynamicEnabled, int dynamicMaxVertices, int dynamicBudgetMiB) {
        StaticEnabled = staticEnabled; StaticBudgetMiB = staticBudgetMiB;
        DynamicEnabled = dynamicEnabled; DynamicMaxVertices = dynamicMaxVertices; DynamicBudgetMiB = dynamicBudgetMiB;
    }

    /// <summary>Publishes the same typed controls and defaults used when older projects have no explicit selections.</summary>
    public static PlatformSettingDefinition[] CreateSettingDefinitions() => [
        new("static-mesh-batching-enabled", "Static Mesh Batching", PlatformSettingKind.Boolean, "false", true, []),
        new("static-mesh-batching-budget-mib", "Static Mesh Batching Budget (MiB)", PlatformSettingKind.Text, "256", true, []),
        new("dynamic-mesh-batching-enabled", "Dynamic Mesh Batching", PlatformSettingKind.Boolean, "false", true, []),
        new("dynamic-mesh-batching-max-vertices", "Dynamic Mesh Batching Maximum Vertices", PlatformSettingKind.Text, "1024", true, []),
        new("dynamic-mesh-batching-budget-mib", "Dynamic Mesh Batching Budget (MiB)", PlatformSettingKind.Text, "4", true, [])
    ];

    /// <summary>Requires valid explicit values and resolves absent keys through the published platform defaults.</summary>
    public static LinuxMeshBatchingOptions FromGraphicsOptions(IReadOnlyDictionary<string,string> values) {
        ArgumentNullException.ThrowIfNull(values);
        Dictionary<string,string> resolved = CreateSettingDefinitions().ToDictionary(setting => setting.SettingId, setting => values.TryGetValue(setting.SettingId, out string value) ? value : setting.DefaultValue, StringComparer.Ordinal);
        return new(ReadBoolean(resolved,"static-mesh-batching-enabled"), ReadPositiveInteger(resolved,"static-mesh-batching-budget-mib"),
            ReadBoolean(resolved,"dynamic-mesh-batching-enabled"), ReadPositiveInteger(resolved,"dynamic-mesh-batching-max-vertices"), ReadPositiveInteger(resolved,"dynamic-mesh-batching-budget-mib"));
    }

    /// <summary>Rejects misspelled boolean selections instead of silently disabling a requested feature.</summary>
    static bool ReadBoolean(IReadOnlyDictionary<string,string> values, string key) {
        if (!bool.TryParse(values[key], out bool value) || values[key] != values[key].Trim()) throw new ArgumentException($"Linux graphics option '{key}' must be true or false.");
        return value;
    }

    /// <summary>Parses positive decimal limits without culture, signs, whitespace, fractions or integer overflow.</summary>
    static int ReadPositiveInteger(IReadOnlyDictionary<string,string> values, string key) {
        if (!int.TryParse(values[key], NumberStyles.None, CultureInfo.InvariantCulture, out int value) || value <= 0) throw new ArgumentException($"Linux graphics option '{key}' must be a positive decimal Int32 integer.");
        return value;
    }
}
