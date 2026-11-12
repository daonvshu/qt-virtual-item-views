#!/usr/bin/env python3
"""Generate the offline HTML API reference and the agent routing index.

Run from any directory: python scripts/generate_api_docs.py [--check]
No third-party packages, network access, build configuration or timestamps.
This is a header excerpt extractor, not a C++ compiler or ABI analyzer.
"""
from __future__ import annotations

import argparse
import html
import json
import posixpath
import re
import sys
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urlsplit, urlunsplit

sys.dont_write_bytecode = True
from api_catalog import FEATURES, VIEWS
from api_translations import FEATURE_EN, VIEW_EN, UI_EN

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs' / 'api'
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27')
TYPE = re.compile(r'^[ \t]*(class|struct|enum(?:\s+class)?)\s+(?:VIRTUALITEMVIEWS_EXPORT\s+)?(\w+)([^;{}]*?)\{', re.M)
ACCESS = re.compile(r'(?:public|protected|private)(?:\s+slots)?\s*:|(?:signals|Q_SIGNALS)\s*:')


def mask(text):
    """Preserve source offsets/newlines while hiding comments and literals."""
    return TOKEN.sub(lambda m: re.sub(r'[^\n]', ' ', m.group()), text)


def matching_brace(text, start):
    depth = 0
    for pos in range(start, len(text)):
        if text[pos] == '{':
            depth += 1
        elif text[pos] == '}':
            depth -= 1
            if depth == 0:
                return pos
    raise ValueError('Unbalanced braces in public header')


def preceding_comment(source, offset):
    lines = source[:offset].rstrip().splitlines()
    result = []
    while lines and lines[-1].lstrip().startswith('///'):
        result.insert(0, lines.pop().strip()[3:].lstrip())
    return '\n'.join(result)


def signature(code):
    """Omit inline function bodies; retain nested types and default arguments."""
    clean = mask(code)
    if re.match(r'\s*(?:enum|struct|class)\b', clean):
        return code.strip()
    parentheses = 0
    for pos, char in enumerate(clean):
        if char == '(':
            parentheses += 1
        elif char == ')':
            parentheses -= 1
        elif char == '{' and parentheses == 0 and ')' in clean[:pos]:
            head = code[:pos].rstrip()
            # Constructors expose their signature, not member initialization.
            head = re.sub(r'\)\s*:\s*[\s\S]*', ')', head)
            return head + ';'
    return code.strip()


def entries(source, start, end, access):
    """Split an access segment into documented, balanced declarations."""
    result, comments, code = [], [], []
    braces = parentheses = 0
    line_number = 0
    pos = start
    for line in source[start:end].splitlines(keepends=True):
        stripped = line.strip()
        if not code:
            if stripped.startswith('///'):
                comments.append(stripped[3:].lstrip())
                pos += len(line)
                continue
            if not stripped or stripped.startswith(('//', '#')):
                pos += len(line)
                continue
            if re.match(r'Q_(?:OBJECT|ENUM|FLAG|PROPERTY|DECLARE|DISABLE)', stripped):
                comments = []
                pos += len(line)
                continue
            line_number = source.count('\n', 0, pos) + 1
        code.append(line.rstrip())
        clean = mask(line)
        braces += clean.count('{') - clean.count('}')
        parentheses += clean.count('(') - clean.count(')')
        if braces == 0 and parentheses == 0 and (clean.rstrip().endswith(';') or clean.rstrip().endswith('}')):
            raw = '\n'.join(code)
            sig = signature(raw)
            # Nested type bodies are intentionally retained in the owning API entry.
            names = re.findall(r'\b(~?[A-Za-z_]\w*)\s*\(', mask(sig))
            symbols = [n for n in names if n not in {'Q_UNUSED', 'decltype', 'sizeof', 'alignof'}]
            if not symbols:
                found = re.search(r'\b(?:enum class|enum|struct|class|using)\s+(\w+)', sig)
                if found:
                    symbols = [found.group(1)]
                else:
                    found = re.search(r'\b(\w+)\s*(?:=|;|\{)', sig)
                    if found:
                        symbols = [found.group(1)]
            result.append(dict(signature=sig, comment='\n'.join(comments),
                               line=line_number, access=access, symbols=list(dict.fromkeys(symbols))))
            comments, code = [], []
        pos += len(line)
    if code and any(x.strip() for x in code):
        raise ValueError('Unparsed public declaration: ' + '\n'.join(code))
    return result


