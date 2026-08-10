#!/usr/bin/env python3
"""Extracts every GLSL string literal from gfx.cpp and compiles it with
glslangValidator. A shader typo does not fail the APK build - it fails at
runtime as a black screen with the reason buried in logcat, so it is worth
catching here."""
import re, subprocess, sys, tempfile, os

SRC = os.path.join(os.path.dirname(__file__), '..', 'app', 'src', 'main', 'cpp', 'gfx.cpp')
text = open(SRC).read()

# static const char* kNameVS = R"(...)";
pattern = re.compile(r'static const char\* (k\w+) = R"\((.*?)\)";', re.S)
shaders = pattern.findall(text)
if not shaders:
    print("no shaders found - did the source format change?")
    sys.exit(1)

fails = 0
for name, body in shaders:
    stage = 'vert' if name.endswith('VS') else 'frag'
    with tempfile.NamedTemporaryFile('w', suffix='.' + stage, delete=False) as f:
        f.write(body)
        path = f.name
    # Plain validation, no SPIR-V: these are '#version 300 es' sources and
    # SPIR-V generation would demand 310+ for reasons that do not apply here.
    proc = subprocess.run(['glslangValidator', path],
                          capture_output=True, text=True)
    os.unlink(path)
    status = 'ok' if proc.returncode == 0 else 'FAIL'
    if proc.returncode != 0:
        fails += 1
    print(f"  {status:4}  {name} ({stage}, {len(body.splitlines())} lines)")
    if proc.returncode != 0:
        print('        ' + (proc.stdout + proc.stderr).strip().replace('\n', '\n        '))

print(f"\n{len(shaders)} shaders, {fails} failures")
sys.exit(1 if fails else 0)
