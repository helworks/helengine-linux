using helengine.linux.builder;

/// <summary>Exercises typed platform controls, strict limits and the real native argument boundary.</summary>
internal static class MeshBatchingSettingsTests {
    /// <summary>Rejects a broken build contract without relying on framework-specific assertions.</summary>
    static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    /// <summary>Checks that editor-discovered defaults and native configuration select the same batching policy.</summary>
    public static void Run() {
        var settings = LinuxPlatformDefinitionFactory.Create().GraphicsProfiles.Single().Settings;
        var expected = new Dictionary<string,string> {
            ["static-mesh-batching-enabled"] = "false", ["static-mesh-batching-budget-mib"] = "256",
            ["dynamic-mesh-batching-enabled"] = "false", ["dynamic-mesh-batching-max-vertices"] = "1024",
            ["dynamic-mesh-batching-budget-mib"] = "4"
        };
        foreach (var item in expected) Require(settings.Single(setting => setting.SettingId == item.Key).DefaultValue == item.Value,"Editor metadata lost batching default " + item.Key);
        LinuxMeshBatchingOptions defaults = LinuxMeshBatchingOptions.FromGraphicsOptions(new Dictionary<string,string>());
        Require(!defaults.StaticEnabled && !defaults.DynamicEnabled && defaults.StaticBudgetMiB == 256 && defaults.DynamicBudgetMiB == 4 && defaults.DynamicMaxVertices == 1024,"Missing selections must resolve the published defaults.");
        foreach (bool staticEnabled in new[] {false,true}) foreach (bool dynamicEnabled in new[] {false,true}) {
            var values = new Dictionary<string,string> {
                ["static-mesh-batching-enabled"] = staticEnabled.ToString(), ["dynamic-mesh-batching-enabled"] = dynamicEnabled.ToString(),
                ["static-mesh-batching-budget-mib"] = "17", ["dynamic-mesh-batching-max-vertices"] = "73", ["dynamic-mesh-batching-budget-mib"] = "3"
            };
            LinuxMeshBatchingOptions options = LinuxMeshBatchingOptions.FromGraphicsOptions(values);
            Require(options.StaticEnabled == staticEnabled && options.DynamicEnabled == dynamicEnabled,"Independent switches were collapsed.");
            var command = LinuxNativeBuildExecutor.CreateConfigureCommand("/repo ação","/build","/core","/code","/content","Release",1920,1080,options);
            string[] required = {
                "-DHELENGINE_LINUX_STATIC_MESH_BATCHING=" + (staticEnabled ? "ON" : "OFF"),
                "-DHELENGINE_LINUX_STATIC_MESH_BATCHING_BUDGET_MIB=17",
                "-DHELENGINE_LINUX_DYNAMIC_MESH_BATCHING=" + (dynamicEnabled ? "ON" : "OFF"),
                "-DHELENGINE_LINUX_DYNAMIC_MESH_BATCHING_MAX_VERTICES=73", "-DHELENGINE_LINUX_DYNAMIC_MESH_BATCHING_BUDGET_MIB=3"
            };
            foreach (string argument in required) Require(command.ArgumentList.Contains(argument),"Selected batching value did not reach CMake: " + argument);
            Require(!command.UseShellExecute,"Configuration must retain structured arguments.");
        }
        foreach (string key in expected.Keys) {
            string[] invalid = key.EndsWith("enabled",StringComparison.Ordinal) ? new[] {"", "1", "yes", "invalid"} : new[] {"", "0", "-1", "1.5", "2147483648", "9999999999999999999999999", " 1", "+1"};
            foreach (string value in invalid) {
                bool rejected = false;
                try { LinuxMeshBatchingOptions.FromGraphicsOptions(new Dictionary<string,string> { [key] = value }); }
                catch (ArgumentException error) { rejected = error.Message.Contains(key,StringComparison.Ordinal); }
                Require(rejected,"Invalid batching value must fail contextually: " + key + "=" + value);
            }
        }
        var boundary = LinuxMeshBatchingOptions.FromGraphicsOptions(new Dictionary<string,string> { ["static-mesh-batching-budget-mib"]="2147483647" });
        Require(boundary.StaticBudgetMiB == int.MaxValue,"Representable positive settings must not overflow MiB parsing.");
        Console.WriteLine("PASS mesh batching metadata, defaults, independent modes, strict limits, CMake propagation");
    }
}
