#!/usr/bin/env python3
"""Checks that every relative link and image in README.md, CHANGES.md and docs/**/*.md resolves to a file (and that
a #fragment names a heading of a Markdown target). Run from anywhere: python3 docs/check_links.py"""
import re, sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
files = [root / "README.md", root / "CHANGES.md", root / "LICENSING.md"] + sorted((root / "docs").rglob("*.md"))
bad = 0


def slug(h):
    h = re.sub(r"[`*]", "", h.strip().lower())
    return re.sub(r"\s", "-", re.sub(r"[^\w\s-]", "", h))


for f in files:
    text = f.read_text(encoding="utf-8")
    refs = re.findall(r"\]\(([^)\s]+)", text) + re.findall(r'(?:src|href)="([^"]+)"', text)
    for r in refs:
        if re.match(r"[a-z]+:", r) or r.startswith("mailto"):
            continue
        path, _, frag = r.partition("#")
        t = (f.parent / path).resolve() if path else f
        if not t.exists():
            print(f"{f.relative_to(root)}: missing {r}")
            bad += 1
        elif frag and t.suffix == ".md":
            heads = {slug(m) for m in re.findall(r"^#+\s+(.*)$", t.read_text(encoding="utf-8"), re.M)}
            if frag not in heads:
                print(f"{f.relative_to(root)}: no heading #{frag} in {t.relative_to(root)}")
                bad += 1
print("links ok" if not bad else f"{bad} problems")
sys.exit(1 if bad else 0)