def extract(path):
    source = path.read_text(encoding='utf-8-sig')
    clean = mask(source)
    result = []
    previous_end = -1
    occupied = []
    for match in TYPE.finditer(clean):
        if match.start() < previous_end:
            continue
        opening = match.end() - 1
        closing = matching_brace(clean, opening)
        previous_end = closing + 1
        occupied.append((match.start(), closing + 1))
        kind, name = match.group(1), match.group(2)
        declaration = source[match.start():opening].strip()
        item = dict(name=name, kind=kind, declaration=declaration,
                    comment=preceding_comment(source, match.start()),
                    line=source.count('\n', 0, match.start()) + 1, entries=[])
        if kind.startswith('enum'):
            item['entries'] = [dict(signature=source[match.start():closing + 1].strip() + ';',
                                    comment='', line=item['line'], access='public', symbols=[name])]
        else:
            access = 'public' if kind == 'struct' else 'private'
            begin = opening + 1
            depth = 0
            pos = begin
            while pos < closing:
                tag = ACCESS.match(clean, pos) if depth == 0 else None
                if tag:
                    if access in ('public', 'signals'):
                        item['entries'].extend(entries(source, begin, pos, access))
                    token = tag.group()
                    access = 'signals' if token.startswith(('signals', 'Q_SIGNALS')) else token.split()[0].rstrip(':')
                    pos = tag.end()
                    begin = pos
                    continue
                if clean[pos] == '{':
                    depth += 1
                elif clean[pos] == '}':
                    depth -= 1
                pos += 1
            if access in ('public', 'signals'):
                item['entries'].extend(entries(source, begin, closing, access))
        if item['entries']:
            result.append(item)
    # Namespace-level constants, aliases, exported factories and flag operators.
    namespace_entries = []
    free_pattern = re.compile(r'^(?:VIRTUALITEMVIEWS_EXPORT\s+[^;{]+;|(?:constexpr|using|typedef)\s+[^;{]+;|Q_DECLARE_(?:FLAGS|OPERATORS_FOR_FLAGS)\([^\n]+\))', re.M)
    for match in free_pattern.finditer(clean):
        if any(a <= match.start() < b for a, b in occupied):
            continue
        code = source[match.start():match.end()]
        names = re.findall(r'\b([A-Za-z_]\w*)\s*\(', mask(code))
        if not names:
            found = re.search(r'\b(?:using\s+)?(\w+)\s*(?:=|;)', code)
            names = [found.group(1)] if found else []
        namespace_entries.append(dict(signature=code.strip(), comment=preceding_comment(source, match.start()),
                                      line=source.count('\n', 0, match.start()) + 1, access='namespace', symbols=names))
    if namespace_entries:
        result.append(dict(name='viv namespace', kind='namespace', declaration='namespace viv', comment='',
                           line=0, entries=namespace_entries))
    return dict(header=path.relative_to(ROOT).as_posix(), types=result)


def examples():
    result = []
    for directory in sorted((ROOT / 'examples').glob('*_capabilities/[0-9]*')):
        readme, cpp = directory / 'README.md', directory / 'example.cpp'
        if not cpp.is_file():
            continue
        if readme.is_file():
            text = readme.read_text(encoding='utf-8-sig')
            title = next((line.lstrip('# ').strip() for line in text.splitlines() if line.startswith('# ')), directory.name)
        else:
            readme = directory.parent / 'README.md'
            text = readme.read_text(encoding='utf-8-sig')
            row = next((line for line in text.splitlines() if directory.name + '/example.cpp' in line), '')
            cells = [cell.strip() for cell in row.split('|')]
            title = directory.name + (' / ' + cells[2] if len(cells) > 3 else '')
        result.append(dict(group=directory.parent.name, title=title,
                           path=cpp.relative_to(ROOT).as_posix(), readme=readme.relative_to(ROOT).as_posix()))
    return result


def resolved_features():
    result = []
    for record in FEATURES:
        item = dict(record)
        paths = []
        for pattern in item['examples']:
            found = sorted(ROOT.glob(pattern))
            if not found:
                raise ValueError('Example pattern has no matches: ' + pattern)
            paths.extend(p.relative_to(ROOT).as_posix() for p in found)
        item['examples'] = list(dict.fromkeys(paths))
        for path in item['implementation'] + ([item['document']] if item['document'] else []):
            if not (ROOT / path).is_file():
                raise ValueError('Missing capability route: ' + path)
        result.append(item)
    return result


