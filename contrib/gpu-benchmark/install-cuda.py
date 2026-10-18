#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Install controlled CUDA 12.8.1 compiler/runtime redistributables into work/."""
import hashlib
import json
import pathlib
import shutil
import sys
import tarfile
import urllib.request
import zipfile


def main():
    destination = pathlib.Path(sys.argv[1]).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    platform = "windows-x86_64" if sys.platform == "win32" else "linux-x86_64"
    base = "https://developer.download.nvidia.com/compute/cuda/redist/"
    with urllib.request.urlopen(base + "redistrib_12.8.1.json", timeout=120) as response:
        manifest = json.load(response)
    for component in ("cuda_nvcc", "cuda_cudart", "cuda_cccl"):
        entry = manifest[component][platform]
        archive = destination / pathlib.PurePosixPath(entry["relative_path"]).name
        urllib.request.urlretrieve(base + entry["relative_path"], archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != entry["sha256"]:
            raise RuntimeError("CUDA archive SHA256 mismatch: " + component)
        unpacked = destination / (component + "-unpacked")
        if platform.startswith("windows"):
            with zipfile.ZipFile(archive) as source:
                source.extractall(unpacked)
        else:
            with tarfile.open(archive) as source:
                source.extractall(unpacked, filter="data")
        roots = list(unpacked.iterdir())
        if len(roots) != 1 or not roots[0].is_dir():
            raise RuntimeError("Unexpected CUDA archive layout")
        # Retain each component's original license before merging the trees.
        notices = destination / "redistributable-notices" / component
        notices.mkdir(parents=True, exist_ok=True)
        for notice in roots[0].rglob("*"):
            if notice.is_file() and ("license" in notice.name.lower() or "notice" in notice.name.lower()):
                shutil.copy2(notice, notices / notice.name)
        shutil.copytree(roots[0], destination, dirs_exist_ok=True)
        shutil.rmtree(unpacked)
        archive.unlink()
        print("Verified and installed", component, manifest[component]["version"])
    # Linux redist archives use lib/, whereas nvcc's default linker layout
    # expects lib64/. Keep both paths usable without changing nvcc's profile.
    if platform.startswith("linux"):
        shutil.copytree(destination / "lib", destination / "lib64", dirs_exist_ok=True)


if __name__ == "__main__":
    main()
