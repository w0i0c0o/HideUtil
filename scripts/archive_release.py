"""Archive a HideUtil release: binaries + tagged source zip + hashes + notes.

Kept out of the tracked tree (build/ is gitignored). Usage:
    python archive_release.py [tag]
"""
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = r"E:\幸玮代码\Buddy_Space\6_Fade"
GIT = r"C:\Program Files\Git\bin\git.exe"
TAG = sys.argv[1] if len(sys.argv) > 1 else "v1.1.8"
DST = os.path.join(r"E:\幸玮代码\_hideutil_archive", TAG)

BINARIES = ["HideUtil.exe", "hideutil_cli.exe", "hideutil_tests.exe", "hideutil_qa_tests.exe"]


def sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([GIT, *args], cwd=ROOT, capture_output=True, text=True)


def main() -> int:
    os.makedirs(DST, exist_ok=True)
    lines: list[str] = []

    for name in BINARIES:
        src = os.path.join(ROOT, "build", name)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(DST, name))
            lines.append(f"  {name}\t{os.path.getsize(src)}\t{sha256(src)}")
        else:
            lines.append(f"  {name}\tMISSING")

    head = run("rev-parse", "HEAD").stdout.strip()
    status = run("status", "--short").stdout.strip()
    tag_at_head = run("rev-list", "-n", "1", TAG).stdout.strip()

    zip_name = f"hideutil-{TAG}-src.zip"
    zip_path = os.path.join(DST, zip_name)
    archive = run("archive", "--format=zip", "-o", zip_path, TAG)
    zip_ok = os.path.exists(zip_path)
    if zip_ok:
        lines.append(f"  {zip_name}\t{os.path.getsize(zip_path)}\t{sha256(zip_path)}")

    note = [
        f"HideUtil 发布存档 {TAG}",
        f"HEAD commit: {head}",
        f"{TAG} -> {tag_at_head}",
        f"源码 zip: {'OK' if zip_ok else 'FAILED: ' + archive.stderr.strip()}",
        f"工作区状态(应为空): {status!r}",
        "",
        "文件 / 字节 / SHA256:",
        *lines,
        "",
        "版本要点: 见 .workbuddy/memory/2026-09-17.md 的 v1.1.x 各节",
    ]
    with open(os.path.join(DST, "README.txt"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(note) + "\n")

    print("\n".join(note))
    return 0 if zip_ok and status == "" else 1


if __name__ == "__main__":
    raise SystemExit(main())
