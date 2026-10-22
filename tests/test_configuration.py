"""Configure the real checkout and verify actionable rejection of invalid player requests."""
import os
import pathlib
import subprocess
import unittest


class ConfigurationTests(unittest.TestCase):
    """Exercise CMake's public input contract without generated fixtures or a compiler build."""

    def configure(self, *arguments):
        """Reuse the platform output root and preserve diagnostics from a real CMake invocation."""
        root = pathlib.Path(__file__).resolve().parents[1]
        output = pathlib.Path(os.environ.get("HELENGINE_LINUX_TEST_OUTPUT", "C:/temp/helengine/linux" if os.name == "nt" else "/tmp/helengine/linux"))
        return subprocess.run(["cmake", "-S", str(root), "-B", str(output), "-G", "Ninja", *arguments], capture_output=True, text=True)

    def test_player_rejects_missing_core(self):
        """A diagnostic host must never be substituted for a successful generated-core player."""
        result = self.configure("-DHELENGINE_LINUX_BUILD_PLAYER=ON", "-DHELENGINE_CORE_CPP_ROOT=/missing/generated-core", "-DHELENGINE_NATIVE_PROFILE=Debug")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("helcpp_config.hpp", result.stdout + result.stderr)

    def test_invalid_profile_is_rejected(self):
        """Unknown profiles cannot silently fall back to an optimized or uninstrumented build."""
        result = self.configure("-DHELENGINE_LINUX_BUILD_PLAYER=OFF", "-DHELENGINE_NATIVE_PROFILE=Unknown")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must be Debug, Release, or Profiler", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()