CSS = '''
:root{color-scheme:light;--ink:#1c2b3e;--muted:#526278;--line:#dce4ee;--accent:#165db0}
*{box-sizing:border-box}body{margin:0;background:#f4f7fb;color:var(--ink);font:16px/1.7 system-ui,"Microsoft YaHei",sans-serif}
header{background:#122b49;color:white;padding:30px max(24px,calc((100% - 1200px)/2))}header a{color:#b9dcff}
main{max-width:1248px;margin:auto;padding:24px}nav{display:flex;flex-wrap:wrap;gap:10px 20px;margin-bottom:16px}
a{color:var(--accent);text-underline-offset:3px}h1{margin:6px 0;font-size:30px;line-height:1.35}h2{font-size:23px;margin-top:32px}h3{font-size:18px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px}.card,article,details{background:white;border:1px solid var(--line);border-radius:10px;padding:18px;margin:14px 0;overflow-wrap:anywhere}.grid .card{margin:0}
.card p,article p{margin:9px 0}.muted{color:var(--muted);font-size:14px}.rules{border-left:4px solid #3485cf;padding-left:14px}
pre{background:#f0f4f8;padding:14px;border-radius:6px;overflow:auto;font-size:13px;line-height:1.6;white-space:pre-wrap;overflow-wrap:anywhere}code{font-family:Consolas,monospace;font-size:.92em}p code,li code{background:#edf3fa;padding:1px 4px}
input{width:100%;padding:12px;border:1px solid #7b93ac;border-radius:6px;font:inherit}label{font-weight:600}summary{cursor:pointer;font-weight:600}.doc{white-space:pre-wrap}.badge{display:inline-block;font-size:12px;background:#e7f0fa;color:#244e7c;border-radius:5px;padding:2px 7px;margin-right:6px}
table{width:100%;border-collapse:collapse;background:white}td,th{text-align:left;vertical-align:top;border:1px solid var(--line);padding:10px}th{background:#eaf0f8}.table-scroll{overflow:auto}[hidden]{display:none!important}:focus-visible{outline:3px solid #edaa23;outline-offset:3px}
.header-top{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap}
.language-switch{display:flex;gap:4px;padding:4px;background:#203d5d;border-radius:8px}
.language-switch button{cursor:pointer;border:1px solid transparent;border-radius:5px;padding:6px 12px;font:inherit;color:#e7f1ff;background:transparent}
.language-switch button[aria-pressed="true"]{background:white;color:#123c68;border-color:#b9dcff}
.function-index{padding:16px;border:1px solid var(--line);border-radius:10px;background:white}
.function-index ul{list-style:none;padding:0}.function-index li{padding:7px 10px;border-bottom:1px solid #e7edf5;overflow-wrap:anywhere}
.function-index li:nth-child(even){background:#f6f8fc}.function-index a{display:block}.function-index code{background:none}
[id]{scroll-margin-top:20px}article:target{outline:2px solid #3485cf;outline-offset:3px}.api-jump{font-weight:600}
@media(max-width:600px){main{padding:14px}header{padding:24px 16px}h1{font-size:24px}}
@media print{body{background:white}header{background:white;color:black}input,label,.search-help,.language-switch{display:none}article,details,.card{break-inside:avoid}a{color:inherit}main{max-width:none}}
'''
JS = r'''
const input = document.querySelector('#filter');
const status = document.querySelector('#search-status');
const switches = document.querySelector('#language-switch');
const localized = Array.from(document.querySelectorAll('[data-zh][data-en]'));
const searchable = Array.from(document.querySelectorAll('[data-search]'));
let language = 'zh';
const storageKey = 'viv.api.language';
const placeholder = {
  zh: '例如：Popup、setAutoHeight、冻结、branch',
  en: 'For example: Popup, setAutoHeight, frozen, branch'
};

function applyFilter() {
  if (!input) return;
  const terms = input.value.toLocaleLowerCase().trim().split(/\s+/).filter(Boolean);
  let visible = 0;
  searchable.forEach(node => {
    // Search either language, even after changing the display language.
    const translations = Array.from(node.querySelectorAll('[data-zh][data-en]'))
      .map(part => part.dataset.zh + ' ' + part.dataset.en).join(' ');
    const text = (node.dataset.search + ' ' + node.textContent + ' ' + translations).toLocaleLowerCase();
    node.hidden = !terms.every(term => text.includes(term));
    if (!node.hidden) visible++;
  });
  if (status) status.textContent = terms.length
    ? (language === 'en' ? `${visible} matches` : `匹配 ${visible} 项`) : '';
}

function updateLinks() {
  document.querySelectorAll('a[href]').forEach(link => {
    const raw = link.getAttribute('href');
    // Keep directory prefixes when carrying language across reference pages.
    if (!/^(?:\.\.\/index|(?:pages\/)?[a-z][a-z0-9_-]*)\.html(?:[?#]|$)/i.test(raw)) return;
    const url = new URL(raw, window.location.href);
    url.searchParams.set('lang', language);
    link.setAttribute('href', raw.split(/[?#]/, 1)[0] + url.search + url.hash);
  });
}

function setLanguage(next, remember = true) {
  language = next === 'en' ? 'en' : 'zh';
  document.documentElement.lang = language === 'en' ? 'en' : 'zh-CN';
  localized.forEach(node => { node.textContent = node.dataset[language]; });
  if (input) input.placeholder = placeholder[language];
  document.title = document.querySelector('h1').textContent + ' · VirtualItemViews';
  if (switches) switches.querySelectorAll('[data-language]').forEach(button => {
    button.setAttribute('aria-pressed', String(button.dataset.language === language));
  });
  if (remember) {
    try { localStorage.setItem(storageKey, language); } catch (_) { /* file/private mode */ }
  }
  const url = new URL(window.location.href);
  url.searchParams.set('lang', language);
  try { window.history.replaceState(null, '', url.href); } catch (_) { /* local-file restrictions */ }
  updateLinks();
  applyFilter();
}

function revealTarget(hash) {
  let id;
  try { id = decodeURIComponent(hash.slice(1)); } catch (_) { return; }
  const target = document.getElementById(id);
  if (!target) return;
  if (input) input.value = '';
  applyFilter();
  for (let parent = target.parentElement; parent; parent = parent.parentElement) {
    if (parent.tagName === 'DETAILS') parent.open = true;
  }
  target.scrollIntoView({block: 'start'});
  target.focus({preventScroll: true});
}

if (input) input.addEventListener('input', applyFilter);
if (switches) {
  switches.hidden = false;
  switches.querySelectorAll('[data-language]').forEach(button => {
    button.addEventListener('click', () => setLanguage(button.dataset.language));
  });
}
document.querySelectorAll('a[href^="#"]').forEach(link => {
  link.addEventListener('click', () => revealTarget(link.getAttribute('href')));
});
window.addEventListener('hashchange', () => revealTarget(window.location.hash));
let preferred = new URL(window.location.href).searchParams.get('lang');
if (preferred !== 'zh' && preferred !== 'en') {
  try { preferred = localStorage.getItem(storageKey); } catch (_) { /* file/private mode */ }
}
setLanguage(preferred, false);
if (window.location.hash) requestAnimationFrame(() => revealTarget(window.location.hash));
'''



