"""Package four pre-audited staging directories into the published component ZIPs.

Staging layout (never package a live installation):

    staging/Core/MoleculeStudio/**
    staging/AIBasic/MoleculeStudio/**
    staging/AIAdvanced/MoleculeStudio/ai_plugins/**
    staging/OCSR/MoleculeStudio/**

Rules:
  * every ZIP keeps the single MoleculeStudio/ root and the same version string
  * AIAdvanced may only contain MoleculeStudio/ai_plugins/** (plus its manifest)
  * AIBasic must not contain MoleculeStudio/ai_plugins/** (use tools/split_ai_package.py)
  * each package gets its own MANIFEST-<Component>.json regenerated from the files
  * sensitive/private file scan and per-file SHA-256 are unchanged
  * every output ZIP is re-opened and its CRC checked before reporting
"""
from pathlib import Path
import argparse
import concurrent.futures
import hashlib
import json
import zipfile
from audit_release import audit

COMPONENTS = ('Core', 'AIBasic', 'AIAdvanced', 'OCSR')
SUFFIX = {
    'Core': 'Core-win64',
    'AIBasic': 'AIBasic-addon-win64',
    'AIAdvanced': 'AIAdvanced-addon-win64',
    'OCSR': 'OCSR-addon-win64',
}
REQUIRES = {
    'Core': [],
    'AIBasic': ['Core'],
    'AIAdvanced': ['AIBasic'],
    'OCSR': ['Core'],
}


def digest(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def validate_layout(base, kind):
    if kind == 'AIAdvanced':
        stray = [p.relative_to(base).as_posix() for p in base.rglob('*')
                 if p.is_file() and not p.relative_to(base).as_posix().startswith('ai_plugins/')]
        if stray:
            raise RuntimeError('AIAdvanced staging contains non plugin files: ' + repr(stray[:5]))
    if kind == 'AIBasic':
        stray = [p.relative_to(base).as_posix() for p in base.rglob('*')
                 if p.is_file() and p.relative_to(base).as_posix().startswith('ai_plugins/')]
        if stray:
            raise RuntimeError('AIBasic staging contains ai_plugins files; split the AI package first')


def package(staging, out, version, kind):
    base = staging / kind / 'MoleculeStudio'
    if not base.is_dir():
        raise RuntimeError('missing staging directory: ' + str(base))
    result = audit(base)
    if result['findings']:
        raise RuntimeError(json.dumps(result))
    validate_layout(base, kind)
    files = sorted(p for p in base.rglob('*') if p.is_file() and p.name != 'MANIFEST-' + kind + '.json')
    manifest = {
        'version': version,
        'package': kind,
        'files': {p.relative_to(base).as_posix(): {'bytes': p.stat().st_size, 'sha256': digest(p)} for p in files},
    }
    mp = base / ('MANIFEST-' + kind + '.json')
    mp.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf8')
    files.append(mp)
    target = out / ('MoleculeStudio-' + version + '-' + SUFFIX[kind] + '.zip')
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as z:
        for p in files:
            stored = p.name.endswith(('.zip', '.gz', '.xz', '.bz2', '.7z'))
            z.write(p, 'MoleculeStudio/' + p.relative_to(base).as_posix(),
                    compress_type=zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(target) as z:
        bad = z.testzip()
        if bad:
            raise RuntimeError('CRC failure: ' + bad)
        if len(z.namelist()) != len(files) or len(z.namelist()) != len(set(z.namelist())):
            raise RuntimeError('entry count/duplicate mismatch in ' + target.name)
    return {
        'component': kind,
        'version': version,
        'requires': REQUIRES[kind],
        'file': target.name,
        'bytes': target.stat().st_size,
        'sha256': digest(target),
        'entries': len(files),
    }


if __name__ == '__main__':
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument('--staging', type=Path, required=True)
    a.add_argument('--output', type=Path, required=True)
    a.add_argument('--version', default='0.95')
    a.add_argument('--components', default=','.join(COMPONENTS),
                   help='comma separated subset for partial rebuilds (default: all four)')
    args = a.parse_args()
    kinds = [k.strip() for k in args.components.split(',') if k.strip()]
    unknown = [k for k in kinds if k not in COMPONENTS]
    if unknown or not kinds:
        raise SystemExit('unknown components: ' + repr(unknown))
    args.output.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(kinds)) as pool:
        rows = list(pool.map(lambda k: package(args.staging, args.output, args.version, k), kinds))
    report = args.output.parent / 'RELEASE_CHECKSUMS.json'
    report.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps(rows, ensure_ascii=False, indent=2))
