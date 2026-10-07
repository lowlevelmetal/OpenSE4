#!/usr/bin/env python3
"""Stages the modding SDK's documentation and example mods into a release package
(tools/package_release.sh, docs/BUILDING.md "Release packages"), with links that work
there.

    python3 tools/stage_sdk_docs.py <package folder>

It copies, without Python's caches:

    docs/sdk             ->  sdk/docs
    docs/MODDING_SDK.md  ->  sdk/docs/MODDING_SDK.md   (the SDK's design, which the pages cite)
    mods/examples        ->  sdk/examples

and then rewrites the Markdown links of those files whose targets are elsewhere in the
package or not in it, and those of the files the packaging script put there first: the
README at the top, and the mods that come with OpenSE4 (mods/bundled.txt) in mods/.

- A file or folder the package has (one of the above, a bundled mod in mods/, the
  README or the licence): the relative path to its copy. From sdk/docs/README.md,
  ../../mods/examples/new-hull/ becomes ../examples/new-hull/, and ../MODDING_SDK.md
  becomes MODDING_SDK.md; from README.md, docs/sdk/README.md becomes sdk/docs/README.md.
- Anything else in the source tree (the specs, other docs, source files): its page on
  GitHub at the version's tag, the version from CMakeLists.txt. From sdk/docs/guide/data/,
  ../../../spec/04-combat.md#weapons becomes
  https://github.com/lowlevelmetal/OpenSE4/blob/v<version>/docs/spec/04-combat.md#weapons
  (tree/ instead of blob/ for a folder, raw/ for a picture, so that it shows).

Heading anchors are kept, and links that already lead to the right place are left as
they are. Links are found as tests/sdk/test_sdk_guide.cpp finds them: [text](target)
outside fenced code blocks and `inline code`, where the target has no space and is not
a web or mail address. The repository's own files are not changed.

It fails (exit 1) when a link leads nowhere in the source tree, or when a relative link
of the staged files does not lead to a file or folder of the package.
"""

from __future__ import annotations

import argparse
import posixpath
import re
import shutil
import sys
from pathlib import Path
from typing import Callable, List, Optional, Tuple

ROOT = Path(__file__).resolve().parent.parent
REPOSITORY = "https://github.com/lowlevelmetal/OpenSE4"
PICTURES = {".png", ".webp", ".jpg", ".jpeg", ".gif", ".svg"}   # linked as raw/ files


def project_version() -> str:
    """The project's version, as tools/package_release.sh reads it from CMakeLists.txt."""
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    found = re.search(r"^ *VERSION ([0-9.]+)$", text, re.MULTILINE)
    if not found:
        sys.exit("CMakeLists.txt: no project VERSION")
    return found.group(1)


def bundled_mods() -> List[str]:
    """The folders of mods/ that mods/bundled.txt names, one a line, # for comments."""
    names = []
    for line in (ROOT / "mods" / "bundled.txt").read_text(encoding="utf-8").splitlines():
        name = line.split("#", 1)[0].strip()
        if name:
            names.append(name)
    return names


# ---- Where the source tree's files are in the package -------------------------------------------


class Layout:
    """The package's copies of the source tree's files: (source path, package path)
    pairs, both relative and with forward slashes, a folder standing for everything in it."""

    def __init__(self, bundled: List[str]):
        self.places: List[Tuple[str, str]] = [
            ("docs/MODDING_SDK.md", "sdk/docs/MODDING_SDK.md"),
            ("docs/sdk", "sdk/docs"),
            ("mods/examples", "sdk/examples"),
            ("README.md", "README.md"),       # copied by the packaging script
            ("LICENSE", "LICENSE"),
        ]
        self.places += [(f"mods/{name}", f"mods/{name}") for name in bundled]

    def in_package(self, source: str) -> Optional[str]:
        for src, dst in self.places:
            if source == src:
                return dst
            if source.startswith(src + "/"):
                return dst + source[len(src):]
        return None


def relative(target: str, start: str) -> str:
    """The relative path from the folder `start` to `target`, both relative to the same
    root with forward slashes (posixpath.relpath would bring in the current folder)."""
    t = target.split("/")
    s = start.split("/") if start else []
    common = 0
    while common < len(t) and common < len(s) and t[common] == s[common]:
        common += 1
    return "/".join([".."] * (len(s) - common) + t[common:]) or "."


# ---- Markdown links -----------------------------------------------------------------------------


def is_followed(target: str) -> bool:
    """Whether a link target is one the guide's test follows: a path, not a web address."""
    return bool(target) and not target.startswith("http") and not target.startswith("mailto:") and " " not in target


