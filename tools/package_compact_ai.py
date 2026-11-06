"""Repack an existing three-part release with the compact AI layout, preserving payloads."""
import argparse
import concurrent.futures
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PREFIX = 'MoleculeStudio/'
OLD = 'AI 套件/dsh_core/'

def rename(name):
    if name.startswith(OLD + 'dsh/'):
        return 'ai/' + name[len(OLD + 'dsh/'):]
    if name == OLD + 'node/node.exe':
        return 'ai/node.exe'
    if name.startswith(OLD):
        return 'ai/' + name[len(OLD):]
    if name == 'AI 套件/README.txt':
        return 'ai/README-AI.txt'
    return name

def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def repack(source, target, kind):
    updates = {'ai_dsh.py': ROOT/'studio/ai_dsh.py'} if kind in ('Core', 'AI') else {}
    if kind == 'Core':
        updates['MoleculeStudio.exe'] = ROOT/'out/Studio/MoleculeStudio.exe'
    with zipfile.ZipFile(source) as src:
        mn = PREFIX + f'MANIFEST-{kind}.json'
        manifest = json.loads(src.read(mn))
        manifest['version'] = '0.95-20261106-ai-compact'
        manifest['files'] = {(rename(n) if kind == 'AI' else n): value for n, value in manifest['files'].items()}
        for n, path in updates.items():
            manifest['files'][n] = {'bytes': path.stat().st_size, 'sha256': digest(path)}
        with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED, compresslevel=1) as dst:
            for info in src.infolist():
                rel = info.filename.removeprefix(PREFIX)
                if info.filename == mn or rel in updates:
                    continue
                info.filename = PREFIX + (rename(rel) if kind == 'AI' else rel)
                with src.open(PREFIX+rel) as r, dst.open(info, 'w', force_zip64=True) as w:
                    shutil.copyfileobj(r, w, 1024*1024)
            for n, path in updates.items():
                dst.write(path, PREFIX+n)
            dst.writestr(mn, json.dumps(manifest, ensure_ascii=False, indent=2))
    with zipfile.ZipFile(target) as z:
        assert len(z.namelist()) == len(set(z.namelist())) == len(manifest['files'])+1
        for n, value in manifest['files'].items():
            with z.open(PREFIX+n) as f:
                assert hashlib.file_digest(f, 'sha256').hexdigest() == value['sha256'], n
        max_path = max(map(len, z.namelist()))
    row = dict(package=kind, file=target.name, bytes=target.stat().st_size, sha256=digest(target), max_archive_path=max_path)
    print(json.dumps(row), flush=True)
    return row

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(exist_ok=True)
    rows = json.loads((args.source/'RELEASE_CHECKSUMS.json').read_text())
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(repack,args.source/r['file'],args.output/(r['package']+'.zip'),r['package']) for r in rows]
        result = [f.result() for f in futures]
    (args.output/'RELEASE_CHECKSUMS.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    (args.output/'SHA256SUMS.txt').write_text(''.join(r['sha256']+'  '+r['file']+'\n' for r in result),encoding='ascii')
    shutil.copy2(ROOT/'dist/MoleculeStudio-OfflineInstall.exe',args.output/'Install.exe')
    (args.output/'安装说明.txt').write_text('双击 Install.exe 离线安装，三个 ZIP 与安装器、RELEASE_CHECKSUMS.json 保持在同一目录。推荐发布目录使用桌面 MS 等短名称。\n核心必装，AI 和图片识别可选。请选择新的空目录；此工具不会覆盖内容不同的现有文件。支持长路径，不需要修改系统注册表。\nAI 改为 ai/node_modules 短目录，所有第三方依赖、许可证与源码保留；主要优化路径和安装体验，不承诺大幅缩小体积。\nOCSR 遇到坐标映射偏差过大或键连接关系不一致时停止识别。\n',encoding='utf-8')
