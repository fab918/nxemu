"""Prepare, but do not apply, a targeted cache bounds fix and a source-derived test.

The harness compiles the actual affected method bodies with fake GPU memory.
It intentionally does not test the renderer, serializer or real CityHash itself.
"""
import difflib
from pathlib import Path
import subprocess

REPO = Path(__file__).resolve().parents[2]
WORK = REPO / 'build/shader-cache-bounds-fix'
SOURCE = REPO / 'src/yuzu_video_core/shader_environment.cpp'


def proposed(source):
    edits = [
        ('(address < cached_highest || (frozen && address == cached_highest))',
         'address <= cached_highest'),
        ('    cached_highest = start_address + static_cast<u32>(size_bytes);',
         '    ASSERT(size_bytes >= INST_SIZE && size_bytes % INST_SIZE == 0);\n'
         '    cached_highest = start_address + static_cast<u32>(size_bytes - INST_SIZE);'),
        ('    // Preserve the same padding as SetCachedSize; subsequent pipeline variants\n'
         '    // share the immutable bytes until ShaderCache invalidates this ShaderInfo.',
         '    // size_bytes includes the final instruction. Share exactly this range\n'
         '    // until ShaderCache invalidates this ShaderInfo.'),
    ]
    for old, new in edits:
        expected = 2 if old.startswith('    cached_highest') else 1
        if source.count(old) != expected:
            raise ValueError(f'Unexpected source for {old!r}')
        source = source.replace(old, new)
    return source


def methods(source):
    names = ['ReadInstruction', 'Analyze', 'SetCachedSize', 'SetCachedCode',
             'ShareCachedCode', 'CachedSizeWords', 'CachedSizeBytes', 'ReadSizeBytes',
             'TryFindSize']
    result = []
    for name in names:
        anchor = source.index('GenericEnvironment::' + name + '(')
        start = source.rfind('\n', 0, anchor) + 1
        opening = source.index('{', anchor)
        depth = 1
        end = opening + 1
        while depth:
            if source[end] == '{': depth += 1
            elif source[end] == '}': depth -= 1
            end += 1
        result.append(source[start:end])
    return '\n\n'.join(result)


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    original = SOURCE.read_text(encoding='utf-8')
    if 'size_bytes - INST_SIZE' in original:
        original = original.replace('address <= cached_highest',
            '(address < cached_highest || (frozen && address == cached_highest))')
        original = original.replace(
            '    ASSERT(size_bytes >= INST_SIZE && size_bytes % INST_SIZE == 0);\n'
            '    cached_highest = start_address + static_cast<u32>(size_bytes - INST_SIZE);',
            '    cached_highest = start_address + static_cast<u32>(size_bytes);')
        original = original.replace(
            '    // size_bytes includes the final instruction. Share exactly this range\n'
            '    // until ShaderCache invalidates this ShaderInfo.',
            '    // Preserve the same padding as SetCachedSize; subsequent pipeline variants\n'
            '    // share the immutable bytes until ShaderCache invalidates this ShaderInfo.')
    fixed = proposed(original)
    (WORK/'shader_environment.cpp').write_text(fixed, encoding='utf-8')
    patch = ''.join(difflib.unified_diff(original.splitlines(True), fixed.splitlines(True),
        fromfile='a/src/yuzu_video_core/shader_environment.cpp',
        tofile='b/src/yuzu_video_core/shader_environment.cpp'))
    (WORK/'cache-bounds-fix.patch').write_text(patch, encoding='utf-8')
    harness = Path(__file__).with_name('tests')/'cache_bounds_harness.cpp'
    for name, source in [('before', original), ('after', fixed)]:
        text = harness.read_text(encoding='utf-8').replace('// METHOD_BODIES', methods(source))
        (WORK/(name+'.cpp')).write_text(text, encoding='utf-8')
    size_only = fixed.replace('address <= cached_highest',
        '(address < cached_highest || (frozen && address == cached_highest))')
    (WORK/'size_only.cpp').write_text(harness.read_text(encoding='utf-8').replace(
        '// METHOD_BODIES', methods(size_only)), encoding='utf-8')
    cmake = '''cmake_minimum_required(VERSION 3.22)
project(cache_bounds_regression LANGUAGES CXX)
foreach(name before after size_only)
  add_executable(${name} ${name}.cpp)
  target_compile_features(${name} PRIVATE cxx_std_20)
endforeach()
enable_testing()
add_test(NAME old_behavior_detected COMMAND before)
set_tests_properties(old_behavior_detected PROPERTIES WILL_FAIL TRUE)
add_test(NAME corrected_bounds COMMAND after)
add_test(NAME incomplete_fix_detected COMMAND size_only)
set_tests_properties(incomplete_fix_detected PROPERTIES WILL_FAIL TRUE)
'''
    (WORK/'CMakeLists.txt').write_text(cmake, encoding='utf-8')
    subprocess.run(['cmake','-S',str(WORK),'-B',str(WORK/'out'),'-A','x64'],check=True)
    subprocess.run(['cmake','--build',str(WORK/'out'),'--config','Release'],check=True)
    subprocess.run(['ctest','--test-dir',str(WORK/'out'),'-C','Release','--output-on-failure'],check=True)
    print(f'Prepared only; production source unchanged. Patch: {WORK / "cache-bounds-fix.patch"}')


if __name__ == '__main__':
    main()
