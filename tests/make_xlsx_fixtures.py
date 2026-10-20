"""Deterministic OOXML fixtures, authored independently of the C++ reader.

Uses only Python's standard library. Some fixtures intentionally contain invalid
numbers, missing formula caches and broken XML that a workbook writer would fix.
"""
from pathlib import Path
from xml.etree.ElementTree import Element, SubElement, tostring
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo

ROOT = Path(__file__).resolve().parents[1]
NS = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
REL = 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'
HEAD = ['Имя файла', 'Тип', 'Длина', 'Ширина', 'Толщина', 'Количество']


def raw(value=None, kind='n', formula=None, rich=None, style=None):
    return dict(value=value, kind=kind, formula=formula, rich=rich, style=style)


def sheet(rows, shared, damaged=False):
    root = Element('worksheet', xmlns=NS)
    SubElement(root, 'dimension', ref=f'A1:F{len(rows)}')
    views = SubElement(root, 'sheetViews')
    view = SubElement(views, 'sheetView', workbookViewId='0')
    SubElement(view, 'pane', ySplit='1', topLeftCell='A2', activePane='bottomLeft', state='frozen')
    cols = SubElement(root, 'cols')
    for i, width in enumerate([28, 24, 17, 17, 16, 18], 1):
        SubElement(cols, 'col', min=str(i), max=str(i), width=str(width), customWidth='1')
    data = SubElement(root, 'sheetData')
    for r, values in enumerate(rows, 1):
        row = SubElement(data, 'row', r=str(r), ht='24', customHeight='1')
        for c, value in enumerate(values, 1):
            if value is None:
                continue
            item = value if isinstance(value, dict) else raw(value, 'inlineStr' if isinstance(value, str) else 'n')
            attrs = dict(r=f'{chr(64+c)}{r}')
            if item['kind']:
                attrs['t'] = item['kind']
            if r == 1 or item.get('style'):
                attrs['s'] = '1' if r == 1 else str(item['style'])
            cell = SubElement(row, 'c', **attrs)
            if item.get('formula') is not None:
                SubElement(cell, 'f').text = item['formula']
            if item['kind'] == 'inlineStr':
                inline = SubElement(cell, 'is')
                for text in item.get('rich') or [str(item['value'])]:
                    parent = SubElement(inline, 'r') if item.get('rich') else inline
                    SubElement(parent, 't', {'xml:space': 'preserve'}).text = text
            elif item['kind'] == 's':
                text = str(item['value'])
                if text not in shared:
                    shared.append(text)
                SubElement(cell, 'v').text = str(shared.index(text))
            elif item['value'] is not None:
                SubElement(cell, 'v').text = str(item['value'])
    SubElement(root, 'autoFilter', ref=f'A1:F{len(rows)}')
    result = tostring(root, encoding='utf-8', xml_declaration=True)
    return result[:-18] if damaged else result


STYLES = f'''<styleSheet xmlns="{NS}"><fonts count="2"><font><sz val="11"/><name val="Calibri"/></font><font><b/><sz val="11"/><name val="Calibri"/></font></fonts><fills count="3"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill><fill><patternFill patternType="solid"><fgColor rgb="FFDBEAFE"/><bgColor indexed="64"/></patternFill></fill></fills><borders count="1"><border/></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="3"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="0" fontId="1" fillId="2" borderId="0" xfId="0" applyFont="1" applyFill="1"/><xf numFmtId="14" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/></cellXfs><cellStyles count="1"><cellStyle name="Normal" xfId="0" builtinId="0"/></cellStyles></styleSheet>'''


