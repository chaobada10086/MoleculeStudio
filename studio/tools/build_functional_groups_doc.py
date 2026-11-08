# -*- coding: utf-8 -*-
"""Regenerate docs/FUNCTIONAL_GROUPS.md and studio/functional_groups_doc.hpp.

Input : studio/nickname_catalog.hpp  (compiled catalogue; names, CDXML fragments, formulas)
        studio/nicknames.src.json    (authored SMILES table; notes, confidence)
Output: docs/FUNCTIONAL_GROUPS.md
        studio/functional_groups_doc.hpp (compiled count, so chem_completion.hpp can
                                          static_assert that the document is in sync)

Neither input is edited here. Run after tools/build_nickname_catalog.py:
    python studio/tools/build_nickname_catalog.py
    python studio/tools/build_functional_groups_doc.py
"""
import json
from pathlib import Path

STUDIO = Path(__file__).resolve().parents[1]
ROOT = STUDIO.parent

# Keys whose spelling is ambiguous by policy; kept unset in chem_completion.hpp.
# Listed here so the document and the code cannot drift apart silently.
UNRESOLVED = ['iam', 'nam', 'sam', 'tam']
REVIEWED = [
    ('tbu', 'tBu', 't-Bu / tBu'),
    ('sbu', 'sBu', 's-Bu / sBu'),
    ('ibu', 'iBu', 'i-Bu / iBu'),
    ('nbu', 'nBu', 'n-Bu / nBu'),
    ('ipr', 'iPr', 'i-Pr / iPr'),
    ('npr', 'nPr', 'n-Pr / nPr'),
]
ELEMENT_GROUPS = [('Ac', 'acetyl'), ('Np', 'neopentyl'), ('Pr', 'propyl'),
                  ('Ts', 'tosyl'), ('Li', 'lithium substituent')]

def parse_entry(line):
    line = line.strip()
    if not line.startswith('{"') or '",R"NICK(' not in line:
        return None
    name, rest = line[2:].split('"', 1)
    start = rest.find('NICK(') + 5
    body = rest[start:]
    marker = ')NICK","'
    end = body.rfind(marker)
    if end < 0:
        return None
    fragment = body[:end]
    tail = body[end + len(marker):]
    formula = tail.split('"', 1)[0]
    return {'name': name, 'fragment': fragment, 'formula': formula,
            'atoms': fragment.count('<n '), 'bonds': fragment.count('<b ')}

def read_catalog():
    text = (STUDIO / 'nickname_catalog.hpp').read_text(encoding='utf-8')
    entries = []
    for line in text.splitlines():
        parsed = parse_entry(line)
        if parsed:
            entries.append(parsed)
    if not entries:
        raise SystemExit('nickname_catalog.hpp has no entries; run build_nickname_catalog.py first')
    return entries

def esc(value):
    return str(value).replace('|', '@BS@|')

def code(value):
    return '@BT@' + str(value) + '@BT@'

