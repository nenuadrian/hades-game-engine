"""MkDocs build hook.

Docs live natively under ``docs/``; this hook only regenerates the class
relationship diagram before each build.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CLASS_DIAGRAM_SCRIPT = REPO_ROOT / "scripts" / "generate_class_diagram.py"


def on_pre_build(config, **kwargs):  # noqa: ANN001
    subprocess.run(
        [sys.executable, str(CLASS_DIAGRAM_SCRIPT)],
        check=True,
        cwd=REPO_ROOT,
    )
