import os
import subprocess
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).parents[2]
SCRIPT = REPOSITORY_ROOT / "tools" / "mkcontrib.py"


def run_git(repository: Path, *arguments: str) -> None:
    subprocess.run(
        ["git", *arguments],
        cwd=repository,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )


def test_non_ascii_author_under_ascii_locale(tmp_path: Path) -> None:
    run_git(tmp_path, "init", "--quiet")
    run_git(
        tmp_path,
        "-c",
        "user.name=José Example",
        "-c",
        "user.email=jose@example.com",
        "commit",
        "--allow-empty",
        "--quiet",
        "--message=Add capture",
    )

    environment = os.environ.copy()
    environment.update(
        {
            "LC_ALL": "C",
            "PYTHONIOENCODING": "utf-8",
            "PYTHONUTF8": "0",
        }
    )
    result = subprocess.run(
        [sys.executable, SCRIPT],
        cwd=tmp_path,
        env=environment,
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )

    assert result.stdout == "José Example <jose@example.com>\n"
