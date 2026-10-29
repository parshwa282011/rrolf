#!/usr/bin/env python3
"""Checks that every petal id has everything the game needs to show it.

    python3 tools/check-petals.py

For each id of enum rr_petal_id (Shared/StaticData.h) it checks that
RR_PETAL_DATA, RR_PETAL_NAMES and RR_PETAL_DESCRIPTIONS have a row for it, in the
same order, and that Client/Assets/RenderPetals.c has a `case` that draws it,
so a petal that is added in one place and forgotten in another fails here
instead of showing up as a blank slot. The inventory, crafting, loadout and
save code loop to rr_petal_id_max and need nothing per petal.
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def read(path):
    return open(os.path.join(ROOT, path)).read()


header = read("Shared/StaticData.h")
source = read("Shared/StaticData.c")
render = read("Client/Assets/RenderPetals.c")

enum = re.search(r"enum rr_petal_id\s*\{(.*?)\};", header, re.S).group(1)
ids = re.findall(r"^\s*(rr_petal_id_\w+)", enum, re.M)
assert ids[0] == "rr_petal_id_none" and ids[-1] == "rr_petal_id_max"
ids = ids[:-1]

data = re.search(r"RR_PETAL_DATA\[rr_petal_id_max\] = \{(.*?)\n\};", source, re.S).group(1)
data_ids = re.findall(r"^\s*\{(rr_petal_id_\w+),", data, re.M)
names = re.search(r"RR_PETAL_NAMES\[rr_petal_id_max\] = \{(.*?)\};", source, re.S).group(1)
names = re.findall(r'"([^"]*)"', names)
descriptions = re.search(r"RR_PETAL_DESCRIPTIONS\[rr_petal_id_max\] = \{(.*?)\n\};", source, re.S).group(1)
# the first one is 0, the rest are strings that may be split over lines
descriptions = re.findall(r'"((?:[^"\\]|\\.)*)"', descriptions)

errors = []
if data_ids != ids:
    for i, (a, b) in enumerate(zip(ids, data_ids)):
        if a != b:
            errors.append("RR_PETAL_DATA row %d is %s, the enum says %s" % (i, b, a))
            break
    if len(data_ids) != len(ids):
        errors.append("RR_PETAL_DATA has %d rows for %d ids" % (len(data_ids), len(ids)))
if len(names) != len(ids):
    errors.append("RR_PETAL_NAMES has %d names for %d ids" % (len(names), len(ids)))
if len(descriptions) != len(ids) - 1:
    errors.append("RR_PETAL_DESCRIPTIONS has %d descriptions for %d ids (the first is 0)"
                  % (len(descriptions), len(ids)))
for name in ids[1:]:
    if not re.search(r"case %s:" % name, render):
        errors.append("RenderPetals.c has no case that draws %s" % name)

print("%d petals checked" % (len(ids) - 1))
for e in errors:
    print("FAIL:", e)
sys.exit(1 if errors else 0)
