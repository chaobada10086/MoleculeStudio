from pathlib import Path
import json
import sys

sys.path.insert(0, str(Path(__file__).parent / 'ocr-deps'))
import pypdfium2 as pdfium
from rapidocr_onnxruntime import RapidOCR

source = pdfium.PdfDocument(r'C:\Users\ChaobaDa\Desktop\知乎缩写.pdf')
ocr = RapidOCR()
all_pages = []
for page_index, page in enumerate(source):
    if page_index == 0:
        all_pages.append([])
        continue
    image = page.render(scale=3).to_pil()
    result, _ = ocr(image)
    rows = []
    for box, value, confidence in result or []:
        x = sum(p[0] for p in box) / 4
        y = sum(p[1] for p in box) / 4
        if 245 < x < 1110 and 210 < y < 2000:
            rows.append({'x': round(x), 'y': round(y), 'text': value, 'confidence': round(confidence, 2)})
    all_pages.append(rows)
    print(page_index + 1, len(rows), flush=True)
Path('work/zhihu-ocr.json').write_text(json.dumps(all_pages, ensure_ascii=False, indent=2), encoding='utf-8')
with Path('work/zhihu-ocr.txt').open('w', encoding='utf-8') as output:
    for page_index, rows in enumerate(all_pages):
        output.write(f'\nPAGE {page_index+1}\n')
        for row in sorted(rows, key=lambda item: (item['y'], item['x'])):
            output.write(f"{row['x']:4} {row['y']:4} {row['text']}\n")
