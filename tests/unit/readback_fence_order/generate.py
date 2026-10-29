"""Compile the production fence method and query callback against a deterministic fake GPU."""
import pathlib
import sys

root, output = map(pathlib.Path, sys.argv[1:3])
fences = (root / 'src/yuzu_video_core/fence_manager.h').read_text()
queries = (root / 'src/yuzu_video_core/query_cache/query_cache.h').read_text()

def braced(text, start):
    begin = text.index('{', start)
    depth, end = 1, begin + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end], end

submit, _ = braced(fences, fences.index('    bool SubmitFence('))
operation, end = braced(queries, queries.index('    std::function<void()> operation([this, is_synced'))
operation += queries[end:queries.index(';', end) + 1]
# The immediate-slot retirement branch after SignalFence must also be exercised.
retirement_start = queries.index('    if (is_synced) {', end)
retirement, _ = braced(queries, retirement_start)
fixture = (pathlib.Path(__file__).parent / 'fixture.cpp.in').read_text()
output.write_text(fixture.replace('@SUBMIT_FENCE@', submit)
                  .replace('@QUERY_OPERATION@', operation)
                  .replace('@QUERY_RETIREMENT@', retirement))
