from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit

REPO_ROOT = Path(__file__).resolve().parent.parent
DOCS_DIR = REPO_ROOT / "docs"

HOME_FRONT_MATTER = """---
hide:
  - toc
---

"""

GENERATED_COMMENT = (
    "<!-- Generated from {source} by scripts/mkdocs_hooks.py. Do not edit "
    "directly. -->\n\n"
)
CLASS_DIAGRAM_SCRIPT = REPO_ROOT / "scripts" / "generate_class_diagram.py"

ROOT_DOC_TARGETS = {
    "README.md": "index.md",
    "SCRIPTS.md": "scripting.md",
}

MARKDOWN_LINK_RE = re.compile(r"(!?\[[^\]]*\]\()([^)]+)(\))")
HTML_ATTR_RE = re.compile(r'(?P<attr>src|href)="(?P<target>[^"]+)"')


def _rewrite_target(target: str) -> str:
    parsed = urlsplit(target)
    if parsed.scheme or parsed.netloc or not parsed.path:
        return target

    rewritten_path = parsed.path
    if rewritten_path.startswith("docs/"):
        rewritten_path = rewritten_path.removeprefix("docs/")
    elif rewritten_path in ROOT_DOC_TARGETS:
        rewritten_path = ROOT_DOC_TARGETS[rewritten_path]

    return urlunsplit(parsed._replace(path=rewritten_path))


def _rewrite_content(content: str) -> str:
    content = MARKDOWN_LINK_RE.sub(
        lambda match: (
            f"{match.group(1)}{_rewrite_target(match.group(2))}{match.group(3)}"
        ),
        content,
    )
    return HTML_ATTR_RE.sub(
        lambda match: f'{match.group("attr")}="{_rewrite_target(match.group("target"))}"',
        content,
    )


def _strip_markdown_section(content: str, heading: str) -> str:
    lines = content.splitlines(keepends=True)
    heading_line = None

    for index, line in enumerate(lines):
        if line.strip().lower() == heading.lower():
            heading_line = index
            break

    if heading_line is None:
        return content

    section_end = len(lines)
    for index in range(heading_line + 1, len(lines)):
        stripped = lines[index].lstrip()
        if not stripped.startswith("#"):
            continue

        level = len(stripped) - len(stripped.lstrip("#"))
        if level <= 2:
            section_end = index
            break

    stripped_lines = lines[:heading_line]
    if stripped_lines and not stripped_lines[-1].endswith("\n"):
        stripped_lines[-1] += "\n"
    return "".join(stripped_lines + lines[section_end:]).lstrip("\n")


def _write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")


def _generate_doc(
    source_name: str,
    target_name: str,
    front_matter: str = "",
    strip_heading: str | None = None,
) -> None:
    source_path = REPO_ROOT / source_name
    target_path = DOCS_DIR / target_name
    content = _rewrite_content(source_path.read_text(encoding="utf-8"))
    if strip_heading:
        content = _strip_markdown_section(content, strip_heading)
    rendered = f"{front_matter}{GENERATED_COMMENT.format(source=source_name)}{content}"
    _write_if_changed(target_path, rendered)


def on_pre_build(config, **kwargs):  # noqa: ANN001
    subprocess.run(
        [sys.executable, str(CLASS_DIAGRAM_SCRIPT)],
        check=True,
        cwd=REPO_ROOT,
    )
    _generate_doc(
        "README.md",
        "index.md",
        HOME_FRONT_MATTER,
        strip_heading="## table of contents",
    )
    _generate_doc("SCRIPTS.md", "scripting.md")
