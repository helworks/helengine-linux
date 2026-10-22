"""Validate the actual generated menu and its scene transition through SDL input, without screenshots or direct scene-loading calls."""
import argparse
import os
import pathlib
import re
import subprocess


def main():
    """Run the same Core/player with an explicit SDL virtual gamepad replay and assert observable generated menu and scene state."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--player", required=True)
    parser.add_argument("--content", required=True)
    parser.add_argument("--xvfb", action="store_true")
    options = parser.parse_args()
    replay = pathlib.Path(__file__).resolve().parent / "input/menu-scene-navigation.txt"
    command = [options.player,"--content",options.content,"--frames","100","--validation","--input-script",str(replay)]
    environment = os.environ.copy()
    if options.xvfb:
        command = ["xvfb-run","-a"] + command
        environment.update(HELENGINE_ISOLATED_DISPLAY_TEST="1",SDL_VIDEO_DRIVER="x11")
    else:
        command.append("--offscreen")
    result = subprocess.run(command,capture_output=True,text=True,env=environment,timeout=120)
    output = result.stdout + result.stderr
    required = ["core=generated scene=DemoDiscMainMenu catalog_count=15", "selected=scene-colored-cube-grid menu_items=44", "panel=ray-tracing-select selected=scene-software-path-tracer menu_items=44", "scene=software_path_tracer", "core_frames=100", "nonuniform=1"]
    transitions = re.findall(r"loaded_scene_frame=\d+ scene=(\S+)",output)
    tracer_index = transitions.index("software_path_tracer") if "software_path_tracer" in transitions else len(transitions)
    returned = transitions and transitions[0]=="DemoDiscMainMenu" and "DemoDiscMainMenu" in transitions[tracer_index+1:]
    if result.returncode or any(marker not in output for marker in required) or not returned or "Vulkan validation:" in output:
        raise RuntimeError("Real SDL/Core menu navigation failed:\n"+output)
    print("PASS generated Core menu44 SDL-virtual-gamepad selection panel scene-entry-and-return GPU-readback validation mode="+("isolated-Xvfb" if options.xvfb else "offscreen"))


if __name__ == "__main__":
    main()
