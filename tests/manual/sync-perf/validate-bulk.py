"""Validate each populated folder, including colliding UIDs across folders."""
import json
import pathlib
import sqlite3
import sys

root = pathlib.Path(sys.argv[1]).resolve()
rows = [json.loads(line) for line in (root / "results.jsonl").read_text().splitlines()]
assert len(rows) == 45 and all(row["ok"] for row in rows)
for row in rows:
    folders = row["populated_folders"]
    expected = row["messages"] * folders if row["phase"] == "initial" else 10 * folders if row["phase"] == "add10-recent" else 0
    assert row["body_messages"] == expected, row
    expected_headers = row["messages"] * folders if row["phase"] in ("initial", "unchanged-all") else expected
    assert row["header_messages"] == expected_headers, row
    assert row["flags_messages"] == 0, row  # All populated folders exceed the boolean-search threshold.
for name in {row["database"] for row in rows}:
    row = next(row for row in rows if row["database"] == name and row["phase"] == "add10-recent")
    with sqlite3.connect((root / name).as_uri() + "?mode=ro&immutable=1", uri=True) as db:
        assert db.execute("PRAGMA quick_check").fetchone() == ("ok",)
        for table in ("cache_messages", "cache_index"):
            values = db.execute(f"SELECT folder,COUNT(*),COUNT(DISTINCT uid),MIN(uid),MAX(uid) FROM {table} GROUP BY folder").fetchall()
            assert len(values) == row["populated_folders"]
            assert all(value[1:] == (row["messages"], row["messages"], 1, row["messages"]) for value in values), values
        for uid, raw in db.execute("SELECT uid,raw FROM cache_messages"):
            assert f"Subject: synthetic {uid}\r\n".encode() in raw
            assert f"Message-ID: <{uid}@example.test>\r\n".encode() in raw
print(f"Validated {len(rows)} syncs and 9 databases, every populated folder")
