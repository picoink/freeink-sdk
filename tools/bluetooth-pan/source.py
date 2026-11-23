"""Materialize the exact Arduino IDF revision, verifying Git blob hashes.

The PlatformIO IDF package is a bootstrap, not the Arduino libraries' source
revision. Reuse matching files and fetch changed blobs instead of mixing them.
"""
from concurrent.futures import ThreadPoolExecutor
import base64
import hashlib
import shutil
import time
import json
import os
from pathlib import Path
import subprocess
import threading

IDF_COMMIT = "94639ab7251fc8230ca725df638c9a57dbb17153"


def verify_submodules(tree, submodules):
    revisions = {item["path"]: item["sha"] for item in tree["tree"] if item["mode"] == "160000"}
    for item in submodules:
        if revisions.get(item["path"]) != item["sha"]:
            raise RuntimeError(f"Submodule does not match the pinned IDF tree: {item['path']}")


def blob_hash(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def fetch(url, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + f".{threading.get_ident()}.download")
    subprocess.run(["curl", "--compressed", "-fsSL", "--retry", "3", "--retry-all-errors", "--max-time", "60",
                    url, "-o", str(temporary)], check=True)
    temporary.replace(output)


def hydrate(root, cache, repository, commit, include=lambda path: True, tree_file=None):
    root, cache = Path(root), Path(cache)
    manifest = Path(tree_file) if tree_file else cache / "tree.json"
    if not manifest.exists():
        fetch(f"https://api.github.com/repos/{repository}/git/trees/{commit}?recursive=1", manifest)
    tree = json.loads(manifest.read_text())
    if tree.get("truncated") or "tree" not in tree:
        raise RuntimeError(f"Incomplete source manifest: {repository}")
    files = [item for item in tree["tree"] if item["type"] == "blob" and include(item["path"])]
    paths = {item["path"] for item in files}
    modules = [item["path"] for item in tree["tree"] if item["mode"] == "160000"]
    # Remove stale source files, leaving package metadata and submodules intact.
    for path in root.rglob("*"):
        name = path.relative_to(root).as_posix()
        if (path.is_file() or path.is_symlink()) and include(name) and name not in paths:
            if name not in ("package.json", ".piopm", "version.txt") and not any(name.startswith(m + "/") for m in modules):
                path.unlink()
    done = 0
    lock = threading.Lock()
    gate = threading.Lock()
    next_request = [0.0]
    authenticated = bool(shutil.which("gh")) and os.environ.get("FREEINK_SOURCE_GH") == "1"

    def materialize(item):
        nonlocal done
        destination = root / item["path"]
        def contents(path):
            if path.is_symlink(): return os.readlink(path).encode()
            return path.read_bytes() if path.is_file() else b""
        if (destination.exists() or destination.is_symlink()) and blob_hash(contents(destination)) == item["sha"]:
            if destination.is_symlink() == (item["mode"] == "120000"):
                if not destination.is_symlink():
                    destination.chmod(0o755 if item["mode"] == "100755" else 0o644)
                return
        cached = cache / "blobs" / item["sha"]
        if not cached.exists() or blob_hash(cached.read_bytes()) != item["sha"]:
            if authenticated:
                # Optional authenticated API transport, useful when raw-content
                # downloads are unavailable. Keep below GitHub's secondary rate limit.
                with gate:
                    time.sleep(max(0, next_request[0] - time.monotonic()))
                    next_request[0] = time.monotonic() + 0.12
                payload = subprocess.check_output(["gh", "api", f"repos/{repository}/git/blobs/{item['sha']}"], timeout=120)
                response = json.loads(payload)
                data = base64.b64decode(response["content"])
                cached.parent.mkdir(parents=True, exist_ok=True)
                temporary = cached.with_name(cached.name + f".{threading.get_ident()}.download")
                temporary.write_bytes(data)
                temporary.replace(cached)
            else:
                from urllib.parse import quote
                fetch(f"https://raw.githubusercontent.com/{repository}/{commit}/{quote(item['path'])}", cached)
        data = cached.read_bytes()
        if blob_hash(data) != item["sha"]:
            raise RuntimeError(f"Source hash mismatch: {repository}/{item['path']}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.is_symlink() or destination.exists(): destination.unlink()
        if item["mode"] == "120000":
            destination.symlink_to(data.decode())
        else:
            destination.write_bytes(data)
            destination.chmod(0o755 if item["mode"] == "100755" else 0o644)
        with lock:
            done += 1
            if done % 100 == 0: print(f"{repository}: updated {done} source files", flush=True)

    with ThreadPoolExecutor(max_workers=24) as pool:
        list(pool.map(materialize, files))
    print(f"Verified {repository}@{commit}: {len(files)} files ({done} updated)", flush=True)
    return tree


def idf_source(path):
    return path.startswith(("components/", "tools/")) or "/" not in path
