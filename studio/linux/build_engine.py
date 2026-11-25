# Build from the same engine source list and overrides as the Windows executable.
from pathlib import Path
import subprocess
import sys
import os

here = Path(__file__).resolve().parent
studio = here.parent
engine = studio.parent / "engine"
sys.path.insert(0, str(studio))
from engine_sources import engine_sources

sources, overrides = engine_sources(engine, studio)
out = studio.parent / "out" / "linux-native"
out.mkdir(parents=True, exist_ok=True)
rules = []
objects = []
for rel in sources:
    if rel in ("next/render/backend_gl.cpp", "next/selection/clipboard_win32.cpp"):
        continue
    src = overrides.get(rel, engine / rel)
    obj = rel.replace("/", "_") + ".o"
    objects.append(obj)
    # Paths in explicit prerequisites are escaped for GNU make.
    original = str(src).replace(" ", "\\ ")
    rules.append(f'{obj}: {original} Makefile\n\t$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I"{(engine / rel).parent}" -MMD -MP -c "$<" -o "$@"')
lib = "libmoleculestudio-engine.a"

def gtk_flags(option):
    return subprocess.check_output(["pkg-config", option, "gtk+-3.0", "librsvg-2.0", "libpng"], text=True).strip()

rules[:0] = [f'CPPFLAGS += -I"{engine}" -I"{studio}"', 'CXXFLAGS += -std=c++17 -O2 -fPIC',
             f'GTK_CFLAGS := {gtk_flags("--cflags")}', f'GTK_LIBS := {gtk_flags("--libs")}',
             'all: moleculestudio', lib + ': ' + ' '.join(objects) + ' Makefile' +
             '\n\trm -f "$@.tmp"\n\t$(AR) rcs "$@.tmp" $(filter %.o,$^)\n\tmv "$@.tmp" "$@"']
frontend = []
for name in ("main", "font", "clipboard"):
    obj = f"gtk_{name}.o"
    frontend.append(obj)
    source = str(here / f"{name}.cpp").replace(" ", "\\ ")
    rules.append(f'{obj}: {source} Makefile\n\t$(CXX) $(CPPFLAGS) $(GTK_CFLAGS) $(CXXFLAGS) -MMD -MP -c "$<" -o "$@"')
rules.append('moleculestudio: ' + ' '.join(frontend + [lib]) +
             '\n\t$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(GTK_LIBS) -o "$@"')
rules.append('-include ' + ' '.join(obj[:-2] + '.d' for obj in objects + frontend))
makefile = "\n".join(rules) + "\n"
target = out / "Makefile"
if not target.exists() or target.read_text() != makefile:
    target.write_text(makefile)
jobs = os.environ.get("MOLECULESTUDIO_BUILD_JOBS", str(min(os.cpu_count() or 1, 4)))
subprocess.run(["make", "-C", str(out), "-j", jobs], check=True)