def esc(value):
    return html.escape(str(value), quote=True)


def link(path, label=None):
    return '<a href="../../' + esc(path) + '">' + esc(label or path) + '</a>'


def shell(title, body, subtitle=''):
    return ('<!doctype html>\n<html lang="zh-CN"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>' + esc(title) + ' · VirtualItemViews</title><link rel="stylesheet" href="reference.css">'
            '</head><body><header><div class="header-top"><a href="index.html">VirtualItemViews / API</a>'
            '<div id="language-switch" class="language-switch" role="group" aria-label="Language / 语言" hidden>'
            '<button type="button" data-language="zh" aria-pressed="true" lang="zh-CN">中文</button>'
            '<button type="button" data-language="en" aria-pressed="false" lang="en">English</button></div></div><h1>' + esc(title) + '</h1><p>' + esc(subtitle) + '</p></header>'
            '<main><nav><a href="index.html">能力总览</a><a href="../../docs/AGENT_GUIDE.md">Agent 索引</a>'
            '<a href="../../examples/README.md">独立示例</a><a href="../../README.md">项目首页</a></nav>'
            '<label for="filter">搜索本页：能力、API 名称或关键词</label><input id="filter" type="search" placeholder="例如：Popup、setAutoHeight、冻结、branch">'
            '<p class="muted search-help">无需网络或服务器。所有内容默认可见；搜索只筛选当前页。完整继承 API 请沿基类链接查阅。</p>'
            '<p id="search-status" role="status" aria-live="polite"></p>' + body +
            '<footer class="muted"><p>按能力查找配套设置与独立示例；通用接口请沿基类链接查阅，Qt 继承接口请参考 Qt 文档。</p></footer>'
            '</main><script src="reference.js"></script></body></html>\n').replace('><', '>\n<')


def feature_html(item):
    text = '<article id="' + item['id'] + '" data-search="' + esc(' '.join(item['api'] + item['headers'])) + '">'
    text += '<h3>' + esc(item['title']) + '</h3><p><code>' + esc(' · '.join(item['api'])) + '</code></p>'
    text += '<p class="rules">' + esc(item['rules']) + '</p>'
    text += '<p class="muted">实现：' + ' · '.join(link(p) for p in item['implementation']) + '</p>'
    if item['document']:
        text += '<p>配套说明：' + link(item['document']) + '</p>'
    text += '<details><summary>独立源码示例（' + str(len(item['examples'])) + '）</summary><ul>'
    text += ''.join('<li>' + link(p) + '</li>' for p in item['examples']) + '</ul></details></article>'
    return text


def api_anchor(typ, entry, number):
    """Use the same stable target in the function index and detailed declaration."""
    return typ['name'].replace(' ', '-') + '-' + (entry['symbols'][0] if entry['symbols'] else 'entry') + '-' + str(number)


def is_function(entry):
    """Exclude type declarations, data fields and flag macros from the jump list."""
    declaration = mask(entry['signature']).strip()
    if re.match(r'(?:enum|struct|class|using|typedef|Q_DECLARE)\b', declaration):
        return False
    if declaration.startswith('Q_DECLARE_') or '(' not in declaration:
        return False
    if re.match(r'std::function<.*>\s+\w+\s*(?:=|;)', declaration, re.S):
        return False
    before = declaration.split('(', 1)[0]
    return '=' not in before or 'operator' in before


