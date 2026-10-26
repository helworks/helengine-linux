"""Require actual generated tagged cameras and real GPU output alpha without a desktop connection."""
import argparse
import os
import re
import subprocess


def main():
    """Validate independent completion/readiness, opaque and transparent fixtures, and rejection of an unmatched camera tag."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--player", required=True)
    parser.add_argument("--content", required=True)
    parser.add_argument("--frames", type=int, default=8)
    arguments = parser.parse_args()
    environment = dict(os.environ)
    environment.pop("DISPLAY", None)
    environment.pop("WAYLAND_DISPLAY", None)
    command = [arguments.player, "--content", arguments.content, "--offscreen", "--frames", str(arguments.frames), "--validation",
               "--output", "main,640,360,opaque", "--output", "glass,640,360,transparent", "--output", "absent-output,80,60,transparent"]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120, env=environment)
    print(result.stdout[-5000:])
    print(result.stderr[-3000:])
    if result.returncode:
        raise RuntimeError("Real generated multi-output player failed: " + str(result.returncode))
    receipts = {}
    for tag, generation, tick, ready, width, height in re.findall(r"^HELENGINE_OUTPUT 1 - ([A-Za-z0-9-]+) (\d+) (\d+) ([01]) (\d+) (\d+)$", result.stdout, re.M):
        receipts.setdefault(tag, []).append([int(generation), int(tick), int(ready), int(width), int(height)])
    for tag, size in {"main": [640, 360], "glass": [640, 360], "absent-output": [80, 60]}.items():
        rows = receipts.get(tag, [])
        if len(rows) != arguments.frames or any(row[0] != 1 or row[1] != index + 1 or row[3:] != size for index, row in enumerate(rows)):
            raise RuntimeError("Missing, stale or incorrect completion receipts for " + tag)
        if rows[-1][2] != int(tag != "absent-output"):
            raise RuntimeError("Camera routing/readiness failed for " + tag)
    summaries = {}
    for tag, size, checksum, zero, partial, opaque in re.findall(r"^output=([A-Za-z0-9-]+) rgba_bytes=(\d+) checksum=(\d+) alpha_zero=(\d+) alpha_partial=(\d+) alpha_opaque=(\d+)$", result.stdout, re.M):
        summaries[tag] = [int(size), int(checksum), int(zero), int(partial), int(opaque)]
    if summaries.get("main", [0] * 5)[2:] != [0, 0, 640 * 360]:
        raise RuntimeError("Opaque output did not retain alpha 255 everywhere.")
    glass = summaries.get("glass", [0] * 5)
    if glass[0] != 640 * 360 * 4 or min(glass[2:]) <= 0:
        raise RuntimeError("Transparent output did not retain empty, partial and opaque fixture pixels.")
    absent = summaries.get("absent-output", [0] * 5)
    if absent[0] != 80 * 60 * 4 or absent[2:] != [80 * 60, 0, 0]:
        raise RuntimeError("An unmatched output was contaminated by another camera.")
    if summaries["main"][1] == glass[1]:
        raise RuntimeError("Tagged outputs produced identical GPU content.")
    print("PASS real Core tagged cameras, independent receipts, alpha fixtures and unmatched output isolation")


if __name__ == "__main__":
    main()
