"""Generate stable.json from already-published GitHub release assets.

Explicit asset names prevent accidentally selecting an old Core or another ZIP.
GitHub's published SHA-256 and sizes are required; no local path is published.
"""
import argparse
import datetime
import json
import re
import urllib.request
from pathlib import Path
from urllib.parse import quote

REPO = 'chaobada10086/MoleculeStudio'

def generate(release, names, versions):
    if release.get('draft') or release.get('prerelease'):
        raise ValueError('Stable channel requires a published, non-prerelease release')
    assets = {a['name']: a for a in release['assets']}
    components = []
    for key, name, version in zip(('Core', 'AI', 'OCSR'), names, versions):
        a = assets[name]
        digest = a.get('digest', '')
        if not re.fullmatch(r'sha256:[0-9a-f]{64}', digest):
            raise ValueError(f'GitHub has no SHA-256 for {name}; wait for upload completion')
        url = a['browser_download_url']
        if not url.startswith(f'https://github.com/{REPO}/releases/download/'):
            raise ValueError('Unexpected asset URL')
        if a.get('state', 'uploaded') != 'uploaded' or a['size'] <= 0:
            raise ValueError('Asset upload is incomplete')
        components.append(dict(id=key, version=version, name=name, url=url,
                               sha256=digest[7:], bytes=a['size'],
                               requires_core_version=None if key == 'Core' else versions[0]))
    return dict(schema=1, min_installer_protocol=1, channel='stable',
                release=release['tag_name'],
                generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                components=components)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tag', required=True)
    for key in ('core', 'ai', 'ocsr'):
        p.add_argument('--'+key+'-asset', required=True)
        p.add_argument('--'+key+'-version', required=True)
    p.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]/'updates/stable.json')
    args = p.parse_args()
    request = urllib.request.Request(f'https://api.github.com/repos/{REPO}/releases/tags/{quote(args.tag, safe="")}', headers={'User-Agent': 'MoleculeStudio-release-tools'})
    with urllib.request.urlopen(request, timeout=30) as r:
        release = json.load(r)
    result = generate(release, [getattr(args, k+'_asset') for k in ('core','ai','ocsr')],
                      [getattr(args, k+'_version') for k in ('core','ai','ocsr')])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp = args.output.with_suffix('.tmp')
    temp.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    temp.replace(args.output)
    print(args.output)