def function_index(header):
    """List callable public declarations and signals separately, preserving overloads."""
    parts = ['<section class="function-index" id="function-index" tabindex="-1"><h3>公开函数列表</h3>',
             '<p class="muted">按声明类型分组；每个重载单独列出，点击签名跳转到说明。信号单独列出；类型和数据成员仍保留在下方。</p>']
    count = 0
    for access in ('public', 'signals'):
        if access == 'signals':
            if not any(e['access'] == 'signals' and is_function(e) for t in header['types'] for e in t['entries']):
                continue
            parts.append('<h3>信号列表</h3>')
        for typ in header['types']:
            selected = [(i, e) for i, e in enumerate(typ['entries'])
                        if is_function(e) and (e['access'] == 'signals') == (access == 'signals')]
            if not selected:
                continue
            parts.append('<h4>' + esc(typ['name']) + '</h4><ul>')
            for number, entry in selected:
                count += 1
                label = re.sub(r'\s+', ' ', entry['signature']).strip()
                parts.append('<li data-search="' + esc(typ['name'] + ' ' + ' '.join(entry['symbols']))
                             + '"><a href="#' + esc(api_anchor(typ, entry, number)) + '"><code>'
                             + esc(label) + '</code></a></li>')
            parts.append('</ul>')
    if not count:
        parts.append('<p>此头文件没有公开函数声明；类型和数据成员见下方。</p>')
    parts.append('</section>')
    return ''.join(parts)


def function_key(header, type_name, signature):
    """Identify overloads without depending on source lines or source comments."""
    return (header, type_name, re.sub(r'\s+', ' ', signature).strip())


def load_function_docs(headers, features, path=None):
    """Require independently authored bilingual documentation for every callable."""
    path = path or OUT / 'data' / 'function-docs.json'
    source = json.loads(path.read_text(encoding='utf-8'))
    if source.get('schema') != 1:
        raise ValueError('Unsupported function documentation schema')
    records = {}
    feature_ids = {feature['id'] for feature in features}
    for record in source['functions']:
        key = function_key(record['header'], record['type'], record['signature'])
        if key in records:
            raise ValueError('Duplicate function documentation: ' + str(key))
        for pair in [record] + record.get('notes', []):
            if any(not isinstance(pair.get(lang), str) or not pair[lang].strip()
                   for lang in ('zh', 'en')):
                raise ValueError('Missing bilingual function explanation: ' + str(key))
        if set(record.get('related', [])) - feature_ids:
            raise ValueError('Unknown related capability: ' + str(key))
        records[key] = {field: record[field] for field in ('zh', 'en', 'notes', 'related') if field in record}
    expected = set()
    for header in headers:
        for typ in header['types']:
            for entry in typ['entries']:
                if not is_function(entry):
                    continue
                key = function_key(header['header'], typ['name'], entry['signature'])
                expected.add(key)
                if key in records:
                    entry['documentation'] = records[key]
    missing, obsolete = expected - records.keys(), records.keys() - expected
    if missing or obsolete:
        details = ['Missing: ' + repr(key) for key in sorted(missing)]
        details += ['Obsolete: ' + repr(key) for key in sorted(obsolete)]
        raise ValueError('Update docs/api/data/function-docs.json for changed public APIs:\n' + '\n'.join(details))


def function_description(entry, stem, features):
    """Render maintained locale pairs, never falling back to header comments."""
    doc = entry['documentation']
    body = ''
    for pair in [doc] + doc.get('notes', []):
        body += ('<p class="doc" data-zh="' + esc(pair['zh']) + '" data-en="'
                 + esc(pair['en']) + '">' + esc(pair['zh']) + '</p>')
    related = {feature['id']: feature for feature in features}
    if doc.get('related'):
        body += '<p class="muted">配套能力：'
        for feature_id in doc['related']:
            feature = related[feature_id]
            target = ('#' if stem in feature['headers'] else 'index.html#') + feature_id
            body += '<a href="' + esc(target) + '">' + esc(feature['title']) + '</a> '
        body += '</p>'
    return body


