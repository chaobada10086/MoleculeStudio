"""Encode Cairo's interleaved opaque polygons and PNG images as an Office page preview."""
import base64
import io
import math
import re
import struct
from xml.etree import ElementTree as ET

from PIL import Image

SVG = "{http://www.w3.org/2000/svg}"
HREF = "{http://www.w3.org/1999/xlink}href"
NUMBER = r"[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?"
SCALE = 20  # 20 logical units per point, or 1440 per inch


def pack(fmt, *values):
    return struct.pack("<" + fmt, *values)


def logical(value):
    return round(value * SCALE)


def placed_image(picture, matrix, width, height):
    a,b,c,d,x,y = matrix
    pw,ph = picture.size
    a,b = a*width/pw, b*width/pw
    c,d = c*height/ph, d*height/ph
    image = picture.convert("RGBA")
    if b == c == 0:
        if a < 0:
            image = image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            x += a*pw
        if d < 0:
            image = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            y += d*ph
        return image, x, y, abs(a)*pw, abs(d)*ph
    # Transform only the image. Some Office print drivers flatten the whole
    # metafile when an alpha-blended bitmap uses a rotated device context.
    det = a*d-b*c
    if abs(det) < 1e-12:
        return None
    xs = (0, a*pw, c*ph, a*pw+c*ph)
    ys = (0, b*pw, d*ph, b*pw+d*ph)
    left,top = min(xs),min(ys)
    w,h = max(xs)-left,max(ys)-top
    density = min(max(4.,1/min(math.hypot(a,b),math.hypot(c,d))),4096/max(w,h))
    cols,rows = max(1,math.ceil(w*density)),max(1,math.ceil(h*density))
    inverse = (d*w/cols/det, -c*h/rows/det, (d*left-c*top)/det,
               -b*w/cols/det, a*h/rows/det, (a*top-b*left)/det)
    image = image.transform((cols,rows), Image.Transform.AFFINE, inverse, Image.Resampling.BICUBIC)
    return image, x+left, y+top, w, h


def mixed_emf(svg, width, height):
    root = ET.parse(svg).getroot()
    if not any(el.tag == SVG+"use" for el in root) or not any(el.tag == SVG+"path" for el in root):
        return None
    records = bytearray()
    count = 0

    def emit(kind, payload):
        nonlocal count
        padding = -len(payload) % 4
        records.extend(pack("II", kind, 8 + len(payload) + padding))
        records.extend(payload)
        records.extend(bytes(padding))
        count += 1

    images = {}
    brush = None
    emit(38, pack("IIiiI", 1, 5, 0, 0, 0))  # PS_NULL prevents outlines on filled polygons
    emit(37, pack("I", 1))
    for el in root:
        if el.tag == SVG+"defs":
            for child in el:
                if child.tag != SVG+"image" or child.get("x","0") != "0" or child.get("y","0") != "0":
                    return None
                images["#"+child.get("id","")] = child
        elif el.tag == SVG+"path":
            fill = re.fullmatch(r"rgb\(\s*([\d.]+)%\s*,\s*([\d.]+)%\s*,\s*([\d.]+)%\s*\)", el.get("fill",""))
            if not fill or set(el.attrib)-{"d","fill","fill-rule","fill-opacity"} or el.get("fill-opacity","1") != "1":
                return None
            path = el.get("d","")
            if not re.fullmatch(r"[MLZ0-9eE+.,\s-]*",path):
                return None
            color = sum(round(float(v)*2.55) << (8*i) for i,v in enumerate(fill.groups()))
            if color != brush:
                if brush is not None:
                    emit(37, pack("I",0x80000000))
                    emit(40, pack("I",2))
                emit(39, pack("4I",2,0,color,0))
                emit(37, pack("I",2))
                brush = color
            emit(19, pack("I",1 if el.get("fill-rule")=="evenodd" else 2))
            points = []
            sizes = []
            for part in path.split("M")[1:]:
                coords = [logical(float(v)) for v in re.findall(NUMBER,part)]
                if len(coords) % 2:
                    return None
                if len(coords) >= 6:
                    sizes.append(len(coords)//2)
                    points.extend(coords)
            if points:
                xs,ys = points[::2],points[1::2]
                emit(8, pack("4i2I",min(xs),min(ys),max(xs),max(ys),len(sizes),len(xs)) +
                     pack(f"{len(sizes)}I",*sizes) + pack(f"{len(points)}i",*points))
        elif el.tag == SVG+"use":
            image = images.get(el.get(HREF))
            matrix = re.fullmatch(r"matrix\(([^()]*)\)",el.get("transform","matrix(1,0,0,1,0,0)"))
            if image is None or not matrix or set(el.attrib)-{HREF,"transform"}:
                return None
            terms = [float(v) for v in re.findall(NUMBER,matrix[1])]
            if len(terms)!=6 or not all(map(math.isfinite,terms)):
                return None
            uri = image.get(HREF,"")
            prefix = "data:image/png;base64,"
            if not uri.startswith(prefix):
                return None
            with Image.open(io.BytesIO(base64.b64decode(uri[len(prefix):]))) as picture:
                pw,ph = picture.size
                if pw*ph > 16000000:
                    return None
                placed = placed_image(picture,terms,float(image.get("width")),float(image.get("height")))
                if placed is None:
                    return None
                bitmap,x,y,w,h = placed
                pw,ph = bitmap.size
                bits = bitmap.convert("RGBa").tobytes("raw","BGRa",0,-1)
            x,y,w,h = map(logical,(x,y,w,h))
            if min(w,h)<1:
                return None
            bmi = pack("IiiHHIIiiII",40,pw,ph,1,32,0,len(bits),0,0,0,0)
            emit(114,pack("4i4iI2i6f6I2i",x,y,x+w,y+h,x,y,w,h,0x01ff0000,0,0,
                          1,0,0,1,0,0,0,0,108,len(bmi),148,len(bits),pw,ph)+bmi+bits)
        else:
            return None
    emit(14,pack("III",0,0,20))
    header = pack("II4i4iIIIIHHIII4i",1,88,0,0,logical(width),logical(height),0,0,
                  round(width*2540/72),round(height*2540/72),0x464d4520,0x10000,
                  88+len(records),count+1,3,0,0,0,0,14400,14400,254,254)
    return header+records