def rewrite_links(text: str, fix: Callable[[int, str], str]) -> str:
    """The text with each link target outside code replaced by fix(line, target)."""
    lines = text.split("\n")
    code = False
    for n, line in enumerate(lines):
        if line.lstrip(" ").startswith("```"):
            code = not code
        if code:
            continue
        # The characters outside `inline code`, by their place in the line.
        kept = []
        inline = False
        for i, c in enumerate(line):
            if c == "`":
                inline = not inline
            elif not inline:
                kept.append(i)
        visible = "".join(line[i] for i in kept)
        parts = []
        done = 0
        at = visible.find("](")
        while at != -1:
            end = visible.find(")", at + 2)
            if end == -1:
                break
            target = visible[at + 2:end]
            first, last = kept[at + 1] + 1, kept[end]
            if is_followed(target) and line[first:last] == target:
                new = fix(n + 1, target)
                if new != target:
                    parts += [line[done:first], new]
                    done = last
            at = visible.find("](", at + 2)
        if parts:
            lines[n] = "".join(parts) + line[done:]
    return "\n".join(lines)


# ---- Staging ------------------------------------------------------------------------------------


class Stager:
    def __init__(self, stage: Path, version: str, layout: Layout):
        self.stage = stage
        self.version = version
        self.layout = layout
        self.problems: List[str] = []
        self.moved = 0      # links to another place in the package
        self.to_web = 0     # links to GitHub

    def web_address(self, source: str) -> str:
        if (ROOT / source).is_dir():
            kind = "tree"
        elif posixpath.splitext(source)[1].lower() in PICTURES:
            kind = "raw"
        else:
            kind = "blob"
        return f"{REPOSITORY}/{kind}/v{self.version}/{source}"

    def rewrite(self, page: str, source: str) -> None:
        """Writes the page `source` of the source tree to `page` of the package, with its
        links rewritten (from the source, so that staging again gives the same)."""

        def fix(line: int, target: str) -> str:
            path, hash_, anchor = target.partition("#")
            if not path:
                return target
            where = f"{source}:{line}: ({target})"
            linked = posixpath.normpath(posixpath.join(posixpath.dirname(source), path))
            if linked == ".." or linked.startswith("../") or not (ROOT / linked).exists():
                self.problems.append(f"{where}: no such file in the source tree")
                return target
            copy = self.layout.in_package(linked)
            if copy is None:
                self.to_web += 1
                return self.web_address(linked) + hash_ + anchor
            if posixpath.normpath(posixpath.join(posixpath.dirname(page), path)) == copy:
                return target
            self.moved += 1
            new = relative(copy, posixpath.dirname(page))
            if path.endswith("/"):
                new += "/"
            return new + hash_ + anchor

        text = (ROOT / source).read_bytes().decode("utf-8")
        (self.stage / page).write_bytes(rewrite_links(text, fix).encode("utf-8"))

    def check(self, page: str) -> None:
        """Checks that every relative link of the staged page leads into the package."""
        root = self.stage.resolve()

        def look(line: int, target: str) -> str:
            path = target.split("#", 1)[0]
            if path:
                found = (self.stage / posixpath.dirname(page) / path).resolve()
                if not found.exists() or (found != root and root not in found.parents):
                    self.problems.append(f"{page}:{line}: ({target}): not in the package")
            return target

        rewrite_links((self.stage / page).read_bytes().decode("utf-8"), look)


def markdown_files(folder: Path) -> List[str]:
    """The Markdown files under `folder`, relative to it with forward slashes."""
    return sorted(p.relative_to(folder).as_posix() for p in folder.rglob("*.md") if p.is_file())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("stage", type=Path, help="the package's folder")
    args = parser.parse_args()
    stage: Path = args.stage
    if not stage.is_dir():
        print(f"{stage}: no such folder", file=sys.stderr)
        return 2
    if (ROOT / "docs" / "sdk" / "MODDING_SDK.md").exists():
        print("docs/sdk/MODDING_SDK.md would be replaced by docs/MODDING_SDK.md", file=sys.stderr)
        return 1

    bundled = bundled_mods()
    layout = Layout(bundled)
    caches = shutil.ignore_patterns("__pycache__")
    pages: List[Tuple[str, str]] = []   # (package path, source path)
    for source in ("docs/sdk", "mods/examples"):
        copy = layout.in_package(source)
        assert copy is not None
        if (stage / copy).exists():
            shutil.rmtree(stage / copy)
        shutil.copytree(ROOT / source, stage / copy, ignore=caches)
        pages += [(f"{copy}/{f}", f"{source}/{f}") for f in markdown_files(ROOT / source)]
    pages.append(("sdk/docs/MODDING_SDK.md", "docs/MODDING_SDK.md"))   # written below
    # The pages the packaging script staged: the README, and the bundled mods' (the files
    # git tracks).
    if (stage / "README.md").is_file():
        pages.append(("README.md", "README.md"))
    for name in bundled:
        if (stage / "mods" / name).is_dir():
            pages += [(f"mods/{name}/{f}", f"mods/{name}/{f}") for f in markdown_files(stage / "mods" / name)]

    stager = Stager(stage, project_version(), layout)
    for page, source in pages:
        stager.rewrite(page, source)
    if not stager.problems:
        for page, _ in pages:
            stager.check(page)
    for problem in stager.problems:
        print(problem, file=sys.stderr)
    if stager.problems:
        return 1
    print(f"    README, sdk/docs, sdk/examples and the bundled mods: {len(pages)} pages, {stager.moved} links "
          f"rewritten to the package's copies, {stager.to_web} to GitHub at v{stager.version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
