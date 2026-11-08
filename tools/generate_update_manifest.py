"""Generate the schema 2 stable.json manifest for the four component release.

Normal releases read the already-published GitHub release assets (published
SHA-256 and sizes are required; local paths are never published). For offline
verification pass --local-dir (or per component --*-path) to hash local ZIPs and
build the exact same schema 2 document without network access.

Components: Core, AIBasic, AIAdvanced, OCSR.
Dependencies: AIBasic requires Core, AIAdvanced requires AIBasic, OCSR requires Core.
"""
import argparse
import datetime
import hashlib
import json
import re
import urllib.request
from pathlib import Path
from urllib.parse import quote

REPO = 'chaobada10086/MoleculeStudio'
COMPONENTS = (
    ('core', 'Core'),
    ('aibasic', 'AIBasic'),
    ('aiadvanced', 'AIAdvanced'),
    ('ocsr', 'OCSR'),
)
REQUIRES = {'Core': [], 'AIBasic': ['Core'], 'AIAdvanced': ['AIBasic'], 'OCSR': ['Core']}


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


DISPLAY = {key: name for key, name in COMPONENTS}


def build_components(rows, core_version):
    components = []
    for key, name, version, size, sha, url in rows:
        component_id = DISPLAY[key]
        if not name.endswith('.zip') or '/' in name or '\\' in name:
            raise ValueError('invalid asset name: ' + name)
        if not re.fullmatch(r'[0-9a-f]{64}', sha):
            raise ValueError('invalid SHA-256 for ' + name)
        if size <= 0:
            raise ValueError('invalid size for ' + name)
        if not url.startswith(f'https://github.com/{REPO}/releases/download/'):
            raise ValueError('unexpected asset URL for ' + name)
        if not url.endswith('/' + name):
            raise ValueError('asset URL does not end with the asset name: ' + name)
        if version is None or not version.strip():
            raise ValueError('missing version for ' + key)
        components.append(dict(
            id=component_id, version=version, name=name, url=url, sha256=sha, bytes=size,
            requires=list(REQUIRES[component_id]),
            requires_core_version=None if component_id == 'Core' else core_version))
    if len({c['name'] for c in components}) != len(components):
        raise ValueError('asset names must be unique')
    ids = {c['id'] for c in components}
    for c in components:
        for dep in c['requires']:
            if dep not in ids:
                raise ValueError(f'{c["id"]} requires missing component {dep}')
    advanced = next(c for c in components if c['id'] == 'AIAdvanced')
    if 'AIBasic' not in advanced['requires']:
        raise ValueError('AIAdvanced must require AIBasic')
    return components


def from_github(tag, asset_names, versions):
    request = urllib.request.Request(
        f'https://api.github.com/repos/{REPO}/releases/tags/{quote(tag, safe="")}',
        headers={'User-Agent': 'MoleculeStudio-release-tools'})
    with urllib.request.urlopen(request, timeout=30) as r:
        release = json.load(r)
    if release.get('draft') or release.get('prerelease'):
        raise ValueError('Stable channel requires a published, non-prerelease release')
    assets = {a['name']: a for a in release['assets']}
    rows = []
    for (key, _), name, version in zip(COMPONENTS, asset_names, versions):
        asset = assets.get(name)
        if asset is None:
            raise ValueError('release has no asset named ' + name)
        digest_value = asset.get('digest', '')
        if not re.fullmatch(r'sha256:[0-9a-f]{64}', digest_value):
            raise ValueError(f'GitHub has no SHA-256 for {name}; wait for upload completion')
        url = asset['browser_download_url']
        if asset.get('state', 'uploaded') != 'uploaded':
            raise ValueError('asset upload is incomplete: ' + name)
        rows.append((key, name, version, asset['size'], digest_value[7:], url))
    return rows


def from_local(tag, directory, paths, asset_names, versions):
    rows = []
    for (key, _), name, version in zip(COMPONENTS, asset_names, versions):
        if paths.get(key) is not None:
            path = Path(paths[key])
        elif directory is not None:
            path = Path(directory) / name
        else:
            raise ValueError('local mode needs --local-dir or --' + key + '-path')
        if not path.is_file():
            raise ValueError('missing local package: ' + str(path))
        rows.append((key, name, version, path.stat().st_size, digest(path),
                     f'https://github.com/{REPO}/releases/download/{quote(tag, safe="")}/{name}'))
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tag', required=True)
    for key, _ in COMPONENTS:
        p.add_argument('--' + key + '-asset', required=True)
        p.add_argument('--' + key + '-version', required=True)
        p.add_argument('--' + key + '-path', type=Path,
                       help='local ZIP path (offline mode; nothing is published)')
    p.add_argument('--local-dir', type=Path, help='directory holding the four local ZIPs (offline mode)')
    p.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'updates/stable.json')
    args = p.parse_args()
    asset_names = [getattr(args, key + '_asset') for key, _ in COMPONENTS]
    versions = [getattr(args, key + '_version') for key, _ in COMPONENTS]
    if len(set(asset_names)) != len(asset_names):
        raise SystemExit('asset names must be unique')
    local_paths = {key: getattr(args, key + '_path') for key, _ in COMPONENTS}
    if args.local_dir is not None or any(v is not None for v in local_paths.values()):
        rows = from_local(args.tag, args.local_dir, local_paths, asset_names, versions)
    else:
        rows = from_github(args.tag, asset_names, versions)
    core_version = next(v for (k, _), v in zip(COMPONENTS, versions) if k == 'core')
    components = build_components(rows, core_version)
    result = dict(
        schema=2, min_installer_protocol=2, channel='stable', release=args.tag,
        generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        components=components)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp = args.output.with_suffix('.tmp')
    temp.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    temp.replace(args.output)
    print(args.output)


if __name__ == '__main__':
    main()
