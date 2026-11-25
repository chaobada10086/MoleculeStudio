"""Generate the Linux update manifest from the exact release .deb files."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from urllib.parse import quote


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'studio' / 'linux'))
from update import PACKAGE, REPOSITORY, checked_release, debian, release_name


def describe(path, tag):
    fields = debian('/usr/bin/dpkg-deb', '-f', str(path), 'Package', 'Version', 'Architecture')
    metadata = dict(line.split(': ', 1) for line in fields.splitlines())
    version, arch = metadata.get('Version'), metadata.get('Architecture')
    expected = release_name(version, arch)
    if metadata.get('Package') != PACKAGE or path.name != expected:
        raise ValueError(f'软件包名称或文件名不匹配：{path}')
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    url = f'https://github.com{REPOSITORY}/releases/download/{quote(tag, safe="")}/{quote(expected, safe="")}'
    row = {'version': version, 'arch': arch, 'bytes': path.stat().st_size, 'sha256': digest, 'url': url}
    checked_release(row)
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag')
    parser.add_argument('packages', nargs='+', type=Path)
    arguments = parser.parse_args()
    rows = sorted((describe(package, arguments.tag) for package in arguments.packages), key=lambda value: value['arch'])
    if len({row['arch'] for row in rows}) != len(rows):
        parser.error('每种架构只允许一个更新软件包')
    print(json.dumps({'schema': 1, 'packages': rows}, indent=2))


if __name__ == '__main__':
    main()
