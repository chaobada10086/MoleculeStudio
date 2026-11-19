"""Call the existing isolated local OCSR worker for host-selected attachments.

The worker keeps MolScribe in memory for one request session.  Detection and
batch recognition both reuse that session so a 20-structure scheme loads the
model once, not 20 times.
"""
import copy
import json
import math
import os
import subprocess
import queue
import threading
import tempfile
import re
import xml.etree.ElementTree as ET
from contextlib import contextmanager
from pathlib import Path


def _resolve_source(attachments, args):
    from ai_local_sources import resolve_source
    number, source, page, count = resolve_source(attachments,args)
    return number,source,page


def _validate_crop(value, label='crop'):
    if (not isinstance(value, list) or len(value) != 4 or
            any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v)
                for v in value)):
        raise ValueError(label + ' 必须为 [左, 上, 宽, 高]，使用 0–1 的相对坐标')
    x, y, w, h = value
    if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > 1.000001 or y + h > 1.000001:
        raise ValueError('选区必须位于图片内部，坐标范围为 0–1')
    return [float(x), float(y), float(w), float(h)]


def prepare_runtime(runtime):
    """Return the bundled base interpreter and its site-packages directory.

    The old package shipped a uv venv whose pyvenv.cfg carried the packaging
    machine's absolute path, so its launcher broke as soon as the folder was
    copied to another computer.  Launch the bundled base Python and load the
    venv site-packages through PYTHONPATH instead: nothing absolute is needed.
    """
    candidates = [runtime / 'python' / 'cpython-3.10-windows-x86_64-none']
    candidates += sorted((runtime / 'python').glob('cpython-3.10*-windows-x86_64-none'))
    base = next((path.resolve() for path in candidates
                 if (path/'python.exe').is_file() and (path/'Lib/encodings/__init__.py').is_file()), None)
    if base is None:
        raise ValueError('本地 OCSR Python 标准库缺失（encodings），请修复运行时：' + str(runtime))
    site = runtime / 'venv' / 'Lib' / 'site-packages'
    if not site.is_dir():
        raise ValueError('本地图片转结构依赖目录缺失：' + str(site))
    return base / 'python.exe', site


class OcsrServiceError(ValueError):
    def __init__(self, message, code, stage):
        super().__init__(message)
        self.diagnostics = {'stage':stage, 'exit_code':code, 'atoms':None, 'rings':None, 'stereocenters':None,
                            'suggestion':'检查本地 OCSR 运行时和服务错误日志；启动失败时不要修改或猜测分子结构来绕过错误。'}


