"""Summarize independent three-run medians (nested timings are inclusive)."""
import collections
import json
import pathlib
import statistics
import sys

rows = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").splitlines()]
assert rows and all(row["ok"] for row in rows)
groups = collections.defaultdict(list)
for row in rows:
    groups[row["scenario"], row["phase"]].append(row)
result = []
for (scenario, phase), values in groups.items():
    keys = set().union(*(v["timings"] for v in values))
    result.append({
        "scenario": scenario, "phase": phase, "runs": len(values),
        "wall_ms": statistics.median(v["wall_ms"] for v in values),
        "wall_min_ms": min(v["wall_ms"] for v in values),
        "wall_max_ms": max(v["wall_ms"] for v in values),
        "cpu_ms": statistics.median(v["cpu_ms"] for v in values),
        "commands": values[0]["commands"], "body_messages": values[0]["body_messages"],
        "timings": {k: {"ms": statistics.median(v["timings"].get(k, {}).get("ms", 0) for v in values),
                         "calls": values[0]["timings"].get(k, {}).get("calls", 0)} for k in sorted(keys)},
    })
serialized = json.dumps(result, indent=2)
if len(sys.argv) > 2:
    pathlib.Path(sys.argv[2]).write_text(serialized + "\n", encoding="utf-8")
else:
    print(serialized)