def page(header, features, all_examples):
    stem = Path(header['header']).stem
    info = VIEWS.get(stem)
    title = info[0] if info else ' / '.join(t['name'] for t in header['types']) or stem
    body = '<p>' + link(header['header'], '公开头文件：' + header['header']) + '</p>'
    if info:
        body += '<p>' + esc(info[4]) + '</p><p>直接基类：'
        parent = next((k for k, v in VIEWS.items() if v[0] == info[2]), None)
        body += ('<a href="' + parent + '.html">' + esc(info[2]) + '</a>' if parent else esc(info[2])) + '。</p>'
        if stem not in ('virtualitemview', 'wraplayout', 'wrapwidget', 'virtualheaderview'):
            body += '<p class="rules">本页列出本类型声明的接口。模型、选择、编辑、拖放、背景、自动高度和诊断还需查 '
            body += '<a href="virtualitemview.html">VirtualItemView</a>。'
            if stem == 'virtualtreetableview':
                body += '列、表头、pane、合并与排序查 <a href="virtualtableview.html">VirtualTableView</a>。'
            body += '“可继承”不代表所有布局都支持：请先看下方能力限制。</p>'
    body += '<p class="api-jump"><a href="#public-api">公开函数列表</a></p>'
    relevant = [f for f in features if stem in f['headers']]
    body += '<h2>能力与配套设置</h2>' + ''.join(feature_html(f) for f in relevant)
    if info and info[3]:
        chapter_list = [e for e in all_examples if e['group'] == info[3] + '_capabilities']
        body += '<details><summary>此视图全部能力章节（' + str(len(chapter_list)) + '）</summary><ul>'
        body += ''.join('<li>' + link(e['path'], e['title']) + ' · ' + link(e['readme'], '说明') + '</li>' for e in chapter_list) + '</ul></details>'
    body += '<h2 id="public-api" tabindex="-1">公开 API、类型与信号</h2>' + function_index(header) + '<h2>函数说明</h2><p class="muted">按函数查看用途与配套设置；重载分别列出，可通过相关能力链接查找示例。</p>'
    for typ in header['types']:
        body += '<section><h3 id="' + esc(typ['name'].replace(' ', '-')) + '">' + esc(typ['name']) + '</h3>'
        body += '<pre>' + esc(typ['declaration']) + '</pre>'
        for number, entry in enumerate(typ['entries']):
            anchor = api_anchor(typ, entry, number)
            body += '<article tabindex="-1" id="' + esc(anchor) + '" data-search="' + esc(typ['name'] + ' ' + ' '.join(entry['symbols'])) + '">'
            body += '<span class="badge">' + esc(entry['access']) + '</span><span class="muted">' + link(header['header'], '头文件') + ' · L' + str(entry['line']) + '</span>'
            body += '<pre>' + esc(entry['signature']) + '</pre>'
            if is_function(entry):
                body += function_description(entry, stem, features)
            body += '<a href="#function-index" class="muted">返回函数列表</a></article>'
        body += '</section>'
    return shell(title, body, info[1] if info else '公开辅助接口与扩展契约')


def index_page(headers, features):
    body = '<h2>先选布局，再组合框架能力</h2><p>业务开始自绘、维护事件过滤器或缓存池之前，先按下方需求查找已有入口。'
    body += '每个视图页同时给出能力约束、配套设置、完整公开声明和可复制的独立章节。</p><div class="grid">'
    for stem, info in VIEWS.items():
        body += '<section class="card" data-search="' + esc(' '.join(info)) + '"><h3><a href="' + stem + '.html">' + esc(info[0]) + '</a></h3><strong>' + esc(info[1]) + '</strong><p>' + esc(info[4]) + '</p></section>'
    body += '</div><h2>常见组合与能力边界</h2><div class="table-scroll"><table><thead><tr><th>需求</th><th>组合 / 边界</th></tr></thead><tbody>'
    for left, right in [
        ('默认交互', 'ExtendedSelection + SelectRows；内置 hover/selected 开启；180ms OutCubic。改物化模式不会改选择范围。'),
        ('输入或 Popup', '优先 ItemEditorAdapter + edit()。不要默认增加 stack layout、全局焦点监听、每行常驻 editor。'),
        ('整表高度', 'autoHeight 独立于 item height。用 setAutoHeightMaximumHeight 限高，0 表示完整内容，父布局不足由业务处理。'),
        ('冻结', 'List/Table 可冻结行；Table/TreeTable 可冻结列；Tree/TreeTable 无冻结行。Flow/Masonry/Wrap 不提供行 pane。'),
        ('尺寸', 'List/Tree/Table 用内核或表格行高；Flow/Masonry/Wrap 用专用尺寸 API，不能照抄 Variable+autoMeasure。'),
        ('样式', '外框、项底色、hover/select、间距、表头容器、section、corner 各有独立职责；透明业务控件才能露出框架底层状态。'),
        ('少量标签 / 大量项目', '少量真实控件用 WrapWidget/WrapLayout；模型复用用 VirtualWrapView；等宽网格用 Flow；不同高度最短列用 Masonry。'),
    ]:
        body += '<tr><td>' + esc(left) + '</td><td>' + esc(right) + '</td></tr>'
    body += '</tbody></table></div><h2>按功能查 API 与实现</h2>' + ''.join(feature_html(f) for f in features)
    body += '<h2>全部公开头文件</h2><div class="grid">'
    for header in headers:
        stem = Path(header['header']).stem
        names = ' / '.join(t['name'] for t in header['types']) or stem
        symbols = ' '.join(s for t in header['types'] for e in t['entries'] for s in e['symbols'])
        body += '<div class="card" data-search="' + esc(names + ' ' + symbols) + '"><a href="' + stem + '.html">' + esc(stem + '.h') + '</a><p class="muted">' + esc(names) + '</p></div>'
    body += '</div>'
    return shell('公开 API 与能力导航', body, '离线可搜索 · 视图选型 · 配套设置 · 公开声明 · 独立示例')