@contextmanager
def service_session(base):
    base = Path(base).resolve()
    runtime = Path(os.environ.get('MOLECULE_STUDIO_OCSR_RUNTIME') or base / 'ocsr_runtime')
    try: python, site = prepare_runtime(runtime)
    except (OSError, ValueError) as exc:
        raise OcsrServiceError('本地识别运行时初始化失败：' + str(exc), None, 'service_startup') from None
    script = base / 'ocsr_service.py'
    if not python.is_file() or not script.is_file():
        raise ValueError('本地图片转结构组件不可用，请检查 ocsr_runtime')
    cache, records = base / 'OCSRCache', base / 'recognition_records'
    command = [str(python), '-X', 'utf8', '-u', str(script), '--serve', '--models', str(runtime / 'models'),
               '--cache', str(cache), '--records', str(records)]
    flags = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
    env = os.environ.copy()
    for key in ('PYTHONHOME', 'PYTHONPATH', 'PYTHONSTARTUP'): env.pop(key, None)
    env['PYTHONPATH'] = str(site)
    env['PYTHONNOUSERSITE'] = '1'
    stderr = tempfile.TemporaryFile(mode='w+b')
    try:
        proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=stderr, text=True, encoding='utf-8', errors='replace',
                                env=env, cwd=base, creationflags=flags)
    except OSError as exc:
        stderr.close()
        raise OcsrServiceError("无法启动本地图片转结构服务：" + str(exc)[:200], getattr(exc, 'winerror', None), 'service_startup') from None
    responses = queue.Queue()
    ready = [False]

    def service_failure(label):
        try: proc.wait(timeout=.3)
        except subprocess.TimeoutExpired: pass
        stderr.seek(0, os.SEEK_END); size = stderr.tell()
        stderr.seek(max(0, size-24000))
        detail = stderr.read().decode('utf-8', 'replace').strip()
        code = proc.poll()
        try:
            cache.mkdir(parents=True, exist_ok=True)
            (cache/'last-ai-service-error.log').write_text('exit_code=%s\npython=%s\n%s\n' % (code, python, detail), encoding='utf-8')
        except OSError: pass
        return OcsrServiceError('%s（退出码 %s）：%s' % (label, code, detail[-1400:] or '服务没有返回诊断信息'),
                                code, 'recognition_service' if ready[0] else 'service_startup')

    def read_responses():
        try:
            for line in proc.stdout:
                responses.put(line)
        except (OSError, UnicodeError):
            pass
        finally:
            responses.put(None)

    reader = threading.Thread(target=read_responses, daemon=True)
    reader.start()

    def request(attrs, timeout=600):
        try:
            proc.stdin.write(ET.tostring(ET.Element('request', attrs), encoding='unicode') + '\n')
            proc.stdin.flush()
        except (OSError, BrokenPipeError):
            raise service_failure('本地识别服务通信中断') from None
        try:
            line = responses.get(timeout=timeout)
        except queue.Empty:
            raise ValueError('本地图片转结构超时，请缩小选区后重试') from None
        if not line:
            raise service_failure('本地识别服务意外退出')
        try: response = ET.fromstring(line)
        except ET.ParseError: raise service_failure('本地识别服务返回格式错误') from None
        if response.get('ok') != '1':
            raise ValueError(response.get('message') or response.get('error') or
                             response.text or '识别失败')
        return response

    try:
        request({'id':'ai-startup', 'op':'ping'}, timeout=60)
        ready[0] = True
        yield request
    except (OSError, ET.ParseError, BrokenPipeError) as exc:
        raise ValueError('本地识别服务通信失败：' + str(exc)[:200]) from None
    finally:
        if proc.poll() is None: proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        reader.join(timeout=5)
        for pipe in (proc.stdin, proc.stdout):
            if pipe:
                pipe.close()
        stderr.close()


def _load_cdxml(result, records):
    path = Path(result.get('cdxml') or '')
    try:
        path.resolve().relative_to(records.resolve())
    except ValueError:
        raise ValueError('识别结果路径无效') from None
    if not path.is_file() or path.stat().st_size > 4 * 1024 * 1024:
        raise ValueError('识别结果不存在或过大')
    root = ET.fromstring(path.read_text(encoding='utf-8'))
    atoms = list(root.iter('n'))
    if root.tag != 'CDXML' or not 1 <= len(atoms) <= 400:
        raise ValueError('识别结构为空或超过 400 原子')
    return root, atoms


def _molecule_node(root, x, y, style):
    attrs = {'x': str(x), 'y': str(y), **style}
    node = ET.Element('molecule', attrs)
    node.text = ET.tostring(root, encoding='unicode')
    return node


def _json_list(value):
    try:
        parsed = json.loads(value or '[]')
        return parsed if isinstance(parsed, list) else []
    except Exception:
        return []


def _parse_detect(response):
    regions = []
    for node in response.findall('region'):
        def numbers(name, cast=float):
            return [cast(v) for v in (node.get(name) or '').split(',') if v]
        regions.append({
            'index': int(node.get('index') or len(regions) + 1),
            'crop': numbers('crop'),
            'bbox_pt': numbers('bbox_pt'),
            'pixels': numbers('pixels', int),
            'score': float(node.get('score') or 0),
            'paths': int(node.get('paths') or 0),
            'closed_paths': int(node.get('closed_paths') or 0),
        })
    return {
        'method': response.get('method') or '',
        'page': int(response.get('page') or 1),
        'dpi': int(response.get('dpi') or 300),
        'page_size_px': [int(v) for v in (response.get('page_size_px') or '').split(',') if v],
        'count': int(response.get('count') or len(regions)),
        'regions': regions,
        'preview': response.get('preview') or '',
        'warnings': _json_list(response.get('warnings')),
    }


