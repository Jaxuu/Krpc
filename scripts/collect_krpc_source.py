#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
from datetime import datetime

SOURCE_EXTS = {".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh"}
EXTRA_EXTS = {".proto"}
EXTRA_NAMES = {"CMakeLists.txt"}

EXCLUDE_DIRS = {
    ".git",
    ".idea",
    ".vscode",
    "build",
    "cmake-build-debug",
    "cmake-build-release",
    "bin",
    "lib",
    "logs",
    "log",
    "tmp",
    "third_party",
    "vendor",
    "FlameGraph",
}

EXCLUDE_SUFFIXES = {
    ".pb.cc",
    ".pb.h",
}

def is_excluded_dir(path: Path) -> bool:
    return any(part in EXCLUDE_DIRS for part in path.parts)

def should_include_file(path: Path) -> bool:
    name = path.name

    for suffix in EXCLUDE_SUFFIXES:
        if name.endswith(suffix):
            return False

    if name in EXTRA_NAMES:
        return True

    if path.suffix in SOURCE_EXTS:
        return True

    if path.suffix in EXTRA_EXTS:
        return True

    return False

def guess_lang(path: Path) -> str:
    if path.suffix in {".cc", ".cpp", ".cxx"}:
        return "cpp"
    if path.suffix in {".h", ".hpp", ".hh"}:
        return "cpp"
    if path.suffix == ".proto":
        return "protobuf"
    if path.name == "CMakeLists.txt":
        return "cmake"
    return ""

def default_out_path() -> Path:
    return Path.cwd() / "krpc_source.md"    

def read_text_safely(path: Path, max_file_bytes: int):
    try:
        size = path.stat().st_size

        if max_file_bytes > 0 and size > max_file_bytes:
            with path.open("rb") as f:
                data = f.read(max_file_bytes)
            text = data.decode("utf-8", errors="replace")
            text += f"\n\n/* 文件过大，已截断。原始大小：{size} bytes，只保留前 {max_file_bytes} bytes。 */\n"
            return text

        return path.read_text(encoding="utf-8", errors="replace")

    except Exception as e:
        return f"/* 读取失败：{e} */\n"

def main():
    parser = argparse.ArgumentParser(
        description="Collect C++ source files into one markdown file, excluding protobuf generated files."
    )
    parser.add_argument(
        "--root",
        default=".",
        help="项目根目录，默认当前目录"
    )
    parser.add_argument(
        "--out",
        default=str(default_out_path()),
        help="输出 md 文件路径，默认桌面 krpc_source.md"
    )
    parser.add_argument(
        "--max-file-bytes",
        type=int,
        default=300000,
        help="单个文件最大读取字节数，默认 300000；设置为 0 表示不限制"
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    out = Path(args.out).expanduser().resolve()

    files = []

    for dirpath, dirnames, filenames in os.walk(root):
        cur = Path(dirpath)

        # 原地过滤目录，避免进入 build/.git/bin 等目录
        dirnames[:] = [
            d for d in dirnames
            if d not in EXCLUDE_DIRS and not is_excluded_dir(cur / d)
        ]

        for filename in filenames:
            path = cur / filename

            if is_excluded_dir(path.relative_to(root)):
                continue

            if should_include_file(path):
                files.append(path)

    files.sort(key=lambda p: str(p.relative_to(root)))

    out.parent.mkdir(parents=True, exist_ok=True)

    total_lines = 0

    with out.open("w", encoding="utf-8") as md:
        md.write("# KRPC Project Source Collection\n\n")
        md.write(f"- Generated at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        md.write(f"- Project root: `{root}`\n")
        md.write(f"- File count: {len(files)}\n")
        md.write("- Excluded: `*.pb.cc`, `*.pb.h`, build/bin/.git/third_party/vendor/log directories\n\n")

        md.write("## File Tree\n\n")
        for path in files:
            rel = path.relative_to(root)
            md.write(f"- `{rel}`\n")

        md.write("\n---\n\n")

        for path in files:
            rel = path.relative_to(root)
            lang = guess_lang(path)
            content = read_text_safely(path, args.max_file_bytes)
            total_lines += len(content.splitlines())

            md.write(f"## File: `{rel}`\n\n")
            md.write(f"```{lang}\n")
            md.write(content)
            if not content.endswith("\n"):
                md.write("\n")
            md.write("```\n\n")
            md.write("---\n\n")

    print(f"Done. Collected {len(files)} files.")
    print(f"Output: {out}")
    print(f"Size: {out.stat().st_size / 1024 / 1024:.2f} MB")
    print(f"Total lines of code: {total_lines}")

if __name__ == "__main__":
    main()