"""Configure the actual Linux checkout and overwrite the single Linux native output directory."""
import argparse
import os
import pathlib
import subprocess


def main():
    """Build either the explicit GPU diagnostic or a generated-core player, preserving each child exit code."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile",choices=["Debug","Release","Profiler"],default="Debug")
    parser.add_argument("--diagnostics",action="store_true")
    parser.add_argument("--core")
    parser.add_argument("--code")
    parser.add_argument("--content")
    parser.add_argument("--test",action="store_true")
    arguments = parser.parse_args()
    repository = pathlib.Path(__file__).resolve().parents[1]
    output = pathlib.Path("/tmp/helengine/linux")
    prefix = pathlib.Path.home() / "dev/helworks/tools/linux-native/usr"
    configure = ["cmake","-S",str(repository),"-B",str(output),"-G","Ninja",
        "-DHELENGINE_NATIVE_PROFILE=" + arguments.profile,"-DHELENGINE_LINUX_BUILD_PLAYER=" + ("OFF" if arguments.diagnostics else "ON"),
        "-DCMAKE_PREFIX_PATH=" + str(prefix),"-DVulkan_LIBRARY=/lib/x86_64-linux-gnu/libvulkan.so.1",
        "-DVulkan_INCLUDE_DIR=" + str(prefix / "include"),"-DHELENGINE_GLSLANG_VALIDATOR=" + str(prefix / "bin/glslangValidator")]
    for value,name in [(arguments.core,"CORE_CPP"),(arguments.code,"CODE"),(arguments.content,"CONTENT")]:
        if value:
            configure.append("-DHELENGINE_" + name + "_ROOT=" + str(pathlib.Path(value).resolve()))
    subprocess.run(configure,check=True)
    subprocess.run(["cmake","--build",str(output),"--parallel","4"],check=True)
    if arguments.test:
        environment = os.environ.copy()
        environment["VK_DRIVER_FILES"] = "/usr/share/vulkan/icd.d/nvidia_icd.json"
        subprocess.run([str(output / "helengine_linux_gpu_test")],env=environment,check=True)


if __name__ == "__main__":
    main()
