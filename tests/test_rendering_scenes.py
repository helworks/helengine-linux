"""Validate all fourteen real cooked rendering scenes using hardware mesh visibility or completed software tracing progress."""
import argparse
import json
import os
import pathlib
import re
import subprocess
import time


def main():
    """Run the real generated player and retain one overwritten report; HUD draws alone cannot satisfy a rendering-scene check."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--player", required=True)
    parser.add_argument("--content", required=True)
    parser.add_argument("--3d-only", dest="meshes_only", action="store_true")
    parser.add_argument("--tracing-only", action="store_true")
    parser.add_argument("--tracer-frames", type=int, default=1400)
    parser.add_argument("--report", default="/tmp/helengine/linux/rendering-scenes.json")
    options = parser.parse_args()
    if options.meshes_only and options.tracing_only:
        parser.error("Select at most one scene subset.")
    if options.tracer_frames < 1:
        parser.error("Tracing frames must be positive.")
    mesh_scenes = ["axis_test","axis_test2","colored_cube_grid","cube_test","directional_shadow_plaza","pbr_material_gallery","pbr_shadow_theater","pbr_textured_showcase","test_scene_matrix_render","textured_cube_grid"]
    tracing_scenes = ["ray_tracing_soft_shadows","ray_tracing_spheres","ray_tracing_teapot","software_path_tracer"]
    selected = mesh_scenes if options.meshes_only else tracing_scenes if options.tracing_only else mesh_scenes + tracing_scenes
    report = {"totalRequestedScenes":14,"selectedScenes":len(selected),"complete":False,"results":[]}
    for scene in selected:
        tracing = scene in tracing_scenes
        command = [options.player,"--content",options.content,"--scene",scene,"--offscreen","--frames",str(options.tracer_frames if tracing else 10),"--validation"]
        if tracing:
            command.append("--require-tracer-progress")
        started = time.monotonic()
        try:
            result = subprocess.run(command,capture_output=True,text=True,env=os.environ.copy(),timeout=240)
            output = result.stdout + result.stderr
            field = "tracer_spp" if tracing else "mesh_visible_samples"
            measured = re.search(r"\b"+field+r"=(\d+)",output)
            passed = result.returncode==0 and measured is not None and int(measured.group(1))>0 and "nonuniform=1" in output and "Vulkan validation:" not in output
            record = {"scene":scene,"passed":passed,"seconds":round(time.monotonic()-started,3),"exitCode":result.returncode,"evidence":output}
        except subprocess.TimeoutExpired as error:
            record = {"scene":scene,"passed":False,"seconds":round(time.monotonic()-started,3),"error":"Player exceeded the explicit 240-second validation limit.","evidence":str(error.stdout or "")+str(error.stderr or "")}
        report["results"].append(record)
        print(("PASS " if record["passed"] else "FAIL ")+scene+" seconds="+str(record["seconds"]),flush=True)
        if not record["passed"]:
            print(record["evidence"][-4500:],flush=True)
        destination = pathlib.Path(options.report)
        destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_text(json.dumps(report,indent=2))
    failures = sum(not result["passed"] for result in report["results"])
    report["complete"] = len(selected)==14 and failures==0
    pathlib.Path(options.report).write_text(json.dumps(report,indent=2))
    if failures:
        raise RuntimeError(str(failures)+" real rendering scene(s) failed; report: "+str(pathlib.Path(options.report).resolve()))
    print("PASS real rendering scenes "+str(len(selected))+"/"+str(len(selected))+"; full14="+str(report["complete"]))


if __name__ == "__main__":
    main()