def workbook(path, rows, damaged=False, second=False):
    shared = []
    first = sheet(rows, shared, damaged)
    other = sheet([HEAD, ['', 'Не импортировать', 1, 2, 3, 4]], shared) if second else None
    types = f'''<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/parts.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/><Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>{'<Override PartName="/xl/worksheets/other.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>' if second else ''}</Types>'''
    book = f'''<workbook xmlns="{NS}" xmlns:r="{REL}"><bookViews><workbookView activeTab="{1 if second else 0}"/></bookViews><sheets><sheet name="Детали" sheetId="1" r:id="rId1"/>{'<sheet name="Другой" sheetId="2" r:id="rId2"/>' if second else ''}</sheets></workbook>'''
    rels = Element('Relationships', xmlns='http://schemas.openxmlformats.org/package/2006/relationships')
    for rid, kind, target in [('rId1', 'worksheet', 'worksheets/parts.xml'), ('styles', 'styles', 'styles.xml'), ('strings', 'sharedStrings', 'sharedStrings.xml')] + ([('rId2', 'worksheet', 'worksheets/other.xml')] if second else []):
        SubElement(rels, 'Relationship', Id=rid, Type=f'{REL}/{kind}', Target=target)
    strings = Element('sst', xmlns=NS, count=str(len(shared)), uniqueCount=str(len(shared)))
    for text in shared:
        SubElement(SubElement(strings, 'si'), 't').text = text
    entries = {'[Content_Types].xml': types, '_rels/.rels': f'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="book" Type="{REL}/officeDocument" Target="xl/workbook.xml"/></Relationships>', 'xl/workbook.xml': book, 'xl/_rels/workbook.xml.rels': tostring(rels), 'xl/worksheets/parts.xml': first, 'xl/styles.xml': STYLES, 'xl/sharedStrings.xml': tostring(strings)}
    if second:
        entries['xl/worksheets/other.xml'] = other
    path.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(path, 'w', ZIP_DEFLATED) as archive:
        for name, content in entries.items():
            item = ZipInfo(name, date_time=(2026, 10, 20, 0, 0, 0))
            item.compress_type = ZIP_DEFLATED
            archive.writestr(item, content)


valid = [HEAD, ['', 'Прямоугольник', 200, 100, 6, 8], ['', 'Круг', 150, None, 8, 3], ['', 'Шайба', 100, 50, 5, 10], ['Кронштейн', 'Прямоугольник', '120,5', '80.25', '2,5', 2], ['', 'Круг', 150, None, 8, 3]]
# The user-facing template is maintained separately; do not overwrite it here.
errors = valid[:4] + [['', 'Шайба', 60, 70, 5, 2], ['', 'Круг', 'abc', None, 8, 1], ['', 'Прямоугольник', 100, 50, 3, 2.5], ['', 'Треугольник', 100, 50, 3, 2], ['', 'Круг', -20, None, 3, 2], ['../Пластина', 'Прямоугольник', 100, 50, 3, 2], ['', 'Круг', 150, None, raw(None, formula='1+1'), 1]]
workbook(ROOT / 'examples/Детали-с-ошибками.xlsx', errors)
values = [HEAD,
    [raw('Пластина', 's'), 'Прямоугольник', 200.5, 100.25, '2,5', '4'],
    ['', 'Круг', 150, raw('#DIV/0!', 'e'), 8, 3],
    ['', 'Шайба', 100, 50, 0, None],
    [None, None, raw(None), None, raw(None, style=2), None],
    ['', ' круг ', raw(150, formula='75*2'), None, raw(2.5, formula='5/2'), raw(3, formula='1+2')],
    [raw('', 'inlineStr', rich=['Шайба ', 'опорная']), 'Шайба', 100, 50, 5, 10],
    ['', 'Круг', 100.12345678901234, None, None, '2,0'],
    ['', raw(1, 'b'), 100, None, 3, 1],
    ['', 'Круг', raw(45000, kind=None, style=2), None, 3, 1],
    ['', 'Круг', 100, None, raw('oops'), 1],
    ['', 'Круг', 100, None, raw('', formula='1+1'), 1],
    ['', 'Круг', 100, None, 2, raw(None, formula='1+1')],
    ['', 'Круг', 100, None, raw('#DIV/0!', 'e', formula='1/0'), 1],
    ['', 'Что-то', 100, 50, 3, 1],
    ['', 'Круг', 100, None, 3, 2.7],
    ['CON.txt', 'Круг', 100, None, 3, 1],
    ['', 'Круг', 0, None, 3, 1],
    [raw(123.5, kind=None), 'Круг', 100, None, 3, 1],
    ['', 'Круг', 100, None, raw('2026-10-20', kind='d'), 1],
    ['', 'Круг', 100, None, 2, 1.000000000000001],
]
workbook(ROOT / 'tests/fixtures/values.xlsx', values, second=True)
workbook(ROOT / 'tests/fixtures/damaged.xlsx', valid, damaged=True)
workbook(ROOT / 'tests/fixtures/no-header.xlsx', valid[1:])
workbook(ROOT / 'tests/fixtures/wrong-order.xlsx', [HEAD[:4] + [HEAD[5], HEAD[4]]] + valid[1:])
workbook(ROOT / 'tests/fixtures/empty.xlsx', [HEAD])
(ROOT / 'tests/fixtures/not-zip.xlsx').write_text('This is not an XLSX archive.', encoding='utf-8')
print('Generated example workbooks and independent OOXML fixtures')
