"""Split a published AI addon ZIP into AIBasic and AIAdvanced component packages.

Rules (see README.md / installer/README.md):
  * AIAdvanced = every payload file under MoleculeStudio/ai_plugins/**
  * AIBasic    = every other payload file, keeping the MoleculeStudio/ prefix
  * the stale MANIFEST-AI.json is dropped in both outputs; each output gets a
    freshly generated MANIFEST-<Component>.json describing exactly its files

The tool copies compressed payload bytes with a single decompress/recompress pass,
verifies CRCs of both outputs, and asserts that the union of both payload file sets
is exactly the original payload set (minus the old manifest). It prints size and
SHA-256 for both packages; deterministic names keep the original version string.
"""
import argparse
import hashlib
import json
import shutil
import sys
import zipfile
from pathlib import Path

PREFIX = 'MoleculeStudio/'
OLD_MANIFEST = PREFIX + 'MANIFEST-AI.json'
PLUGIN_PREFIX = PREFIX + 'ai_plugins/'
CHUNK = 1024 * 1024


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def copy_entry(src, dst, info, hasher):
    out = zipfile.ZipInfo(info.filename, info.date_time)
    out.compress_type = info.compress_type
    out.external_attr = info.external_attr
    out.internal_attr = info.internal_attr
    out.create_system = info.create_system
    with src.open(info) as r, dst.open(out, 'w', force_zip64=True) as w:
        while True:
            block = r.read(CHUNK)
            if not block:
                break
            hasher.update(block)
            w.write(block)


def write_manifest(dst, name, component, version, revision, files):
    manifest = {
        'version': version,
        'package': component,
        'package_revision': revision,
        'files': files,
    }
    info = zipfile.ZipInfo(PREFIX + name, (2026, 11, 8, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    dst.writestr(PREFIX + name, json.dumps(manifest, ensure_ascii=False, indent=2))


def split(source, output, version=None, revision=None):
    source = Path(source)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(source) as src:
        if OLD_MANIFEST not in src.namelist():
            raise SystemExit('source archive has no ' + OLD_MANIFEST)
        old = json.loads(src.read(OLD_MANIFEST))
        version = version or old['version']
        revision = revision or (str(old.get('package_revision', '')) + '+split').lstrip('+')
        basic_name = 'MoleculeStudio-' + version + '-AIBasic-addon-win64.zip'
        advanced_name = 'MoleculeStudio-' + version + '-AIAdvanced-addon-win64.zip'
        basic_path = output / basic_name
        advanced_path = output / advanced_name
        for path in (basic_path, advanced_path):
            if path.exists():
                raise SystemExit('refusing to overwrite ' + str(path))
        records = {'AIBasic': {}, 'AIAdvanced': {}}
        counts = {'AIBasic': 0, 'AIAdvanced': 0}
        with zipfile.ZipFile(basic_path, 'x', zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as basic, \
                zipfile.ZipFile(advanced_path, 'x', zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as advanced:
            for info in src.infolist():
                if info.filename == OLD_MANIFEST or info.is_dir():
                    continue
                if not info.filename.startswith(PREFIX):
                    raise SystemExit('unexpected archive root: ' + info.filename)
                component = 'AIAdvanced' if info.filename.startswith(PLUGIN_PREFIX) else 'AIBasic'
                hasher = hashlib.sha256()
                copy_entry(src, basic if component == 'AIBasic' else advanced, info, hasher)
                records[component][info.filename[len(PREFIX):]] = {
                    'bytes': info.file_size,
                    'sha256': hasher.hexdigest(),
                }
                counts[component] += 1
            for component, path in (('AIBasic', basic_path), ('AIAdvanced', advanced_path)):
                write_manifest(advanced if component == 'AIAdvanced' else basic,
                               'MANIFEST-' + component + '.json', component, version, revision,
                               records[component])
    verify(source, basic_path, advanced_path, records, counts, version)
    rows = []
    for component, path in (('AIBasic', basic_path), ('AIAdvanced', advanced_path)):
        rows.append({
            'component': component,
            'file': path.name,
            'bytes': path.stat().st_size,
            'sha256': digest(path),
            'entries': counts[component] + 1,
            'version': version,
        })
    return rows


def verify(source, basic_path, advanced_path, records, counts, version):
    with zipfile.ZipFile(source) as src:
        original = {i.filename[len(PREFIX):]: i for i in src.infolist()
                    if i.filename != OLD_MANIFEST and not i.is_dir()}
        expected = {}
        for name, info in original.items():
            expected[name] = info.file_size
    union = {}
    for component, path in (('AIBasic', basic_path), ('AIAdvanced', advanced_path)):
        with zipfile.ZipFile(path) as z:
            bad = z.testzip()
            if bad:
                raise SystemExit('CRC failure in ' + path.name + ': ' + bad)
            names = z.namelist()
            if len(names) != len(set(names)):
                raise SystemExit('duplicate entries in ' + path.name)
            manifest_name = PREFIX + 'MANIFEST-' + component + '.json'
            if manifest_name not in names:
                raise SystemExit('missing manifest in ' + path.name)
            stored = json.loads(z.read(manifest_name))
            if stored['version'] != version or stored['package'] != component:
                raise SystemExit('manifest metadata mismatch in ' + path.name)
            listed = set(stored['files']) | {'MANIFEST-' + component + '.json'}
            actual = {n[len(PREFIX):] for n in z.namelist() if n.startswith(PREFIX)}
            if listed != actual:
                raise SystemExit('manifest does not list every entry in ' + path.name)
            for info in z.infolist():
                name = info.filename
                rel = name[len(PREFIX):]
                if name == manifest_name:
                    continue
                if not name.startswith(PREFIX):
                    raise SystemExit('bad root in ' + path.name + ': ' + name)
                if component == 'AIAdvanced' and not name.startswith(PLUGIN_PREFIX):
                    raise SystemExit('AIAdvanced contains non plugin file: ' + name)
                if component == 'AIBasic' and name.startswith(PLUGIN_PREFIX):
                    raise SystemExit('AIBasic contains plugin file: ' + name)
                if rel not in expected:
                    raise SystemExit('unexpected file in ' + path.name + ': ' + rel)
                if rel in union:
                    raise SystemExit('file duplicated across outputs: ' + rel)
                union[rel] = info.file_size
                with z.open(info) as f:
                    if hashlib.file_digest(f, 'sha256').hexdigest() != records[component][rel]['sha256']:
                        raise SystemExit('hash mismatch in ' + path.name + ': ' + rel)
                if info.file_size != expected[rel]:
                    raise SystemExit('size mismatch in ' + path.name + ': ' + rel)
        if counts[component] != len(records[component]):
            raise SystemExit('entry count mismatch for ' + component)
    if set(union) != set(expected):
        missing = sorted(set(expected) - set(union))[:5]
        extra = sorted(set(union) - set(expected))[:5]
        raise SystemExit('payload set mismatch: missing=' + repr(missing) + ' extra=' + repr(extra))
    if counts['AIBasic'] + counts['AIAdvanced'] != len(expected):
        raise SystemExit('combined entry count mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='published AI addon ZIP')
    parser.add_argument('--output', type=Path, required=True, help='directory for the two component ZIPs')
    parser.add_argument('--version', help='defaults to the source MANIFEST-AI.json version')
    parser.add_argument('--revision', help='package_revision written into both manifests')
    args = parser.parse_args()
    rows = split(args.source, args.output, args.version, args.revision)
    report = args.output / 'AI_SPLIT_RESULT.json'
    report.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print(json.dumps(rows, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