def agent_guide(headers, features, chapters):
    lines = ['# VIV agent capability routing', '',
             'Use this index to integrate VIV capabilities into an application before writing equivalent custom behavior.',
             'All paths are relative to the VIV dependency source root. Start with a requirement keyword, then follow its API and example links.', '',
             '## Retrieval protocol',
             '1. Classify the layout; consult the view matrix below. Do not assume VIV implements QAbstractItemView APIs.',
             '2. Search this file by requirement or exact symbol. Read the listed header, then the smallest independent example.',
             '3. Read base APIs too: every virtual content view derives from VirtualItemView; TreeTable also inherits Table.',
             '4. Before custom hover/selection paint, timer animation, editor stacks, popup cancellation filters, wheel forwarding, pooling, frozen clones, manual height or column geometry: check the routes below.',
             '5. Reuse a public setter/adapter first. Use custom drawing/layout only for behavior not covered by the public configuration or adapter callbacks.',
             '6. Apply the documented layout and lifecycle constraints. Implementation links help clarify behavior; do not call private *_p.h helpers from application code.',
             '7. Query `docs/api/data/public-api.json` by header/type: entries provide signature, symbols, access, line and documentation (zh/en, notes, related capabilities). Read only the types relevant to the requirement.',
             '8. Offline reference: `docs/api/index.html`, with Chinese/English switching, keyword search and per-overload function links.', '',
             '## View matrix']
    for stem, info in VIEWS.items():
        lines.append(f'- {info[0]} | base={info[2]} | header=include/virtualitemviews/{stem}.h | {info[4]}')
    lines += ['', '## Capability routes']
    for item in features:
        lines += ['', '### ' + item['id'] + ' | ' + item['title'],
                  'api: ' + ' '.join(item['api']),
                  'headers: ' + ' '.join('include/virtualitemviews/' + h + '.h' for h in item['headers']),
                  'rules: ' + item['rules'],
                  'impl: ' + ' '.join(item['implementation'])]
        if item['document']:
            lines.append('doc: ' + item['document'])
        lines.append(('examples: ' + ' '.join(item['examples'])).rstrip())
    lines += ['', '## Public symbol inventory by declaring type',
              'These are declarations, not a guarantee that all inherited configurations work on all layouts. Apply the capability limits above.',
              'Nested types and overloaded signatures are retained in public-api.json. Public data members are indexed alongside methods.']
    for header in headers:
        lines += ['', '### ' + header['header']]
        for typ in header['types']:
            symbols = list(dict.fromkeys(s for e in typ['entries'] for s in e['symbols']))
            lines.append(typ['name'] + ': ' + ' '.join(symbols))
    lines += ['', '## Complete independent example inventory',
              'Each chapter owns its model/adapter/widgets/UI; copy the smallest chapter, not another view implementation. README explains setup.']
    group = None
    for entry in chapters:
        if group != entry['group']:
            group = entry['group']
            lines += ['', '### examples/' + group]
        lines.append('- ' + entry['path'] + ' | ' + entry['title'])
    lines.append('')
    return '\n'.join(lines)


def translation_map(features, chapters):
    """Require complete authored translations for capability and view descriptions."""
    if set(FEATURE_EN) != {f['id'] for f in features} or set(VIEW_EN) != set(VIEWS):
        raise ValueError('English catalog keys must match the capability/view catalog')
    translations = dict(UI_EN)
    for item in features:
        title, rules = FEATURE_EN[item['id']]
        translations[item['title']] = title
        translations[item['rules']] = rules
    for stem, info in VIEWS.items():
        translations[info[1]], translations[info[4]] = VIEW_EN[stem]
    for chapter in chapters:
        directory = Path(chapter['path']).parent.name
        number, slug = directory.split('_', 1)
        label = slug.replace('_', ' ').capitalize()
        for word in ('ui', 'api', 'popup', 'rtl'):
            label = re.sub(r'\b' + word + r'\b', word.upper() if word != 'popup' else 'Popup', label, flags=re.I)
        translations[chapter['title']] = (number + ' / ' if chapter['title'][:1].isdigit() else '') + label
    return translations


