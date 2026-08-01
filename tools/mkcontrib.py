#!/usr/bin/env python3
"""Generate CONTRIB from a trusted local developer checkout.

This local-only maintenance script intentionally resolves ``git`` through the
caller's PATH. Run it only from a trusted checkout with a trusted PATH.
"""

# 1. Visual inspection: python3 tools/mkcontrib.py
# 2. If OK, python3 tools/mkcontrib.py | grep -v ^# > CONTRIB

import subprocess
from collections import Counter, defaultdict


def main() -> int:
    result = subprocess.run(
        [
            "git",
            "log",
            "--no-merges",
            "--encoding=UTF-8",
            "--format=%aN <%aE>",
        ],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )

    common_email = defaultdict(Counter)
    common_name = defaultdict(Counter)
    for line in result.stdout.splitlines():
        author, email = line.split("<")
        common_email[author].update([email])
        common_name[email].update([author])

    for email in common_name:
        names = common_name[email]
        names = sorted(names, key=lambda name: (names[name], len(name)), reverse=True)
        for name in names[1:]:
            common_email[names[0]] += common_email[name]
            del common_email[name]
            print(
                "# Less common or shorter name",
                name,
                "is replaced by",
                names[0],
            )

    for name in sorted(common_email):
        for email, _count in common_email[name].most_common()[1:]:
            print(f"# Less common email <{email} is removed.")
        print(f"{name}<{common_email[name].most_common(1)[0][0]}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
