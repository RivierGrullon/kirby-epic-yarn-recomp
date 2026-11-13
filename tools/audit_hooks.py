#!/usr/bin/env python3
"""Report symbol-based HLE address candidates without enabling or rewriting hooks."""
import collections
import csv
import json
import re
import sys

from kirby import ROOT, community_symbols, translator, run_logged


def main():
    try:
        index = ROOT / "local/upstream-native-index.json"
        index.parent.mkdir(exist_ok=True)
        tr = translator()
        run_logged("native-index-audit", [*tr, "emit-native-index", "--project", ROOT / "projects/RK5E01/recomp.yml",
                                         "--native-source-dir", ROOT / "vendor/wiicompiled/runtime/src", "--out", index])
        source_names = {}
        for line in (ROOT / "vendor/wiicompiled/projects/mkwii/MAP.txt").read_text().splitlines():
            fields = line.split(None, 1)
            if len(fields) == 2 and not fields[0].startswith("#"):
                source_names[int(fields[0], 16)] = fields[1]
        kirby = collections.defaultdict(set)
        for address, name, _ in community_symbols():
            if not re.fullmatch(r"(?:fn|func|lbl)_[0-9A-Fa-f]+", name):
                kirby[name].add(address)
        rows = []
        for registration in json.loads(index.read_text())["registrations"]:
            old = int(registration["address"], 16)
            name = source_names.get(old, "")
            matches = sorted(kirby.get(name, ()))
            status = "candidate_requires_abi_and_implementation_review" if len(matches) == 1 else "ambiguous" if matches else "no_named_match"
            rows.append(dict(upstream_address=f"0x{old:08X}", symbol=name,
                             upstream_hook=registration["symbol"], source_file=registration["sourceFile"],
                             kirby_candidates=";".join(f"0x{address:08X}" for address in matches), status=status))
        output = ROOT / "local/hook-audit.tsv"
        with output.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t")
            writer.writeheader()
            writer.writerows(rows)
        print(json.dumps(dict(collections.Counter(row["status"] for row in rows)), indent=2))
        for row in rows:
            if row["symbol"] in {"__init_hardware", "OSInit", "DVDInit", "VIInit", "GXInit", "WPADInit", "KPADInit"}:
                print(f"{row['symbol']}: {row['upstream_address']} -> {row['kirby_candidates'] or 'unresolved'} ({row['status']})")
        print(f"Audit only; no hook enabled. Report: {output}")
        return 0
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
