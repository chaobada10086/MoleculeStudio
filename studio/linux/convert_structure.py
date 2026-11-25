import sys
from pathlib import Path

scripts = Path(__file__).resolve().parent
if not (scripts / "chemistry_open.py").is_file():
    sys.path.insert(0, str(scripts.parent))
from chemistry_open import run

sys.stdin.reconfigure(encoding="utf-8")
sys.stdout.reconfigure(encoding="utf-8")
sys.stderr.reconfigure(encoding="utf-8")
try:
    action = sys.argv[3] if len(sys.argv) > 3 else "convert"
    sys.stdout.write(run(sys.stdin.read(), sys.argv[1], sys.argv[2], action))
except (RuntimeError, ValueError) as error:
    sys.exit(str(error))
