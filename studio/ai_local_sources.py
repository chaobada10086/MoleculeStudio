"""Resolve model requests exclusively against the current native host request."""

def resolve_source(attachments, args):
    if "path" in args:
        raise ValueError("请上传图片或 PDF 并使用本轮附件编号")
    number = args.get("attachment")
    sources = getattr(attachments, "sources", [])
    if type(number) is not int or not 1 <= number <= len(sources):
        raise ValueError("附件编号不存在，请先上传文件")
    source = sources[number - 1]
    count = 1
    if source["kind"] == "pdf":
        count = next((n for p, n in attachments.pdfs if p == source["path"]), 0)
    page = args.get("page", 1)
    if type(page) is not int or not 1 <= page <= count:
        raise ValueError("页码超出附件范围")
    return number, source, page, count


def save_previews(blocks, workspace):
    """Make actual page images accessible to the advanced agent's read_image."""
    import base64,tempfile
    directory = workspace
    paths=[]
    for block in blocks:
        if block.get('type')!='image_url': continue
        uri=block['image_url']['url']
        with tempfile.NamedTemporaryFile(dir=directory,suffix='.jpg',delete=False) as f:
            f.write(base64.b64decode(uri.split(',',1)[1],validate=True));paths.append(f.name)
    return paths
