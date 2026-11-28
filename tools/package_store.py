"""Build the Microsoft Store MSIX for MoleculeStudio from the verified component ZIPs.

Input  : the four release ZIPs produced by tools/package_release.py (Core / AIBasic /
         AIAdvanced / OCSR) for one version.
Output : a single x64 .msix (and optionally a signed copy for sideload testing).

The Store package differs from the portable package in these ways:
  * the manual update downloader (MoleculeStudio-OnlineSetup.exe) is removed, because a
    Store app may not install its own copy outside the Store (policy 10.2.2);
  * per-user state (interface.ini, drawing.ini, shortcuts.ini, custom_groups.xml,
    custom_templates/, OCSR caches, logs) is never shipped: the install directory is
    read-only at runtime, so the app keeps that data in %LOCALAPPDATA%\\MoleculeStudio;
  * chemistry_backend.ini is forced to a relative interpreter path, because the absolute
    path recorded by studio/build.py does not exist on the user's machine;
  * known build-only PyTorch files and local regression outputs are omitted;
  * AppxManifest.xml is rendered from installer/store/AppxManifest.xml with the identity
    values taken from Partner Center, and the icons are generated from the project icon.

Every extracted file is checked against MANIFEST-<Component>.json before it enters the
package, so a corrupted or stale ZIP fails loudly instead of shipping.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from runtime_payload import development_file

try:
    from PIL import Image
except ImportError:  # pragma: no cover - reported below
    Image = None

ROOT = Path(__file__).resolve().parents[1]
STORE = ROOT / 'installer' / 'store'
DEFAULT_ICON = ROOT / 'studio' / 'assets' / 'app-icons' / 'moleculestudio-professional.png'

COMPONENTS = ('Core', 'AIBasic', 'AIAdvanced', 'OCSR')
SUFFIX = {
    'Core': 'Core-win64',
    'AIBasic': 'AIBasic-addon-win64',
    'AIAdvanced': 'AIAdvanced-addon-win64',
    'OCSR': 'OCSR-addon-win64',
}

# Shipped by the portable package, never by the Store package.
EXCLUDE_EXACT = {
    'MoleculeStudio-OnlineSetup.exe',   # self-updater, forbidden in a Store package
    'interface.ini', 'drawing.ini', 'shortcuts.ini', 'custom_groups.xml',
    'chemistry_runtime.ini',
}
EXCLUDE_DIRS = {
    '__pycache__', 'custom_templates', 'OCSRCache', 'recognition_records',
    'ChemistryJobs', 'AIJobs', 'AIChat', 'sessions', 'home',
}
EXCLUDE_SUFFIX = {'.pyc', '.pyo'}
EXCLUDE_PREFIX = ('MANIFEST-',)      # per-component build manifests; packaging bookkeeping

# Asset name -> (width, height). "scale" files follow the MSIX qualifier convention.
ASSETS = [
    ('StoreLogo.png', 50, 50),
    ('Square44x44Logo.png', 44, 44),
    ('Square150x150Logo.png', 150, 150),
    ('Wide310x150Logo.png', 310, 150),
]
SCALES = {
    'StoreLogo': (50, 50),
    'Square44x44Logo': (44, 44),
    'Square150x150Logo': (150, 150),
    'Wide310x150Logo': (310, 150),
}
SCALE_FACTORS = (125, 150, 200, 400)
TARGETSIZES = (16, 20, 24, 30, 32, 36, 40, 44, 48, 60, 64, 72, 80, 96, 256)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    with open(path, 'rb') as f:
        digest = hashlib.sha256()
        for block in iter(lambda: f.read(1024 * 1024), b''):
            digest.update(block)
        return digest.hexdigest()


def excluded(rel):
    parts = rel.parts
    if parts and parts[0] == 'App':
        parts = parts[1:]
    if not parts:
        return True
    if any(part in EXCLUDE_DIRS for part in parts[:-1]):
        return True
    name = parts[-1]
    if name in EXCLUDE_EXACT or name.startswith(EXCLUDE_PREFIX):
        return True
    if any(name.endswith(suffix) for suffix in EXCLUDE_SUFFIX):
        return True
    return development_file(parts)


def find_archive(release, kind):
    pattern = '*-' + SUFFIX[kind] + '.zip'
    matches = sorted(release.glob(pattern))
    if not matches:
        raise SystemExit('找不到组件包 ' + SUFFIX[kind] + '（' + str(release / pattern) + '）')
    if len(matches) > 1:
        raise SystemExit('组件包不唯一，请只保留一个 ' + pattern + '：' + ', '.join(p.name for p in matches))
    return matches[0]


def stage_component(archive, stage, kind, conflicts, allow_overlay):
    import zipfile
    with zipfile.ZipFile(archive) as z:
        try:
            manifest = json.loads(z.read('MoleculeStudio/MANIFEST-' + kind + '.json'))
        except KeyError:
            raise SystemExit('组件包缺少清单 MANIFEST-' + kind + '.json：' + archive.name)
        files = manifest.get('files') or {}
        if not files:
            raise SystemExit('组件包清单为空：' + archive.name)
        written = skipped = 0
        for key, info in sorted(files.items()):
            rel = Path('App') / Path(key)
            if excluded(rel):
                skipped += 1
                continue
            target = stage / rel
            if not target.resolve().is_relative_to(stage.resolve()):
                raise SystemExit('组件包含有越界路径：' + key)
            data = z.read('MoleculeStudio/' + key)
            digest = sha256_bytes(data)
            if digest != info.get('sha256'):
                raise SystemExit('校验失败（' + kind + '）：' + key + ' 实际 ' + digest + '，期望 ' + str(info.get('sha256')))
            if target.exists():
                previous = sha256_file(target)
                if previous != digest:
                    if not allow_overlay:
                        conflicts.append(key)
                        continue
                    conflicts.append(key)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            written += 1
    return written, skipped


def normalize_backend_ini(stage):
    """Point chemistry_backend.ini at the interpreter that actually ships.

    studio/build.py records the build machine's absolute Python path, which does not exist
    on a user's machine. Rewrite only when an absolute path is found, and keep the file's
    original encoding and newline style: the app reads it through the Win32 profile APIs,
    which are picky about both. A shipped relative path is left untouched.
    """
    ini = stage / 'App' / 'chemistry_backend.ini'
    if not ini.exists():
        return None
    raw = ini.read_bytes()
    text = None
    encoding = None
    for candidate in ('utf-16', 'utf-8-sig', 'utf-8', 'latin-1'):
        try:
            text = raw.decode(candidate)
            encoding = candidate
            break
        except (UnicodeError, UnicodeDecodeError):
            continue
    if text is None:
        return ['chemistry_backend.ini 无法解码，保持原样']
    newline = '\r\n' if '\r\n' in text else '\n'
    shipped = (stage / 'App' / 'python' / 'python.exe').exists()
    default_python = 'python\\python.exe' if shipped else 'chemistry_python\\python.exe'
    notes = []
    lines = []
    for line in text.splitlines():
        key, sep, value = line.partition('=')
        if sep and key.strip().lower() in ('python', 'chemistrypython'):
            stripped = value.strip()
            absolute = bool(re.match(r'^[A-Za-z]:[\\/]', stripped)) or stripped.startswith('\\\\') or stripped.startswith('//')
            if absolute:
                relative = 'chemistry_python\\python.exe' if key.strip().lower() == 'chemistrypython' else default_python
                line = key + sep + relative
                notes.append(key.strip() + ' -> ' + relative)
        lines.append(line)
    if not notes:
        return None
    ini.write_bytes((newline.join(lines) + newline).encode(encoding))
    return notes


def share_ocsr_interpreter(stage, app_exe):
    """Remove an identical OCSR CPython base while keeping its package search isolated."""
    app = (stage / 'App').resolve()
    runtime = app / 'ocsr_runtime'
    python_tree = runtime / 'python'
    base = python_tree / 'cpython-3.10-windows-x86_64-none'
    core = app / 'chemistry_python'
    base_site = base / 'Lib' / 'site-packages'
    support = runtime / 'python-support'
    required = ('python.exe', 'python310.dll', 'Lib/encodings/__init__.py')
    if (not base_site.is_dir() or not (runtime / 'venv/Lib/site-packages').is_dir()
            or not all((base / name).is_file() for name in required)
            or sorted(python_tree.iterdir()) != [base]):
        raise SystemExit('无法合并 OCSR Python：运行时布局与预期不符')

    count = 0
    removed_bytes = 0
    for source in base.rglob('*'):
        if not source.is_file() or source.is_relative_to(base_site):
            continue
        rel = source.relative_to(base)
        target = core / rel
        size = source.stat().st_size
        if not target.is_file() or size != target.stat().st_size or sha256_file(source) != sha256_file(target):
            raise SystemExit('无法合并 OCSR Python：与 Core 的文件不同：' + str(rel))
        count += 1
        removed_bytes += size

    build_dir = app_exe.resolve().parent
    scripts = ('ai_ocsr_bridge.py', 'ocsr_client.py')
    if not all((build_dir / name).is_file() for name in scripts):
        raise SystemExit('合并 OCSR Python 需要主程序同目录下的最新 OCSR 启动脚本')
    # Keep even the smaller base site-packages, including their original metadata.
    base_site.rename(support)
    shutil.rmtree(python_tree)
    (runtime / 'use-core-python').touch()
    for name in scripts:
        if (app / name).is_file():
            shutil.copy2(build_dir / name, app / name)

    old_python = rb'ocsr_runtime\python\cpython-3.10-windows-x86_64-none\python.exe"'
    new_python = rb'chemistry_python\python.exe" -S'
    old_site = rb'ocsr_runtime\venv\Lib\site-packages"'
    new_site = rb'ocsr_runtime\venv\Lib\site-packages;%ROOT%ocsr_runtime\python-support"'
    for launcher in app.glob('*.cmd'):
        raw = launcher.read_bytes()
        if old_python in raw:
            if old_site not in raw:
                raise SystemExit('无法更新 OCSR 命令行启动脚本：' + launcher.name)
            launcher.write_bytes(raw.replace(old_python, new_python).replace(old_site, new_site))
    update_shared_ocsr_guide(app)
    return {'duplicate_files': count, 'bytes_removed': removed_bytes}


def update_shared_ocsr_guide(app):
    guide = app / '调用说明.md'
    if guide.is_file():
        raw = guide.read_bytes()
        raw = raw.replace(rb'.\ocsr_runtime\python\cpython-3.10-windows-x86_64-none\python.exe ',
                          rb'.\chemistry_python\python.exe -S ')
        raw = raw.replace(rb'$PWD\ocsr_runtime\venv\Lib\site-packages"',
                          rb'$PWD\ocsr_runtime\venv\Lib\site-packages;$PWD\ocsr_runtime\python-support"')
        raw = raw.replace('│  ├─ python\\              # 内嵌 CPython 3.10（可移动）'.encode(),
                          '│  ├─ python-support\\     # OCSR 基础依赖，解释器位于 chemistry_python\\'.encode())
        guide.write_bytes(raw)


def build_assets(icon_path, assets_dir):
    if Image is None:
        raise SystemExit('需要 Pillow 才能生成图标：pip install pillow')
    if not icon_path.exists():
        raise SystemExit('找不到项目图标：' + str(icon_path))
    assets_dir.mkdir(parents=True, exist_ok=True)
    icon = Image.open(icon_path).convert('RGBA')
    made = []
    for name, width, height in ASSETS:
        icon.resize((width, height), Image.Resampling.LANCZOS).save(assets_dir / name)
        made.append(name)
    for stem, (width, height) in SCALES.items():
        for factor in SCALE_FACTORS:
            size = (round(width * factor / 100), round(height * factor / 100))
            name = stem + '.scale-' + str(factor) + '.png'
            icon.resize(size, Image.Resampling.LANCZOS).save(assets_dir / name)
            made.append(name)
    for size in TARGETSIZES:
        target = icon.resize((size, size), Image.Resampling.LANCZOS)
        for suffix in ('', '_altform-unplated', '_altform-lightunplated'):
            name = f'Square44x44Logo.targetsize-{size}{suffix}.png'
            target.save(assets_dir / name)
            made.append(name)
    return made


def build_icon_index(stage, makepri):
    # Index only package artwork. The application has large private runtime trees.
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp) / 'resources'
        root.mkdir()
        shutil.copytree(stage / 'Assets', root / 'Assets')
        shutil.copy2(stage / 'AppxManifest.xml', root / 'AppxManifest.xml')
        config = Path(tmp) / 'config.xml'
        subprocess.run([makepri, 'createconfig', '/cf', str(config), '/dq', 'en-US'], check=True)
        subprocess.run([makepri, 'new', '/pr', str(root), '/cf', str(config),
                        '/of', str(stage / 'resources.pri'), '/o'], check=True)


def normalize_version(text):
    """Map a release version onto the four numeric segments MSIX requires.

    The project ships versions like 0.9.7.0.0 (five segments); the Store package uses the
    first four with the fourth forced to 0, but only when the extra segment carries no
    meaning of its own, otherwise the mapping would silently invent a version.
    """
    parts = [p for p in text.strip().split('.') if p != '']
    if not parts or not all(p.isdigit() for p in parts):
        raise SystemExit('版本号必须是数字段，例如 0.9.7.0：' + text)
    if len(parts) == 5:
        if parts[4] != '0':
            raise SystemExit('五段版本号无法映射为商店四段版本，请手工指定四段版本：' + text)
        parts = parts[:4]
    elif len(parts) > 5:
        raise SystemExit('版本号最多五段：' + text)
    while len(parts) < 4:
        parts.append('0')
    if parts[3] != '0':
        raise SystemExit('第四段是商店保留位，必须为 0：' + text)
    if any(int(p) > 65535 for p in parts):
        raise SystemExit('版本号每段不得超过 65535：' + text)
    return '.'.join(parts)


def render_manifest(template, target, values):
    text = template.read_text(encoding='utf-8')
    for key, value in values.items():
        text = text.replace('{{' + key + '}}', value)
    leftover = [token for token in ('{{NAME}}', '{{PUBLISHER}}', '{{PUBLISHER_DISPLAY_NAME}}', '{{VERSION}}') if token in text]
    if leftover:
        raise SystemExit('清单模板仍有未替换的占位符：' + ', '.join(leftover))
    target.write_text(text, encoding='utf-8')


def find_windows_kit_tool(name):
    import glob
    for pattern in (
        str(Path('C:/Program Files (x86)/Windows Kits/10/bin/*/x64/') / name),
        str(Path('C:/Program Files/Windows Kits/10/bin/*/x64/') / name),
    ):
        found = sorted(glob.glob(pattern))
        if found:
            return found[-1]
    return None


def resolve_tool(explicit, name, extra_hint):
    if explicit:
        path = Path(explicit)
        if not path.exists():
            raise SystemExit('找不到 ' + name + '：' + str(path))
        return str(path)
    found = shutil.which(name)
    if found:
        return found
    found = find_windows_kit_tool(name + '.exe')
    if found:
        return found
    raise SystemExit('找不到 ' + name + '，请安装 Windows SDK / MSIX 打包工具，或用参数指定：' + extra_hint)


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8', errors='replace')
        except (AttributeError, ValueError):
            pass
    parser = argparse.ArgumentParser(description='构建 MoleculeStudio 商店 MSIX 包')
    parser.add_argument('--release', type=Path, required=True, help='包含四个组件 ZIP 的目录')
    parser.add_argument('--output', type=Path, required=True, help='输出的 .msix 路径')
    parser.add_argument('--name', required=True, help='Partner Center 的 Package/Identity/Name')
    parser.add_argument('--publisher', required=True, help='Package/Identity/Publisher，形如 CN=..., O=..., C=CN')
    parser.add_argument('--publisher-display-name', required=True, help='Package/Properties/PublisherDisplayName')
    parser.add_argument('--version', required=True, help='四段版本号，第四段为 0')
    parser.add_argument('--components', default=','.join(COMPONENTS), help='参与打包的组件，默认全部四个')
    parser.add_argument('--makeappx', help='makeappx.exe 路径，默认自动查找 Windows SDK')
    parser.add_argument('--icon', type=Path, default=DEFAULT_ICON, help='生成商店图标的源 PNG')
    parser.add_argument('--app-exe', type=Path, help='用已验证的新版主程序替换 Core 中的主程序')
    parser.add_argument('--share-ocsr-python', action='store_true', help='合并完整安装包中内容相同的 Core 和 OCSR Python 解释器，需要新版主程序与脚本')
    parser.add_argument('--stage-dir', type=Path, help='使用空目录并保留中间文件，便于检查内容')
    parser.add_argument('--allow-overlay', action='store_true', help='允许组件之间同名文件内容不同（默认报错）')
    parser.add_argument('--dry-run', action='store_true', help='只暂存、生成清单和图标，不调用 makeappx')
    parser.add_argument('--sign-pfx', type=Path, help='仅用于本地侧载测试：用 PFX 给 MSIX 签名')
    parser.add_argument('--sign-password', help='PFX 密码')
    parser.add_argument('--signtool', help='signtool.exe 路径')
    args = parser.parse_args(argv)

    release = args.release.resolve()
    if not release.is_dir():
        raise SystemExit('发布目录不存在：' + str(release))
    version = normalize_version(args.version)
    if not args.publisher.startswith('CN='):
        raise SystemExit('Publisher 必须以 CN= 开头，例如 "CN=4B2A1C3D-..., O=..., C=CN"：' + args.publisher)
    kinds = [k.strip() for k in args.components.split(',') if k.strip()]
    unknown = [k for k in kinds if k not in COMPONENTS]
    if unknown:
        raise SystemExit('未知组件：' + ', '.join(unknown))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp = None
    if args.share_ocsr_python and (not args.app_exe or 'OCSR' not in kinds or 'Core' not in kinds):
        raise SystemExit('--share-ocsr-python 需要 --app-exe、Core 和 OCSR')
    if args.app_exe and not args.app_exe.is_file():
        raise SystemExit('找不到新版主程序：' + str(args.app_exe))
    if args.stage_dir:
        stage = args.stage_dir.resolve()
        stage.mkdir(parents=True, exist_ok=True)
        if any(stage.iterdir()):
            raise SystemExit('暂存目录必须为空：' + str(stage))
    else:
        temp = tempfile.mkdtemp(prefix='molecule-store-')
        stage = Path(temp)

    summary = {'version': version, 'components': [], 'excluded_entries': 0, 'conflicts': [], 'assets': []}
    try:
        conflicts = []
        for kind in kinds:
            archive = find_archive(release, kind)
            written, skipped = stage_component(archive, stage, kind, conflicts, args.allow_overlay)
            summary['components'].append({'component': kind, 'archive': archive.name, 'files': written, 'skipped': skipped})
            summary['excluded_entries'] += skipped
        summary['conflicts'] = sorted(set(conflicts))
        if conflicts and not args.allow_overlay:
            raise SystemExit('组件之间存在内容不同的同名文件，请确认叠加顺序或用 --allow-overlay：' + ', '.join(summary['conflicts'][:10]))

        exe = stage / 'App' / 'MoleculeStudio.exe'
        if not exe.exists():
            raise SystemExit('暂存目录里没有 App\\MoleculeStudio.exe，请检查 Core 组件包')

        if args.app_exe:
            shutil.copy2(args.app_exe, exe)
        if args.share_ocsr_python:
            summary['shared_ocsr_python'] = share_ocsr_interpreter(stage, args.app_exe)
        summary['backend_ini'] = normalize_backend_ini(stage)
        if (stage / 'App' / 'MoleculeStudio-OnlineSetup.exe').exists():
            raise SystemExit('商店包不应包含更新下载器，检查排除规则')

        summary['assets'] = build_assets(args.icon, stage / 'Assets')
        render_manifest(STORE / 'AppxManifest.xml', stage / 'AppxManifest.xml', {
            'NAME': args.name,
            'PUBLISHER': args.publisher,
            'PUBLISHER_DISPLAY_NAME': args.publisher_display_name,
            'VERSION': version,
        })

        total_files = sum(1 for _ in stage.rglob('*') if _.is_file())
        total_bytes = sum(p.stat().st_size for p in stage.rglob('*') if p.is_file())
        summary['files'] = total_files
        summary['bytes'] = total_bytes
        summary['stage'] = str(stage)

        if args.dry_run:
            print(json.dumps(summary, ensure_ascii=False, indent=2))
            print('dry-run：已暂存到 ' + str(stage) + '，未生成 MSIX', file=sys.stderr)
            return 0

        makeappx = resolve_tool(args.makeappx, 'makeappx', '--makeappx D:/SDK/x64/makeappx.exe')
        makepri = Path(makeappx).with_name('makepri.exe')
        build_icon_index(stage, str(makepri))
        command = [makeappx, 'pack', '/d', str(stage), '/p', str(args.output), '/o', '/h', 'SHA256']
        subprocess.run(command, check=True)
        if not args.output.exists():
            raise SystemExit('makeappx 未生成 ' + str(args.output))
        summary['msix'] = str(args.output)
        summary['msix_bytes'] = args.output.stat().st_size
        summary['msix_sha256'] = sha256_file(args.output)
        if summary['msix_bytes'] > 25 * 1024 ** 3:
            print('警告：单个包不得超过 25 GB，当前 ' + str(summary['msix_bytes']) + ' 字节', file=sys.stderr)

        if args.sign_pfx:
            signtool = resolve_tool(args.signtool, 'signtool', '--signtool .../signtool.exe')
            sign = [signtool, 'sign', '/fd', 'SHA256', '/td', 'SHA256',
                    '/tr', 'http://timestamp.digicert.com', '/f', str(args.sign_pfx)]
            if args.sign_password:
                sign += ['/p', args.sign_password]
            sign.append(str(args.output))
            subprocess.run(sign, check=True)
            summary['signed'] = True

        print(json.dumps(summary, ensure_ascii=False, indent=2))
        return 0
    finally:
        if temp:
            shutil.rmtree(temp, ignore_errors=True)


if __name__ == '__main__':
    raise SystemExit(main())
