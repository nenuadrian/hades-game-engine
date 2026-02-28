#!/usr/bin/env python3
"""Generate a Mermaid class relationship diagram from C++ headers."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass, field
from pathlib import Path


CLASS_PATTERN = re.compile(
    r"\b(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\b([^;{]*)\{",
    re.MULTILINE,
)


@dataclass
class ClassInfo:
    name: str
    kind: str
    header_path: Path
    bases: set[str] = field(default_factory=set)
    uses: set[str] = field(default_factory=set)
    body: str = ""


def strip_comments_and_literals(text: str) -> str:
    text = re.sub(r"//.*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", "''", text)
    return text


def find_matching_brace(text: str, open_idx: int) -> int:
    depth = 0
    i = open_idx
    length = len(text)

    while i < length:
        two = text[i : i + 2]
        ch = text[i]

        if two == "//":
            newline = text.find("\n", i)
            if newline == -1:
                return -1
            i = newline + 1
            continue
        if two == "/*":
            end = text.find("*/", i + 2)
            if end == -1:
                return -1
            i = end + 2
            continue
        if ch in {"'", '"'}:
            quote = ch
            i += 1
            while i < length:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1

    return -1


def parse_bases(header_fragment: str) -> set[str]:
    if ":" not in header_fragment:
        return set()

    fragment = header_fragment.split(":", 1)[1]
    bases: set[str] = set()

    for raw_base in fragment.split(","):
        cleaned = re.sub(
            r"\b(public|protected|private|virtual|final)\b", " ", raw_base
        )
        candidates = re.findall(r"[A-Za-z_][A-Za-z0-9_:]*", cleaned)
        for candidate in candidates:
            if candidate in {"class", "struct"}:
                continue
            bases.add(candidate.split("::")[-1])
            break

    return bases


def parse_classes_from_header(path: Path) -> list[ClassInfo]:
    content = path.read_text(encoding="utf-8", errors="ignore")
    classes: list[ClassInfo] = []

    for match in CLASS_PATTERN.finditer(content):
        kind = match.group(1)
        class_name = match.group(2)
        header_fragment = match.group(3)
        open_brace_idx = match.end() - 1
        close_brace_idx = find_matching_brace(content, open_brace_idx)
        if close_brace_idx == -1:
            continue

        body = content[open_brace_idx + 1 : close_brace_idx]
        classes.append(
            ClassInfo(
                name=class_name,
                kind=kind,
                header_path=path,
                bases=parse_bases(header_fragment),
                body=body,
            )
        )

    return classes


def build_diagram(classes: list[ClassInfo], source_root: Path) -> str:
    known_names = {cls.name for cls in classes}

    for cls in classes:
        clean_body = strip_comments_and_literals(cls.body)
        for other_name in sorted(known_names):
            if other_name == cls.name:
                continue
            if re.search(rf"\b{re.escape(other_name)}\b", clean_body):
                cls.uses.add(other_name)

    inheritance_edges: set[tuple[str, str]] = set()
    usage_edges: set[tuple[str, str]] = set()

    for cls in classes:
        for base in cls.bases:
            if base in known_names:
                inheritance_edges.add((base, cls.name))
        for used in cls.uses:
            if used in known_names and used not in cls.bases:
                usage_edges.add((cls.name, used))

    lines: list[str] = []
    lines.append("# Class Relationship Diagram")
    lines.append("")
    lines.append(
        "This page is auto-generated from `src/**/*.h*` by `scripts/generate_class_diagram.py`."
    )
    lines.append("")
    lines.append("```mermaid")
    lines.append("classDiagram")
    lines.append("  direction LR")

    for cls in sorted(classes, key=lambda item: item.name):
        lines.append(f"  class {cls.name}")

    for base, derived in sorted(inheritance_edges):
        lines.append(f"  {base} <|-- {derived}")

    for source, target in sorted(usage_edges):
        lines.append(f"  {source} --> {target} : uses")

    lines.append("```")
    lines.append("")
    lines.append("## Classes")
    lines.append("")
    lines.append("| Class | Kind | Header |")
    lines.append("|---|---|---|")

    for cls in sorted(classes, key=lambda item: item.name):
        relative_path = cls.header_path.relative_to(source_root.parent).as_posix()
        lines.append(f"| `{cls.name}` | `{cls.kind}` | `{relative_path}` |")

    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate Mermaid class relationship docs for MkDocs."
    )
    parser.add_argument(
        "--source-root",
        default="src",
        help="Root directory to scan for C/C++ headers (default: src).",
    )
    parser.add_argument(
        "--output",
        default="docs/generated/class-relationships.md",
        help="Output markdown path (default: docs/generated/class-relationships.md).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_root = Path(args.source_root).resolve()
    output_path = Path(args.output).resolve()

    headers = sorted(
        [
            path
            for path in source_root.rglob("*")
            if path.suffix in {".h", ".hpp", ".hh", ".hxx"}
        ]
    )

    classes: list[ClassInfo] = []
    for header in headers:
        classes.extend(parse_classes_from_header(header))

    if not classes:
        raise RuntimeError(f"No classes found in: {source_root}")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        build_diagram(classes, source_root),
        encoding="utf-8",
    )

    print(f"Generated diagram doc: {output_path}")
    print(f"Discovered classes: {len(classes)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
