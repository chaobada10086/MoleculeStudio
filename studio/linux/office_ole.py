"""Write a MoleculeStudio OLE storage recognized by the Windows document server."""
import hashlib
import math
from tempfile import TemporaryDirectory
import os
from pathlib import Path
import re
import struct
import sys
import uuid
import xml.etree.ElementTree as ET

import gi
gi.require_version("Gsf", "1")
gi.require_version("GdkPixbuf", "2.0")
from gi.repository import Gsf, GdkPixbuf

CLASS = uuid.UUID("c000bc8c-8c39-4722-b93c-d40d3319e7b5")
SVG = "{http://www.w3.org/2000/svg}"


def wmf(svg, width, height):
    records = []
    def record(code, data=b""):
        records.append(struct.pack("<IH", 3+len(data)//2, code)+data)
    def words(code, *values):
        record(code, struct.pack("<"+"h"*len(values), *values))

    # Integer vector coordinates have substantially finer resolution than the display.
    scale = 20000/max(width, height)
    w, h = max(1, round(width*scale)), max(1, round(height*scale))
    words(0x0103, 8)
    words(0x020b, 0, 0)
    words(0x020c, h, w)
    root = ET.parse(svg).getroot()
    paths = list(root)
    vector = all(p.tag == SVG+"path" and not set(p.attrib)-{"d","fill-rule","fill","fill-opacity"} and
           p.get("fill-opacity","1") == "1" and re.fullmatch(r"[MLZ0-9eE+.,\s-]*", p.get("d","")) and
           re.fullmatch(r"rgb\([\d.,%\s]+\)",p.get("fill","")) for p in paths)
    if vector:
        # Office's PDF exporter outlines every triangle with a stock NULL_PEN.
        # An explicit PS_NULL pen gives a fill-only polygon in both Office hosts.
        record(0x02fa,struct.pack("<HhhI",5,0,0,0))
        words(0x012d,0)
        for path in paths:
            channels = re.findall(r"[\d.]+%?",path.get("fill"))
            rgb = [round(float(c[:-1])*2.55) if c.endswith("%") else round(float(c)) for c in channels]
            record(0x02fc,struct.pack("<HIB",0,rgb[0]|rgb[1]<<8|rgb[2]<<16,0)+b"\0")
            words(0x012d,1)
            words(0x0106,1 if path.get("fill-rule")=="evenodd" else 2)
            polys=[]
            for subpath in path.get("d").split("M")[1:]:
                coords=[float(v) for v in re.findall(r"[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?",subpath)]
                if len(coords)>=6:
                    polys.append([max(-32768,min(32767,round(v*scale))) for v in coords])
            if polys:
                sizes=[len(p)//2 for p in polys]
                record(0x0538,struct.pack("<H",len(polys))+struct.pack("<"+"H"*len(sizes),*sizes)+
                       b"".join(struct.pack("<"+"h"*len(p),*p) for p in polys))
            record(0x012d,struct.pack("<H",0x8000))
            words(0x01f0,1)
    else:
        pw=max(1,round(width*min(3.,1800/max(width,height))))
        ph=max(1,round(height*min(3.,1800/max(width,height))))
        image=GdkPixbuf.Pixbuf.new_from_file_at_scale(str(svg),pw,ph,False)
        bg=GdkPixbuf.Pixbuf.new(GdkPixbuf.Colorspace.RGB,False,8,pw,ph)
        bg.fill(0xffffffff)
        image.composite(bg,0,0,pw,ph,0,0,1,1,GdkPixbuf.InterpType.BILINEAR,255)
        ok,bmp=bg.save_to_bufferv("bmp",[],[])
        if not ok: raise ValueError("Could not encode the OLE preview")
        record(0x0f43,struct.pack("<IH8h",0xcc0020,0,ph,pw,0,0,h,w,0,0)+bytes(bmp)[14:])
    words(0)
    length=sum(map(len,records))
    picture=struct.pack("<HHHIHIH",1,9,0x300,9+length//2,2 if vector else 1,max(map(len,records))//2,0)+b"".join(records)
    return picture, (w,h) if vector else None


def write_storage(source, svg, target, extent=None, original=None):
    native=Path(source).read_bytes()
    if not native.startswith((b"R62EXT ",b"T01DOC ")):
        raise ValueError("Expected a native MoleculeStudio document")
    view=list(map(float,ET.parse(svg).getroot().get("viewBox").split()))
    width,height=view[2:]
    if not all(math.isfinite(x) and x>0 for x in (width,height)):
        raise ValueError("Invalid preview size")
    picture, logical=wmf(svg,width,height)
    if extent is None:
        extent=tuple(max(1,round(x*2540/72)) for x in (width,height))
    with TemporaryDirectory(dir=Path(target).parent) as folder:
        temp=Path(folder)/"object"
        sink=Gsf.OutputStdio.new(str(temp))
        root=Gsf.OutfileMSOle.new(sink)
        if not root.set_class_id(CLASS.bytes_le): raise IOError("Cannot set OLE class")
        streams={"CONTENTS":native,"MSXT":struct.pack("<ii",*extent),"MSPV":struct.pack("<I",2),
                 "\x01Ole":struct.pack("<IIIII",0x02000001,0,0,0,0),
                 "\x02OlePres000":struct.pack("<10I",0xffffffff,3,4,1,0xffffffff,2,0,*extent,len(picture))+picture}
        if original is not None and original != native:
            # Bind the original Linux editing bytes to this particular Windows payload.
            streams["MSSource"] = hashlib.sha256(native).digest() + original
        for name,data in streams.items():
            child=root.new_child(name,False)
            if not child or not child.write(data) or not child.close(): raise IOError("Cannot save OLE stream "+name)
        if not root.close(): raise IOError("Cannot finish OLE storage")
        os.replace(temp,target)
    return picture, logical



def vector_preview(svg, folder, width, height, picture, logical, mixed_encoder=None):
    if logical:
        # Standalone WMF files carry a placeable header before the OLE metafile.
        units=max(1,min(65535,round(logical[0]*72/width)))
        header=struct.pack("<IHhhhhHI",0x9ac6cdd7,0,0,0,*logical,units,0)
        checksum=0
        for value in struct.unpack("<10H",header):
            checksum ^= value
        path=folder/"preview.wmf"
        path.write_bytes(header+struct.pack("<H",checksum)+picture)
    else:
        if mixed_encoder is None:
            from office_emf import mixed_emf as mixed_encoder
        mixed = mixed_encoder(svg,width,height)
        if mixed is not None:
            path = folder/"preview.emf"
            path.write_bytes(mixed)
            return path
        return None
    return path


def page_preview(svg, folder, width, height, picture, logical):
    path = vector_preview(svg, folder, width, height, picture, logical)
    if path is None:
        path=folder/"preview.png"
        scale=min(4.,2400/max(width,height))
        image=GdkPixbuf.Pixbuf.new_from_file_at_scale(str(svg),max(1,round(width*scale)),max(1,round(height*scale)),False)
        image.savev(str(path),"png",[],[])
    return path


def word(target, storage, preview, width, height):
    from zipfile import ZipFile, ZIP_DEFLATED
    relationships = "http://schemas.openxmlformats.org/package/2006/relationships"
    rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
    with ZipFile(target, "w", ZIP_DEFLATED) as doc:
        media_type={".wmf":"image/x-wmf",".emf":"image/x-emf",".png":"image/png"}[preview.suffix]
        doc.writestr("[Content_Types].xml", f"""<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
          <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
          <Default Extension="bin" ContentType="application/vnd.openxmlformats-officedocument.oleObject"/>
          <Default Extension="{preview.suffix[1:]}" ContentType="{media_type}"/>
          <Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>""")
        doc.writestr("_rels/.rels", f'<Relationships xmlns="{relationships}"><Relationship Id="main" Type="{rel}/officeDocument" Target="word/document.xml"/></Relationships>')
        doc.writestr("word/_rels/document.xml.rels", f'<Relationships xmlns="{relationships}"><Relationship Id="drawing" Type="{rel}/oleObject" Target="embeddings/drawing.bin"/><Relationship Id="preview" Type="{rel}/image" Target="media/{preview.name}"/></Relationships>')
        doc.writestr("word/document.xml", f"""<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"
          xmlns:r="{rel}" xmlns:v="urn:schemas-microsoft-com:vml" xmlns:o="urn:schemas-microsoft-com:office:office">
          <w:body><w:p><w:r><w:object w:dxaOrig="{round(width*20)}" w:dyaOrig="{round(height*20)}">
          <v:shape id="_x0000_i1025" style="width:{width:.3f}pt;height:{height:.3f}pt" o:ole="">
          <v:imagedata r:id="preview" o:title="MoleculeStudio"/></v:shape>
          <o:OLEObject Type="Embed" ProgID="MoleculeStudio.Document.1" ShapeID="_x0000_i1025" DrawAspect="Content" ObjectID="_123456789" r:id="drawing"/>
          </w:object></w:r></w:p><w:sectPr>
          <w:pgSz w:w="12240" w:h="15840"/>
          <w:pgMar w:top="1440" w:right="1800" w:bottom="1440" w:left="1800" w:header="720" w:footer="720" w:gutter="0"/>
          </w:sectPr></w:body></w:document>""")
        doc.write(storage,"word/embeddings/drawing.bin")
        doc.write(preview,"word/media/"+preview.name)


def export(source, svg, target):
    target=Path(target)
    suffix=target.suffix.lower()
    if suffix not in (".docx",".pptx"):
        raise ValueError("Choose .docx or .pptx for an editable Office drawing")
    with TemporaryDirectory(dir=target.parent) as folder:
        folder=Path(folder)
        storage=folder/"drawing.bin"
        out=folder/target.name
        width,height=map(float,ET.parse(svg).getroot().get("viewBox").split()[2:])
        if suffix==".docx":
            scale=min(1.,420/width,600/height)
            placed=(width*scale,height*scale)
            picture,logical=write_storage(source,svg,storage,tuple(max(1,round(pt*2540/72)) for pt in placed))
            preview=page_preview(svg,folder,width,height,picture,logical)
            word(out,storage,preview,*placed)
        else:
            from zipfile import ZipFile, ZIP_DEFLATED
            template=Path(__file__).parent/"assets/office/drawing-template.pptx"
            if not template.exists():
                template=Path(__file__).parent.parent/"assets/office/drawing-template.pptx"
            with ZipFile(template) as base, ZipFile(out,"w",ZIP_DEFLATED) as result:
                pres=ET.fromstring(base.read("ppt/presentation.xml"))
                size=pres.find("{http://schemas.openxmlformats.org/presentationml/2006/main}sldSz")
                sw,sh=int(size.get("cx")),int(size.get("cy"))
                scale=min(12700.,.9*sw/width,.9*sh/height)
                w,h=max(1,round(width*scale)),max(1,round(height*scale))
                # OLE uses hundredths of a millimetre. The slide uses 360 EMU per unit.
                picture,logical=write_storage(source,svg,storage,(max(1,round(w/360)),max(1,round(h/360))))
                preview=page_preview(svg,folder,width,height,picture,logical)
                for member in base.namelist():
                    data=base.read(member)
                    if member=="ppt/slides/slide1.xml":
                        slide=ET.fromstring(data)
                        frame=next(slide.iterfind(".//{*}graphicFrame"))
                        for node in frame.iterfind(".//{*}off"):
                            node.attrib.update(x=str((sw-w)//2),y=str((sh-h)//2))
                        for node in frame.iterfind(".//{*}ext"):
                            node.attrib.update(cx=str(w),cy=str(h))
                        ole=next(frame.iterfind(".//{*}oleObj"))
                        ole.attrib.update(imgW=str(w),imgH=str(h),showAsIcon="0")
                        data=ET.tostring(slide,encoding="utf-8",xml_declaration=True)
                    elif member=="ppt/embeddings/oleObject1.bin": data=storage.read_bytes()
                    elif member=="ppt/media/image1.png":
                        member="ppt/media/image1"+preview.suffix
                        data=preview.read_bytes()
                    elif preview.suffix!=".png" and member=="ppt/slides/_rels/slide1.xml.rels":
                        data=data.replace(b'../media/image1.png',(".."+"/media/image1"+preview.suffix).encode())
                    elif preview.suffix!=".png" and member=="[Content_Types].xml":
                        ext=preview.suffix[1:]
                        data=data.replace(b'</Types>',f'<Default Extension="{ext}" ContentType="image/x-{ext}"/></Types>'.encode())
                    result.writestr(member,data)
        os.replace(out,target)


if __name__=="__main__":
    if len(sys.argv)==5:
        import subprocess
        # The Windows OLE server reads its sizing policy from the embedded
        # document. Generate it alongside the preview from the same session.
        with TemporaryDirectory() as folder:
            embedded = Path(folder) / "embedded.r62doc"
            subprocess.run([sys.argv[4], "--export-office-preview", sys.argv[1],
                            sys.argv[2], str(embedded)], check=True)
            export(embedded, sys.argv[2], sys.argv[3])
    else:
        export(*sys.argv[1:4])
