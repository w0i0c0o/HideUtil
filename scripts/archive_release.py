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
DST_ROOT = r"E:\幸玮代码\_hideutil_archive"

BINARIES = ["HideUtil.exe", "hideutil_cli.exe", "hideutil_tests.exe", "hideutil_qa_tests.exe"]

# Conventional Commits type -> Chinese label used in the archive README.
TYPE_LABELS = [
    ("feat", "新功能"),
    ("fix", "修复"),
    ("perf", "性能"),
    ("refactor", "重构"),
    ("docs", "文档"),
    ("test", "测试"),
    ("build", "构建"),
    ("ci", "CI"),
    ("chore", "杂项"),
]


def sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([GIT, *args], cwd=ROOT, capture_output=True, text=True)


def resolve_tag() -> str:
    """Explicit argument wins; otherwise archive the most recent tag."""
    if len(sys.argv) > 1:
        return sys.argv[1]
    latest = run("describe", "--tags", "--abbrev=0").stdout.strip()
    if not latest:
        raise SystemExit("没有传入 tag，且仓库里找不到任何标签")
    return latest


def split_subject(subject: str) -> tuple[str, str]:
    """'feat(i18n): add Localizer' -> ('feat', 'add Localizer')."""
    head, _, rest = subject.partition(":")
    ctype = head.split("(")[0].strip().lower()
    return ctype, rest.strip() or subject.strip()


def collect_highlights(tag: str) -> tuple[str, list[str]]:
    """Group commit subjects between the previous tag and this one."""
    # `git describe <tag>^` yields the preceding tag; it fails for the first tag.
    prev = run("describe", "--tags", "--abbrev=0", tag + "^").stdout.strip()
    rev_range = f"{prev}..{tag}" if prev else tag
    log = run("log", "--no-merges", "--pretty=format:%s", rev_range).stdout

    buckets: dict[str, list[str]] = {}
    other: list[str] = []
    for line in log.splitlines():
        line = line.strip()
        if not line:
            continue
        ctype, text = split_subject(line)
        if ctype in dict(TYPE_LABELS):
            buckets.setdefault(ctype, []).append(text)
        else:
            other.append(line)

    lines: list[str] = []
    for ctype, label in TYPE_LABELS:
        for text in buckets.get(ctype, []):
            lines.append(f"  [{label}] {text}")
    for text in other:
        lines.append(f"  [其它] {text}")
    return (f"{prev}..{tag}" if prev else f"(首个标签) {tag}"), (lines or ["  （该区间内没有提交）"])


def main() -> int:
    tag = resolve_tag()
    dst = os.path.join(DST_ROOT, tag)
    os.makedirs(dst, exist_ok=True)
    lines: list[str] = []

    for name in BINARIES:
        src = os.path.join(ROOT, "build", name)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(dst, name))
            lines.append(f"  {name}\t{os.path.getsize(src)}\t{sha256(src)}")
        else:
            lines.append(f"  {name}\tMISSING")

    head = run("rev-parse", "HEAD").stdout.strip()
    status = run("status", "--short").stdout.strip()
    tag_at_head = run("rev-list", "-n", "1", tag).stdout.strip()
    rev_range, highlights = collect_highlights(tag)

    zip_name = f"hideutil-{tag}-src.zip"
    zip_path = os.path.join(dst, zip_name)
    archive = run("archive", "--format=zip", "-o", zip_path, tag)
    zip_ok = os.path.exists(zip_path)
    if zip_ok:
        lines.append(f"  {zip_name}\t{os.path.getsize(zip_path)}\t{sha256(zip_path)}")

    note = [
        f"HideUtil 发布存档 {tag}",
        f"HEAD commit: {head}",
        f"{tag} -> {tag_at_head}",
        f"源码 zip: {'OK' if zip_ok else 'FAILED: ' + archive.stderr.strip()}",
        f"工作区状态(应为空): {status!r}",
        "",
        "文件 / 字节 / SHA256:",
        *lines,
        "",
        f"版本要点（提交区间 {rev_range}，由 Conventional Commits 前缀自动归类）:",
        *highlights,
    ]
    with open(os.path.join(dst, "README.txt"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(note) + "\n")

    print("\n".join(note))
    return 0 if zip_ok and status == "" else 1


if __name__ == "__main__":
    raise SystemExit(main())