def _detect(request, source, page, min_score=0.35, max_regions=120):
    response = request({'op': 'detect', 'file': str(source['path']), 'page': str(page),
                        'dpi': '300', 'min_score': str(min_score),
                        'max_regions': str(max_regions)}, timeout=120)
    return _parse_detect(response)


def _recognize(request, source, page, crop, rendered):
    width, height = int(rendered.get('width')), int(rendered.get('height'))
    x, y, w, h = crop
    return request({'op': 'recognize', 'page_image': rendered.get('image'),
                    'crop_x': str(round(x * width)), 'crop_y': str(round(y * height)),
                    'crop_w': str(round(w * width)), 'crop_h': str(round(h * height)),
                    'source_kind': source['kind'], 'source_path': str(source['path']),
                    'page': str(page), 'dpi': '300', 'vector': '1', 'target_dpi': '600'})


def recognize_and_draw(engine, attachments, args, base, number, style):
    source_id, source, page = _resolve_source(attachments, args)
    crop = _validate_crop(args.get('crop', [0, 0, 1, 1]))
    cache, records = base / 'OCSRCache', base / 'recognition_records'
    with service_session(base) as request:
        engine.progress('正在本地渲染附件…')
        rendered = request({'op': 'render', 'file': str(source['path']),
                            'page': str(page), 'dpi': '300'})
        engine.progress('正在调用本地图片转结构模型，首次加载可能需要一些时间…')
        result = _recognize(request, source, page, crop, rendered)
    if result.get('insertable') != '1':
        warnings = [n.text for n in result.findall('warning') if n.text]
        raise ValueError('识别结果未通过本地插入质量门，未暂存任何结构；请调整裁剪范围并重新识别。' + '；'.join(warnings[:4]))
    root, atoms = _load_cdxml(result, records)
    attrs = {'x': str(number(args, 'x', engine.molecules * 120)),
             'y': str(number(args, 'y', 0)), **style(args)}
    node = _molecule_node(root, attrs['x'], attrs['y'], style(args))
    snapshot = (copy.deepcopy(engine.scene), engine.created_count,
                engine.molecules, list(engine.actions))
    try:
        created = engine.record_creation(node)
        engine.actions.append(node)
        engine.molecules += 1
        engine._ocsr_staged = True
    except Exception:
        engine.scene, engine.created_count, engine.molecules, engine.actions = snapshot
        raise
    warnings = [n.text for n in result.findall('warning') if n.text]
    warnings.append('识别结果需要核对原图，特别是实楔、虚楔、粗实线和粗虚线；模型置信度不等于正确率。')
    return {'staged': True, **created, 'attachment': source_id, 'path': str(source['path']), 'page': page,
            'crop': crop, 'atoms': len(atoms), 'bonds': len(list(root.iter('b'))),
            'confidence': result.get('confidence'), 'stereo_status': result.get('stereo_status'),
            'service_insertable': result.get('insertable') == '1', 'warnings': warnings}


def detect_regions(engine, attachments, args, base):
    source_id, source, page = _resolve_source(attachments, args)
    min_score = args.get('min_score', 0.35)
    max_regions = args.get('max_regions', 120)
    if type(min_score) not in (int, float) or not math.isfinite(min_score) or not 0 <= min_score <= 1:
        raise ValueError('min_score 必须是 0–1 的有限数值')
    if type(max_regions) is not int or not 1 <= max_regions <= 120:
        raise ValueError('max_regions 必须是 1–120 的整数')
    with service_session(base) as request:
        engine.progress('正在检测页面上的结构区域…')
        detected = _detect(request, source, page,
                           min_score=min_score, max_regions=max_regions)
    return {'attachment': source_id, 'path': str(source['path']), 'page': page, 'method': detected['method'],
            'dpi': detected['dpi'], 'page_size_px': detected['page_size_px'],
            'count': detected['count'], 'regions': detected['regions'],
            'preview': detected['preview'], 'warnings': detected['warnings']}


