"""Repack the reviewed OCSR archive with staged hotfix files, preserving other entries."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--original', type=Path, required=True)
    p.add_argument('--staging', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        raise FileExistsError(a.output)
    prefix = 'MoleculeStudio/'
    updates = {'ocsr_service.py', 'OCSR-HOTFIX.txt'}
    for folder in ('ocsr_runtime/venv/Scripts', 'ocsr_runtime/python/cpython-3.10-windows-x86_64-none'):
        updates.update(str(f.relative_to(a.staging)).replace('\\', '/') for f in (a.staging / folder).glob('*.dll'))
    updates.update(str(f.relative_to(a.staging)).replace('\\', '/') for f in (a.staging / 'legal/OCSR/msvc-runtime').iterdir() if f.is_file())
    manifest_name = prefix + 'MANIFEST-OCSR.json'
    with zipfile.ZipFile(a.original) as src:
        manifest = json.loads(src.read(manifest_name))
        manifest['package_revision'] = 'hotfix1'
        for name in sorted(updates):
            data = (a.staging / name).read_bytes()
            manifest['files'][name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        with zipfile.ZipFile(a.output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as dst:
            for entry in src.infolist():
                if entry.filename == manifest_name or entry.filename.removeprefix(prefix) in updates:
                    continue
                dst.writestr(entry, src.read(entry), compress_type=entry.compress_type, compresslevel=1)
            for name in sorted(updates):
                dst.write(a.staging / name, prefix + name)
            dst.writestr(manifest_name, json.dumps(manifest, ensure_ascii=False, indent=2))
    with zipfile.ZipFile(a.output) as z:
        bad = z.testzip()
        if bad:
            raise ValueError('CRC mismatch: ' + bad)
        for name in updates:
            assert hashlib.sha256(z.read(prefix + name)).hexdigest() == manifest['files'][name]['sha256']
    digest = hashlib.file_digest(a.output.open('rb'), 'sha256').hexdigest()
    a.output.with_suffix('.sha256.txt').write_text(digest + '  ' + a.output.name + '\n', encoding='ascii')
    print(json.dumps({'file': str(a.output), 'bytes': a.output.stat().st_size, 'sha256': digest, 'verified': True}))


if __name__ == '__main__':
    main()
