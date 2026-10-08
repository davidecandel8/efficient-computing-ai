#!/usr/bin/env python3
"""Refresh the homepage page-update list from the configured starting commit."""

from pathlib import Path
import subprocess
import sys


START_COMMIT = "b9c8d0e"


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    try:
        commits = subprocess.check_output(
            [
                "git", "-C", str(root), "log",
                "--format=%H%x00%ad%x00%s", "--date=format:%-d %B %Y",
                f"{START_COMMIT}^..HEAD",
            ],
            text=True,
        )
    except subprocess.CalledProcessError as error:
        print(f"Could not read commits from {START_COMMIT}: {error}", file=sys.stderr)
        return error.returncode

    groups: dict[str, list[tuple[str, list[str]]]] = {}
    for line in commits.splitlines():
        commit, date, subject = line.split("\0", maxsplit=2)
        if not subject.startswith("docs:"):
            continue

        changed_files = subprocess.check_output(
            [
                "git", "-C", str(root), "diff-tree", "--no-commit-id",
                "--name-only", "--diff-filter=ACMRT", "-r", commit,
            ],
            text=True,
        ).splitlines()
        pages = [
            path for path in changed_files
            if path.endswith(".md") and path != "index.md" and (root / path).is_file()
        ]
        if pages:
            groups.setdefault(date, []).append((subject, pages))

    sections = []
    for date, updates in groups.items():
        bullets = []
        for subject, pages in updates:
            bullets.append(f"- **{subject}**")
            bullets.extend(f"  - [{path}]({path})" for path in pages)
        sections.append(f"## {date}\n\n" + "\n".join(bullets))
    updates = "\n\n".join(sections)

    index = root / "index.md"
    content = index.read_text()
    front_matter_end = content.find("\n---", content.find("---") + 3)
    if not content.startswith("---\n") or front_matter_end < 0:
        print("index.md must start with YAML front matter", file=sys.stderr)
        return 1

    front_matter = content[: front_matter_end + len("\n---")]
    index.write_text(f"{front_matter}\n\n{updates}\n" if updates else f"{front_matter}\n")
    page_count = sum(len(pages) for updates in groups.values() for _, pages in updates)
    print(f"Updated {index} with {page_count} changed page link(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
