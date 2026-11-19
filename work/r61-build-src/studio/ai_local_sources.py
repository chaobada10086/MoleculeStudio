"""Read-only local image/PDF sources shared by OCSR and visual inspection."""
import os
from pathlib import Path
from contextlib import closing

def resolve_local(path, workspace=None):
    if not isinstance(path,str) or not path.strip() or len(path)>32767 or "\x00" in path:
        raise ValueError('path 必须是本地图片或 PDF 文件路径')
    if '://' in path: raise ValueError('path 只接受本地文件，不接受网络 URL')
    p=Path(path).expanduser()
    if not p.is_absolute():
        if workspace is None:
            from ai_dsh import workspace_dir
            workspace=workspace_dir()
        candidates=[Path(workspace)/p,Path.cwd()/p]
        p=next((p for p in candidates if p.is_file()),candidates[0])
    p=p.resolve()
    if not p.is_file(): raise ValueError('找不到本地图片或 PDF：'+str(p))
    try:
        size=p.stat().st_size
        with p.open('rb') as f: signature=f.read(1024)
    except OSError as exc: raise ValueError('无法读取本地文件：'+str(p)) from exc
    if size>512*1024*1024: raise ValueError('本地文件超过512 MB，请拆分后读取')
    if b'%PDF-' in signature or p.suffix.lower()=='.pdf':
        import pypdfium2 as pdfium
        try:
            with pdfium.PdfDocument(p) as doc: count=len(doc)
        except Exception as exc: raise ValueError('PDF无法打开，可能已损坏或有密码保护') from exc
        if count<1: raise ValueError('PDF没有可读取的页面')
        return {'path':p,'kind':'pdf','origin':'local_path'},count
    if size>64*1024*1024: raise ValueError('本地图片超过64 MB，请先缩小')
    try:
        from PIL import Image
        with Image.open(p) as image:
            if image.format not in ('PNG','JPEG','WEBP','GIF','BMP','TIFF'): raise ValueError('不支持的图片格式')
            if image.width*image.height>80_000_000: raise ValueError('图片尺寸超过8000万像素')
            image.verify()
    except Exception as exc: raise ValueError('本地文件不是有效的图片或 PDF：'+str(p)) from exc
    return {'path':p,'kind':'image','origin':'local_path'},1

def resolve_source(attachments,args):
    if ('path' in args)==('attachment' in args): raise ValueError('path 与 attachment 必须且只能指定一个')
    if 'path' in args:
        source,count=resolve_local(args['path'],getattr(attachments,'workspace',None)); number=None
    else:
        number=args.get('attachment'); sources=getattr(attachments,'sources',[])
        if type(number) is not int or not 1<=number<=len(sources): raise ValueError('附件编号不存在；也可用 path 指定本地图片或 PDF')
        source=sources[number-1];count=next((n for p,n in attachments.pdfs if p==source['path']),0) if source['kind']=='pdf' else 1
    page=args.get('page',1)
    if type(page) is not int or not 1<=page<=count: raise ValueError('页码超出范围：1–'+str(count))
    return number,source,page,count


def save_previews(blocks):
    """Make actual page images accessible to the advanced agent's read_image."""
    import base64,tempfile
    from ai_dsh import workspace_dir
    directory=workspace_dir()/'molecule_studio_previews';directory.mkdir(parents=True,exist_ok=True)
    paths=[]
    for block in blocks:
        if block.get('type')!='image_url': continue
        uri=block['image_url']['url']
        with tempfile.NamedTemporaryFile(dir=directory,suffix='.jpg',delete=False) as f:
            f.write(base64.b64decode(uri.split(',',1)[1],validate=True));paths.append(f.name)
    return paths