def recognize_regions(engine, attachments, args, base, number, style):
    source_id, source, page = _resolve_source(attachments, args)
    records = base / 'recognition_records'
    explicit = args.get('regions')
    stage = args.get('stage', True)
    max_regions = args.get('max_regions', 24)
    min_score = args.get('min_score', 0.5)
    min_height = args.get('min_height', 24)
    if type(stage) is not bool: raise ValueError('stage 必须是布尔值 true/false')
    if type(max_regions) is not int or not 1 <= max_regions <= 40: raise ValueError('max_regions 必须是 1–40 的整数')
    if type(min_score) not in (int, float) or not math.isfinite(min_score) or not 0 <= min_score <= 1: raise ValueError('min_score 必须是 0–1 的有限数值')
    if type(min_height) not in (int, float) or not math.isfinite(min_height) or min_height < 0: raise ValueError('min_height 必须是非负有限数值')
    warnings = []
    results = []
    if explicit is not None:
        if not isinstance(explicit, list) or not 1 <= len(explicit) <= max_regions:
            raise ValueError('regions 需要 1–' + str(max_regions) + ' 个 [左,上,宽,高] 选区')
        regions = [_validate_crop(item, 'region') for item in explicit]
        mode = 'explicit'
    else:
        regions = []
        mode = 'auto'

    with service_session(base) as request:
        engine.progress('正在本地渲染附件…')
        rendered = request({'op': 'render', 'file': str(source['path']),
                            'page': str(page), 'dpi': '300'})
        if mode == 'auto':
            engine.progress('正在自动检测页面结构区域…')
            detected = _detect(request, source, page, min_score=min_score,
                               max_regions=120)
            for region in detected['regions']:
                bbox = region.get('bbox_pt') or [0, 0, 0, 0]
                if region['score'] < min_score:
                    continue
                if (bbox[3] - bbox[1]) < min_height:  # skip ruled text / formula labels
                    continue
                regions.append(region['crop'])
                if len(regions) >= max_regions:
                    break
            warnings.extend(detected.get('warnings', []))
            if not regions:
                raise ValueError('没有检测到可识别的完整结构；请改用单结构 crop 或降低 min_score')
        engine.progress('正在批量识别 ' + str(len(regions)) + ' 个结构（模型只加载一次）…')
        for index, crop in enumerate(regions, 1):
            try:
                response = _recognize(request, source, page, crop, rendered)
                root, atoms = _load_cdxml(response, records)
                region_warnings = [n.text for n in response.findall('warning') if n.text]
                results.append({
                    'index': index,
                    'crop': [round(v, 6) for v in crop],
                    'confidence': response.get('confidence') or '',
                    'atoms': len(atoms),
                    'bonds': len(list(root.iter('b'))),
                    'stereo_status': response.get('stereo_status') or '',
                    'insertable': response.get('insertable') == '1',
                    'warnings': region_warnings,
                    'record': response.get('record') or '',
                    '_root': root,
                })
            except Exception as exc:
                results.append({'index': index, 'crop': [round(v, 6) for v in crop],
                                'error': str(exc)[:300], 'insertable': False})
    recognized = [item for item in results if 'error' not in item]
    insertable = [item for item in recognized if item.get('insertable')]
    staged = 0
    if stage and insertable:
        count = len(insertable)
        columns = max(1, min(5, int(math.ceil(math.sqrt(count)))))
        start_x = number(args, 'x', 0.0)
        start_y = number(args, 'y', 0.0)
        snapshot = (copy.deepcopy(engine.scene), engine.created_count,
                    engine.molecules, list(engine.actions))
        try:
            for position, item in enumerate(insertable):
                row, column = divmod(position, columns)
                node = _molecule_node(item['_root'], start_x + column * 200.0,
                                      start_y + row * 170.0, style(args))
                engine.record_creation(node)
                engine.actions.append(node)
                engine.molecules += 1
                staged += 1
            engine._ocsr_staged = True
        except Exception:
            engine.scene, engine.created_count, engine.molecules, engine.actions = snapshot
            raise
    for item in results:
        item.pop('_root', None)
    output = {
        'attachment': source_id, 'path': str(source['path']),
        'page': page,
        'mode': mode,
        'detected': len(regions),
        'recognized': len(recognized),
        'staged': staged,
        'regions': results,
        'warnings': warnings + [
            ('批量结果需要逐项核对；通过本地质量门的结构已暂存，尚未提交画布。' if stage else '本次仅返回识别候选，未暂存或插入任何结构；需与原图核对后再插入。'),
            '模型置信度不等于正确率，实楔、虚楔、粗实线和粗虚线必须对照原图。',
        ],
    }
    return output
