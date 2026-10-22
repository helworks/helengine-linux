"""Validate actual packaged Core/scene rendering without creating a window or writing framebuffer screenshots."""
import argparse
import re
import subprocess


def main():
    """Require the real player to load a catalog scene, update frames and submit nonuniform GPU content."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--player", required=True)
    parser.add_argument("--content", required=True)
    parser.add_argument("--scene")
    arguments = parser.parse_args()
    command = [arguments.player,"--content",arguments.content,"--offscreen","--frames","3","--validation"]
    if arguments.scene:
        command.extend(["--scene",arguments.scene])
    completed = subprocess.run(command,capture_output=True,text=True,timeout=120)
    print(completed.stdout[-4000:])
    print(completed.stderr[-4000:])
    if completed.returncode:
        raise RuntimeError("The actual generated-core player failed with exit code " + str(completed.returncode))
    if "core=generated" not in completed.stdout or "core_frames=3" not in completed.stdout or "nonuniform=1" not in completed.stdout:
        raise RuntimeError("The player did not prove actual scene loading, simulation and nonuniform GPU rendering.")
    calls = re.search(r"draw_calls=(\d+)",completed.stdout)
    if not calls or int(calls[1]) <= 0:
        raise RuntimeError("No real generated command reached the GPU.")
    print("PASS real generated Core/catalog scene/update/2D GPU readback")


if __name__ == "__main__":
    main()
