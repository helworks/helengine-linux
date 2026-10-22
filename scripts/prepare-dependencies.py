"""Install hash-locked Ubuntu development packages into the user's persistent tool prefix, without sudo."""
import hashlib
import json
import pathlib
import subprocess


def main():
    """Download exact versions, verify every byte and extract only authenticated lock matches."""
    repository = pathlib.Path(__file__).resolve().parents[1]
    lock = json.loads((repository / "dependencies.lock.json").read_text())
    packages = pathlib.Path("/tmp/helengine/linux/packages")
    prefix = pathlib.Path.home() / "dev/helworks/tools/linux-native"
    packages.mkdir(parents=True, exist_ok=True)
    prefix.mkdir(parents=True, exist_ok=True)
    for package in lock["packages"]:
        subprocess.run(["apt-get", "download", package["name"] + "=" + package["version"]],cwd=packages,check=True)
        candidates = list(packages.glob(package["name"] + "_" + package["version"] + "_amd64.deb"))
        if len(candidates) != 1:
            raise RuntimeError("Expected exactly one pinned package: " + package["name"])
        archive = candidates[0]
        if hashlib.sha256(archive.read_bytes()).hexdigest() != package["sha256"]:
            raise RuntimeError("SHA256 mismatch; refusing to extract " + archive.name)
        subprocess.run(["dpkg-deb","-x",str(archive),str(prefix)],check=True)
        archive.unlink()
    packages.rmdir()
    print("Verified Linux native tools: " + str(prefix))


if __name__ == "__main__":
    main()