def main():
    entries = read_catalog()
    source = json.loads((STUDIO / 'nicknames.src.json').read_text(encoding='utf-8'))
    compiled = {e['name'] for e in entries}
    excluded = [name for name in source if name not in compiled]
    lines = []
    add = lines.append
    add('# 官能团与缩写目录')
    add('')
    add('> 本文件由 ' + code('studio/tools/build_functional_groups_doc.py') + ' 从 ' + code('studio/nicknames.src.json'))
    add('> 与 ' + code('studio/nickname_catalog.hpp') + ' 生成，**请勿手工编辑**。修改缩写后依次运行：')
    add('>')
    add('> ' + code('python studio/tools/build_nickname_catalog.py'))
    add('> ' + code('python studio/tools/build_functional_groups_doc.py'))
    add('')
    add('## 统计')
    add('')
    add('| 项目 | 数量 |')
    add('| --- | --- |')
    add('| 授权源数据条目（nicknames.src.json） | %d |' % len(source))
    add('| 编译进编辑器目录（nickname_catalog.hpp） | %d |' % len(entries))
    add('| 未编译条目（独立分子，无连接点） | %d |' % len(excluded))
    add('')
    add('未编译条目（只作为结构参考保留在源数据中，不参与补全）：')
    add('')
    add('、'.join(code(name) for name in excluded))
    add('')
    add('每条编译条目都是一段带恰好一个外部连接点的 CDXML 片段：在键末端输入其缩写后，')
    add('画布上的原子会被识别为对应官能团，可用“官能团展开”还原为完整结构。')
    add('')
    add('## 自动大小写规则')
    add('')
    add('输入在提交时经过 ' + code('chemcompletion::normalize') + '（原子标签）或')
    add(code('chemcompletion::chemicalToken') + '（整段文字），规则按顺序如下：')
    add('')
    add('1. **完全匹配的元素符号原样保留**：' + '、'.join(code(s) for s in ('Co', 'Cn', 'N', 'O', 'Fe')) +
        ' 等保持用户键入的大小写，不会被改成缩写或其它大小写。元素符号优先于同名缩写。')
    add('2. **已经等于目录标准写法的输入不重写**：' + '、'.join(code(s) for s in ('Boc', 'CN', 'i-Bu')) + ' 原样保留。')
    add('3. **键匹配忽略大小写和连字符**：键由字母数字组成，去掉 ' + code('-') + ' 并转小写。')
    add('   小写或大小写混写输入匹配到唯一标准写法后替换，例如 ' +
        '、'.join(code(a) + ' → ' + code(b) for a, b in (('cn', 'CN'), ('bpin', 'Bpin'), ('boc', 'Boc'), ('mgbr', 'MgBr'))) + '。')
    add('4. **歧义键不猜测**：同一键对应多个仅大小写/连字符不同的写法、且没有经过人工评审时，')
    add('   补全不会改写输入（保持用户原文），只在设置页的列表中提供候选。')
    add('   当前保留未决的键：' + '、'.join(code(k) for k in UNRESOLVED) + '。')
    add('5. **人工评审的连字符写法**：以下常用烷基前缀统一采用紧凑写法（旧目录中的连字符形式')
    add('   仍然可以被显式输入识别，只是不再作为小写输入的目标）：')
    add('')
    add('| 输入键 | 标准写法 | 目录中同名异写 |')
    add('| --- | --- | --- |')
    for key, canonical, variants in REVIEWED:
        add('| %s | %s | %s |' % (code(key), code(canonical), code(variants)))
    add('')
    add('6. **元素符号同时是常用缩写**（' + '、'.join(code(e) for e, _ in ELEMENT_GROUPS) + ' 等）：')
    add('   完全按元素符号大小写输入时拼写不变，但该标签仍会按缩写目录识别（例如 ' + code('Ts') + ' 显示为对甲苯磺酰基）。')
    add('   如需把元素本身作为普通元素标签使用，可在“设置 → 基团补齐”中停用对应条目。')
    add('')
    add('## 歧义处理')
    add('')
    add('- ' + code('pin') + ' 收录为 **O-连接的频哪醇醚**（pinacolato O-linked）；如需频哪醇硼酸酯请用 ' + code('Bpin') + '。')
    add('- ' + code('DIBAL') + ' 是试剂简写，片段按 Al 连接的二异丁基铝绘制（不含 Al–H 氢）。')
    add('- ' + code('Cp') + ' 按 η1 碳连接绘制；与金属的 η5 配位需另行绘制配位键。')
    add('- 只收录化学意义明确、文献广泛使用、且能用“一个连接点”表示的结构缩写；')
    add('  分子式、溶剂、当量条件（如 THF、NaH、80 °C）不属于本目录。')
    add('- 大小写冲突的元素符号（' + '、'.join(code(e) for e, _ in ELEMENT_GROUPS) + '）不做自动大小写改写；')
    add('  停用某个条目后，该缩写完全按用户输入保留。')
    add('')
    add('## 全部缩写')
    add('')
    add('| 缩写 | 含义 | SMILES（源数据） | 原子/键 | 分子式 | 置信度 |')
    add('| --- | --- | --- | --- | --- | --- |')
    missing = []
    for entry in sorted(entries, key=lambda item: item['name'].lower()):
        rec = source.get(entry['name'])
        if rec is None:
            missing.append(entry['name'])
            rec = {}
        add('| %s | %s | %s | %d/%d | %s | %s |' % (
            code(esc(entry['name'])), esc(rec.get('note', '')), code(esc(rec.get('smiles', ''))),
            entry['atoms'], entry['bonds'], esc(entry['formula']), esc(rec.get('confidence', ''))))
    if missing:
        raise SystemExit('catalogue entries missing authored source: %s' % ', '.join(missing))
    add('')
    add('<!-- generated by studio/tools/build_functional_groups_doc.py; '
        'catalog-count=%d; source-count=%d -->' % (len(entries), len(source)))
    add('')
    text = chr(10).join(lines).replace('@BT@', chr(96)).replace('@BS@', chr(92))
    (ROOT / 'docs').mkdir(exist_ok=True)
    (ROOT / 'docs/FUNCTIONAL_GROUPS.md').write_text(text, encoding='utf-8', newline=chr(10))
    header = ['#pragma once',
              '// Generated by tools/build_functional_groups_doc.py. Do not edit by hand.',
              '// The compiled catalogue count documented in docs/FUNCTIONAL_GROUPS.md.',
              'namespace functionalgroupsdoc {',
              'static const unsigned count=%d;' % len(entries),
              '}',
              '']
    (STUDIO / 'functional_groups_doc.hpp').write_text(chr(10).join(header), encoding='utf-8', newline=chr(10))
    print('functional groups: %d compiled, %d authored, %d excluded' % (len(entries), len(source), len(excluded)))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