class BilingualHTML(HTMLParser):
    """Attach text-only locale pairs without changing code, IDs or link destinations."""
    def __init__(self, translations):
        super().__init__(convert_charrefs=True)
        self.translations = sorted(translations.items(), key=lambda pair: len(pair[0]), reverse=True)
        self.parts = []
        self.stack = []

    def handle_decl(self, declaration):
        self.parts.append('<!' + declaration + '>')

    def handle_starttag(self, tag, attrs):
        self.parts.append(self.get_starttag_text())
        if tag not in {'area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source', 'track', 'wbr'}:
            self.stack.append((tag, 'doc' in dict(attrs).get('class', '').split()))

    def handle_startendtag(self, tag, attrs):
        self.parts.append(self.get_starttag_text())

    def handle_endtag(self, tag):
        self.parts.append('</' + tag + '>')
        if self.stack and self.stack[-1][0] == tag:
            self.stack.pop()

    def handle_comment(self, data):
        self.parts.append('<!--' + data + '-->')

    def handle_data(self, data):
        if not re.search('[\u4e00-\u9fff]', data) or any(t in {'pre', 'code', 'title', 'button'} or original for t, original in self.stack):
            self.parts.append(esc(data))
            return
        translated = re.sub(r'公开头文件：', 'Public header: ', data)
        translated = re.sub(r'独立源码示例（(\d+)）', r'Independent source examples (\1)', translated)
        translated = re.sub(r'此视图全部能力章节（(\d+)）', r'All capability chapters for this view (\1)', translated)
        for original, english in self.translations:
            translated = translated.replace(original, english)
        if re.search('[\u4e00-\u9fff]', translated):
            raise ValueError('Missing English UI translation: ' + data.strip())
        self.parts.append('<span data-zh="' + esc(data) + '" data-en="' + esc(translated) + '">' + esc(data) + '</span>')


def localize_html(source, translations):
    """Keep Chinese as the no-script fallback and embed offline English content."""
    parser = BilingualHTML(translations)
    parser.feed(source)
    parser.close()
    return ''.join(parser.parts)


def output_location(path):
    """Keep the entry page at the root and group reference output by purpose."""
    if path.parent != OUT or path.name == 'index.html':
        return path
    group = {'.html': 'pages', '.css': 'assets', '.js': 'assets', '.json': 'data'}
    return OUT / group[path.suffix] / path.name


def relocate_links(source, destination, locations):
    """Resolve rendered flat-reference URLs relative to their final page directory."""
    def replace(match):
        url = urlsplit(html.unescape(match.group(2)))
        if url.scheme or url.netloc or not url.path or url.path.startswith('/'):
            return match.group(0)
        target = posixpath.normpath(posixpath.join('docs/api', url.path))
        target = locations.get(target, target)
        relative = posixpath.relpath(target, destination.parent.relative_to(ROOT).as_posix())
        value = urlunsplit(('', '', relative, url.query, url.fragment))
        return match.group(1) + '="' + esc(value) + '"'
    return re.sub(r'\b(href|src)="([^"]*)"', replace, source)


def generate():
    headers = [extract(p) for p in sorted((ROOT / 'include' / 'virtualitemviews').glob('*.h'))]
    features, chapters = resolved_features(), examples()
    load_function_docs(headers, features)
    known = {Path(h['header']).stem for h in headers}
    for item in features:
        if set(item['headers']) - known:
            raise ValueError('Unknown header in ' + item['id'])
    source = '\n'.join((ROOT / h['header']).read_text(encoding='utf-8-sig') for h in headers)
    for item in features:
        for symbol in item['api']:
            if symbol != 'setAcceptDrops' and not re.search(r'\b' + re.escape(symbol) + r'\b', source):
                raise ValueError('Unknown capability symbol: ' + item['id'] + '/' + symbol)
    outputs = {OUT / 'reference.css': CSS.strip() + '\n', OUT / 'reference.js': JS.strip() + '\n',
               OUT / 'index.html': index_page(headers, features),
               ROOT / 'docs' / 'AGENT_GUIDE.md': agent_guide(headers, features, chapters),
               OUT / 'public-api.json': json.dumps(dict(schema_version=2, headers=headers), ensure_ascii=False, indent=2) + '\n'}
    for header in headers:
        outputs[OUT / (Path(header['header']).stem + '.html')] = page(header, features, chapters)
    translations = translation_map(features, chapters)
    locations = {path.relative_to(ROOT).as_posix(): output_location(path).relative_to(ROOT).as_posix()
                 for path in outputs}
    grouped = {}
    for path, content in outputs.items():
        destination = output_location(path)
        if path.suffix == '.html':
            content = localize_html(relocate_links(content, destination, locations), translations)
        grouped[destination] = content
    return grouped, headers, features, chapters


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Check generated files without writing')
    args = parser.parse_args()
    outputs, headers, features, chapters = generate()
    stale = []
    for path, text in outputs.items():
        if args.check:
            if not path.exists() or path.read_text(encoding='utf-8') != text:
                stale.append(path.relative_to(ROOT).as_posix())
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open('w', encoding='utf-8', newline='\n') as output:
                output.write(text)
    if stale:
        print('Generated documentation is stale: ' + ', '.join(stale))
        return 1
    count = sum(len(t['entries']) for h in headers for t in h['types'])
    print(f'{"Checked" if args.check else "Generated"}: {len(headers)} public headers, {count} API entries, {len(features)} capability routes, {len(chapters)} example chapters.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
