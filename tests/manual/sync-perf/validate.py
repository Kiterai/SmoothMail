"""Read-only validation of recovered synthetic benchmark databases and results."""
import collections
import json
import pathlib
import sqlite3
import sys

directory = pathlib.Path(sys.argv[1]).resolve()
rows = [json.loads(line) for line in (directory / "results.jsonl").read_text(encoding="utf-8").splitlines()]
assert len(rows) == 120 and all(row["ok"] for row in rows)
for row in rows:
    expected = row["messages"] if row["phase"] == "initial" else 10 if row["phase"] == "add10-recent" else 0
    assert row["body_messages"] == expected, row
databases = list(directory.glob("*.sqlite"))
assert len(databases) == 24, len(databases)
counts = collections.Counter()
for path in databases:
    with sqlite3.connect(path.as_uri() + "?mode=ro&immutable=1", uri=True) as db:
        assert db.execute("PRAGMA quick_check").fetchone() == ("ok",), path
        count = db.execute("SELECT COUNT(*) FROM cache_messages").fetchone()[0]
        assert db.execute("SELECT COUNT(*) FROM cache_index").fetchone()[0] == count
        assert db.execute("SELECT COUNT(DISTINCT uid) FROM cache_messages").fetchone()[0] == count
        assert db.execute("SELECT MIN(uid),MAX(uid) FROM cache_messages").fetchone() == (1, count)
        assert db.execute("SELECT COUNT(*) FROM cache_folders").fetchone()[0] in (1, 30)
        counts[count] += 1
assert counts == {110: 9, 1010: 9, 5010: 6}, counts
print(json.dumps({"syncs": len(rows), "failures": 0, "databases": len(databases), "message_count_distribution": dict(counts)}))
