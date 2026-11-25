"""Manual update client for the Debian desktop package."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from urllib.parse import unquote, urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener
from urllib.error import HTTPError


PACKAGE = 'moleculestudio-native'
REPOSITORY = '/chaobada10086/MoleculeStudio'
FEED = f'https://raw.githubusercontent.com{REPOSITORY}/main/updates/linux-stable.json'
MAX_MANIFEST = 256 * 1024
MAX_PACKAGE = 512 * 1024 * 1024
DOWNLOAD_HOSTS = {'github.com', 'release-assets.githubusercontent.com', 'objects.githubusercontent.com'}


class OfficialRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        target = urlsplit(new_url)
        if request.full_url == FEED or target.scheme != 'https' or target.netloc not in DOWNLOAD_HOSTS:
            raise ValueError('更新请求跳转到了未知站点')
        return super().redirect_request(request, fp, code, message, headers, new_url)


urlopen = build_opener(OfficialRedirect()).open


def debian(*args):
    return subprocess.check_output(args, stderr=subprocess.DEVNULL, text=True, timeout=10).strip()


def release_name(version, arch):
    if not isinstance(version, str) or not re.fullmatch(r'[0-9][A-Za-z0-9.+~:-]{0,95}', version):
        raise ValueError('版本清单中的 Debian 版本无效')
    if not isinstance(arch, str) or not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,23}', arch):
        raise ValueError('版本清单中的架构无效')
    return f'{PACKAGE}_{version}_{arch}.deb'


def checked_release(row):
    if not isinstance(row, dict):
        raise ValueError('更新包信息无效')
    version, arch = row.get('version'), row.get('arch')
    name = release_name(version, arch)
    size, digest, url = row.get('bytes'), row.get('sha256'), row.get('url')
    if type(size) is not int or not 0 < size <= MAX_PACKAGE:
        raise ValueError('更新包大小无效')
    if not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest):
        raise ValueError('更新包的 SHA-256 无效')
    if not isinstance(url, str) or not re.fullmatch(r'[!-~]+', url):
        raise ValueError('更新包下载地址无效')
    parts = urlsplit(url)
    prefix = REPOSITORY + '/releases/download/'
    path = unquote(parts.path)
    fields = path[len(prefix):].split('/') if path.startswith(prefix) else []
    if (parts.scheme != 'https' or parts.netloc != 'github.com' or parts.query or parts.fragment or
            len(fields) != 2 or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+~-]{0,95}', fields[0]) or fields[1] != name):
        raise ValueError('更新包必须来自官方 GitHub Releases')
    return version, arch, name, size, digest, url


def find_release(arch):
    try:
        with urlopen(Request(FEED, headers={'User-Agent': PACKAGE}), timeout=12) as response:
            if response.geturl() != FEED:
                raise ValueError('版本清单被重定向')
            data = response.read(MAX_MANIFEST + 1)
    except HTTPError as error:
        if error.code == 404:
            raise ValueError('官方尚未发布 Linux 更新清单') from error
        raise
    if len(data) > MAX_MANIFEST:
        raise ValueError('版本清单过大')
    document = json.loads(data)
    if not isinstance(document, dict) or type(document.get('schema')) is not int or document['schema'] != 1:
        raise ValueError('无法读取 Linux 更新协议版本')
    rows = document.get('packages')
    if not isinstance(rows, list):
        raise ValueError('版本清单缺少 Linux 软件包')
    matches = [checked_release(row) for row in rows if isinstance(row, dict) and row.get('arch') == arch]
    if len(matches) != 1:
        raise ValueError('当前架构没有唯一可用的 Linux 更新包')
    return matches[0]


def check():
    arch = debian('/usr/bin/dpkg', '--print-architecture')
    installed = debian('/usr/bin/dpkg-query', '-W', '-f=${Version}', PACKAGE)
    release_name(installed, arch)
    version, arch, name, size, digest, url = find_release(arch)
    newer = subprocess.run(['/usr/bin/dpkg', '--compare-versions', version, 'gt', installed], timeout=10)
    if newer.returncode == 1:
        return ('current', installed)
    newer.check_returncode()
    return ('available', installed, version, arch, name, str(size), digest, url)


def download(row, destination):
    version, arch, name, size, digest, url = checked_release(row)
    if arch != debian('/usr/bin/dpkg', '--print-architecture'):
        raise ValueError('更新包架构与此系统不同')
    path = Path(destination)
    partial = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.moleculestudio-', suffix='.deb', dir=path.parent,
                                         delete=False) as output:
            partial = Path(output.name)
            checksum = hashlib.sha256()
            written = 0
            with urlopen(Request(url, headers={'User-Agent': PACKAGE}), timeout=15) as response:
                redirected = urlsplit(response.geturl())
                if redirected.scheme != 'https' or redirected.netloc not in DOWNLOAD_HOSTS:
                    raise ValueError('更新包跳转到了未知的下载站点')
                while chunk := response.read(256 * 1024):
                    written += len(chunk)
                    if written > size:
                        raise ValueError('更新包大于清单中的大小')
                    checksum.update(chunk)
                    output.write(chunk)
            if written != size or checksum.hexdigest() != digest:
                raise ValueError('更新包的大小或 SHA-256 校验失败')
            output.flush()
            os.fsync(output.fileno())
        fields = debian('/usr/bin/dpkg-deb', '-f', str(partial), 'Package', 'Version', 'Architecture')
        metadata = dict(line.split(': ', 1) for line in fields.splitlines())
        if (metadata.get('Package'), metadata.get('Version'), metadata.get('Architecture')) != (PACKAGE, version, arch):
            raise ValueError('更新包的名称、版本或架构不符合清单')
        os.replace(partial, path)
        return name
    finally:
        if partial is not None:
            partial.unlink(missing_ok=True)


def main():
    try:
        if sys.argv[1:] == ['check']:
            print('\t'.join(check()))
        elif len(sys.argv) == 8 and sys.argv[1] == 'download':
            _, _, version, arch, size, digest, url, destination = sys.argv
            print(download({'version': version, 'arch': arch, 'bytes': int(size), 'sha256': digest, 'url': url}, destination))
        else:
            raise ValueError('更新命令无效')
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        sys.exit(str(error))


if __name__ == '__main__':
    main()
