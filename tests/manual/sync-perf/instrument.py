"""Generate fail-closed timing-only source copies; never edit product sources."""
import pathlib
import re
import sys

root, output = map(pathlib.Path, sys.argv[1:])


def instrument(relative, functions):
    text = (root / "src" / relative).read_text(encoding="utf-8")
    text = '#include "perf.h"\n' + text
    for function, label in functions:
        pattern = rf"({re.escape(function)}\([^{{;]*\)\s*\{{)"
        text, count = re.subn(pattern, lambda m: m[1] + f'\n    perf::Scope perfScope("{label}");', text)
        if count != 1:
            raise RuntimeError(f"{relative}: {function}: {count} matches")
    path = output / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


path = instrument("mail/mailservice.cpp", [])
text = path.read_text(encoding="utf-8")
needle = "QString runJob(KJob &job)\n{"
assert text.count(needle) == 1
text = text.replace(needle, needle + "\n    perf::Scope perfJob(job.metaObject()->className());")
path.write_text(text, encoding="utf-8")
path = instrument("storage/accountrepository.cpp", [
    ("AccountRepository::" + name, "db." + name) for name in
    ["prepareSync", "recentBoundary", "beginSync", "reuseMessage", "stageMessages", "commitSync", "abortSync"]
])
text = path.read_text(encoding="utf-8")
for name in ["prepareSync", "recentBoundary", "beginSync", "reuseMessage", "stageMessages", "commitSync", "abortSync"]:
    start = text.index(f"AccountRepository::{name}(")
    begin = text.index("QMetaObject::invokeMethod(m_worker,", start)
    brace = text.index("{", begin)
    text = text[:brace + 1] + f'\n        perf::Scope workerScope("worker.{name}");' + text[brace + 1:]
path.write_text(text, encoding="utf-8")
instrument("mime/mimeparser.cpp", [("MimeParser::parse", "mime.parse")])
instrument("core/diagnostics.cpp", [("appendRecord", "diagnostics.append")])
# Response delay is a controlled server turnaround delay, not a real WAN.
text = (root / "tests/mailtestserver.h").read_text(encoding="utf-8")
assert text.count("                    handle(line);") == 1
text = text.replace("                    handle(line);",
    "                    if (commandDelayMs) QTimer::singleShot(commandDelayMs, this, [this, line] { handle(line); });\n"
    "                    else handle(line);")
text = text.replace("    int fullFetchCount = 0;", "    int commandDelayMs = 0;\n    int fullFetchCount = 0;")
text = text.replace("    void handle(const QByteArray &line)\n    {",
    '    void handle(const QByteArray &line)\n    {\n        perf::Scope serverScope("server.handle");')
(output / "perfserver.h").write_text('#include "perf.h"\n' + text, encoding="utf-8")
