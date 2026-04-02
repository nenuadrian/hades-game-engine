from __future__ import annotations

import re
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


def _write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")


def _generate_doc(source_name: str, target_name: str, front_matter: str = "") -> None:
    source_path = REPO_ROOT / source_name
    target_path = DOCS_DIR / target_name
    content = _rewrite_content(source_path.read_text(encoding="utf-8"))
    rendered = f"{front_matter}{GENERATED_COMMENT.format(source=source_name)}{content}"
    _write_if_changed(target_path, rendered)


def on_pre_build(config, **kwargs):  # noqa: ANN001
    _generate_doc("README.md", "index.md", HOME_FRONT_MATTER)
    _generate_doc("SCRIPTS.md", "scripting.md")
