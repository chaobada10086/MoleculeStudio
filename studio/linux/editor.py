#!/usr/bin/python3
"""Linux desktop preview for editing CDXML molecular graphs."""
import math
import os
import sys
import tempfile
from pathlib import Path
from xml.dom.minidom import parseString

import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib

EMPTY = b'<CDXML BondLength="30"><page id="1"><fragment id="2"/></page></CDXML>'
ELEMENTS = ("? H He Li Be B C N O F Ne Na Mg Al Si P S Cl Ar K Ca Sc Ti V Cr Mn Fe Co Ni Cu Zn Ga Ge As Se Br Kr "
            "Rb Sr Y Zr Nb Mo Tc Ru Rh Pd Ag Cd In Sn Sb Te I Xe Cs Ba La Ce Pr Nd Pm Sm Eu Gd Tb Dy Ho Er Tm Yb Lu "
            "Hf Ta W Re Os Ir Pt Au Hg Tl Pb Bi Po At Rn Fr Ra Ac Th Pa U Np Pu Am Cm Bk Cf Es Fm Md No Lr Rf Db Sg Bh Hs Mt Ds Rg Cn Nh Fl Mc Lv Ts Og").split()


class Drawing:
    def __init__(self, data):
        self.xml = parseString(data)
        if self.xml.documentElement.tagName != "CDXML":
            raise ValueError("请选择 CDXML 文件")
        pages = self.xml.documentElement.getElementsByTagName("page")
        if not pages:
            raise ValueError("文件缺少页面")
        self.page = pages[0]
        self.fragments = [n for n in self.page.childNodes if n.nodeName == "fragment"]
        self.atoms = {}
        self.bonds = []
        for frag in self.fragments:
            for n in frag.childNodes:
                if n.nodeName == "n":
                    coords = n.getAttribute("p").split()
                    if len(coords) == 2:
                        x, y = map(float, coords)
                        if math.isfinite(x) and math.isfinite(y):
                            self.atoms[n.getAttribute("id")] = (x, y, n)
                elif n.nodeName == "b":
                    self.bonds.append(n)
        ids = [n.getAttribute("id") for n in self.xml.getElementsByTagName("*")]
        self.next_id = max((int(i) for i in ids if i.isdecimal()), default=0) + 1

    def append(self, parent, tag, **attrs):
        node = self.xml.createElement(tag)
        node.setAttribute("id", str(self.next_id))
        self.next_id += 1
        for key, value in attrs.items():
            node.setAttribute(key, value)
        parent.appendChild(node)
        return node

    def add_atom(self, x, y, anchor=None):
        if not self.fragments:
            self.fragments.append(self.append(self.page, "fragment"))
        parent = self.atoms[anchor][2].parentNode if anchor else self.fragments[0]
        n = self.append(parent, "n", p=f"{x:.6f} {y:.6f}")
        key = n.getAttribute("id")
        self.atoms[key] = (x, y, n)
        return key

    def connect(self, a, b):
        if a == b or self.atoms[a][2].parentNode != self.atoms[b][2].parentNode:
            return False
        if any({n.getAttribute("B"), n.getAttribute("E")} == {a,b} for n in self.bonds):
            return False
        self.bonds.append(self.append(self.atoms[a][2].parentNode, "b", B=a, E=b, Order="1"))
        return True

    def bytes(self):
        return self.xml.toxml(encoding="utf-8")


class Editor(Gtk.Window):
    def __init__(self, path=None):
        super().__init__(title="MoleculeStudio · Linux 预览版")
        self.set_default_size(980, 680)
        self.doc = Drawing(path.read_bytes() if path else EMPTY)
        self.saved = self.doc.bytes()
        self.path = path
        self.history = []
        self.selected = None
        self.drag = None
        self.scale = 2
        self.offset = (50,50)
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.add(box)
        bar = Gtk.Box(spacing=6, margin=8)
        box.pack_start(bar, False, False, 0)
        for name, fn in [("新建",self.new), ("打开…",self.open_file), ("另存为…",self.save),
                         ("撤销",self.undo), ("适应窗口",self.fit)]:
            button = Gtk.Button(label=name)
            button.connect("clicked", lambda _, f=fn: f())
            bar.pack_start(button, False, False, 0)
        self.mode = Gtk.ComboBoxText()
        for name in ["移动原子","添加碳原子","连接两个原子","修改键级"]:
            self.mode.append_text(name)
        self.mode.set_active(0)
        self.mode.connect("changed",lambda _: self.clear_selection())
        bar.add(self.mode)
        self.area = Gtk.DrawingArea()
        self.area.add_events(256 | 512 | 4)
        self.area.connect("draw",self.draw)
        self.area.connect("button-press-event",self.press)
        self.area.connect("button-release-event",self.release)
        box.pack_start(self.area,True,True,0)
        self.status = Gtk.Label(label="当前编辑第一页的结构式。其他页面、文字和图形保留在文件中。")
        box.pack_end(self.status,False,False,5)
        self.connect("delete-event",lambda *_: not self.discard())
        self.connect("destroy",lambda *_: Gtk.main_quit() if Gtk.main_level() else None)
        self.show_all()
        GLib.idle_add(self.fit)

    def clear_selection(self):
        self.selected = self.drag = None
        self.area.queue_draw()

    def message(self,text):
        dlg = Gtk.MessageDialog(transient_for=self, modal=True, message_type=Gtk.MessageType.INFO,
                                buttons=Gtk.ButtonsType.OK,text=str(text))
        dlg.run()
        dlg.destroy()

    def discard(self):
        if self.doc.bytes() == self.saved:
            return True
        dlg = Gtk.MessageDialog(transient_for=self, modal=True, message_type=Gtk.MessageType.QUESTION,
                                buttons=Gtk.ButtonsType.YES_NO,text="放弃未保存的修改？")
        yes = dlg.run() == Gtk.ResponseType.YES
        dlg.destroy()
        return yes

    def reset(self,data,path):
        doc = Drawing(data)
        self.doc, self.path, self.saved = doc,path,doc.bytes()
        self.history.clear()
        self.clear_selection()
        self.fit()

    def new(self):
        if self.discard():
            self.reset(EMPTY,None)

    def choose(self,saving):
        dlg = Gtk.FileChooserDialog(title="另存为 CDXML" if saving else "打开 CDXML",transient_for=self,
                                    action=Gtk.FileChooserAction.SAVE if saving else Gtk.FileChooserAction.OPEN)
        dlg.add_buttons("取消",Gtk.ResponseType.CANCEL,"确定",Gtk.ResponseType.OK)
        if saving:
            dlg.set_do_overwrite_confirmation(True)
            dlg.set_current_name(self.path.name if self.path else "结构式.cdxml")
        path = dlg.get_filename() if dlg.run() == Gtk.ResponseType.OK else None
        dlg.destroy()
        return Path(path) if path else None

    def open_file(self):
        path = self.choose(False)
        if path and self.discard():
            try:
                self.reset(path.read_bytes(),path)
            except Exception as exc:
                self.message(exc)

    def save_to(self,path):
        data = self.doc.bytes()
        with tempfile.NamedTemporaryFile(dir=path.parent,delete=False) as tmp:
            name = tmp.name
        try:
            Path(name).write_bytes(data)
            os.replace(name,path)
        finally:
            Path(name).unlink(missing_ok=True)
        self.path,self.saved = path,data
        self.status.set_text("已保存 " + str(path))

    def save(self):
        path = self.choose(True)
        if path:
            try:
                self.save_to(path)
            except OSError as exc:
                self.message(exc)

    def undo(self):
        if self.history:
            self.doc = Drawing(self.history.pop())
            self.clear_selection()

    def fit(self):
        pts = list(self.doc.atoms.values())
        w,h = self.area.get_allocated_width(),self.area.get_allocated_height()
        if pts:
            xs,ys,_ = zip(*pts)
            self.scale = min(3,max(0.01,min((w-60)/max(1,max(xs)-min(xs)),(h-60)/max(1,max(ys)-min(ys)))))
            self.offset = ((w-(max(xs)+min(xs))*self.scale)/2,(h-(max(ys)+min(ys))*self.scale)/2)
        else:
            self.scale,self.offset = 2,(50,50)
        self.area.queue_draw()
        return False

    def screen(self,key):
        x,y,_ = self.doc.atoms[key]
        return x*self.scale+self.offset[0], y*self.scale+self.offset[1]

    def nearest(self,x,y):
        return min((k for k in self.doc.atoms if math.dist((x,y),self.screen(k)) <= 12),
                   key=lambda k: math.dist((x,y),self.screen(k)),default=None)

    def edges(self):
        for b in self.doc.bonds:
            a,e = b.getAttribute("B"), b.getAttribute("E")
            if a in self.doc.atoms and e in self.doc.atoms:
                yield b,self.screen(a),self.screen(e)

    def press(self,_,ev):
        if ev.button != 1:
            return
        p = (ev.x,ev.y)
        k = self.nearest(*p)
        mode = self.mode.get_active()
        before = self.doc.bytes()
        if mode == 0:
            self.drag = k
        elif mode == 1 and k is None:
            self.doc.add_atom((ev.x-self.offset[0])/self.scale,(ev.y-self.offset[1])/self.scale)
        elif mode == 2 and k:
            if self.selected:
                if not self.doc.connect(self.selected,k):
                    self.status.set_text("请选择同一片段内尚未成键的两个原子。")
                self.selected = None
            else:
                self.selected = k
        elif mode == 3:
            best, distance = None,10
            for b,(x,y),(u,v) in self.edges():
                dx,dy = u-x,v-y
                t = min(1,max(0,((ev.x-x)*dx+(ev.y-y)*dy)/(dx*dx+dy*dy or 1)))
                d = math.dist(p,(x+t*dx,y+t*dy))
                if d < distance:
                    best,distance = b,d
            if best is not None and best.getAttribute("Order") in ("","1","2","3") and not best.hasAttribute("Display"):
                best.setAttribute("Order",str(int(best.getAttribute("Order") or "1")%3+1))
        if before != self.doc.bytes():
            self.history.append(before)
        self.area.queue_draw()

    def release(self,_,ev):
        if self.drag and ev.button == 1:
            if math.dist(self.screen(self.drag),(ev.x,ev.y)) > 2:
                self.history.append(self.doc.bytes())
                x,y = (ev.x-self.offset[0])/self.scale,(ev.y-self.offset[1])/self.scale
                n = self.doc.atoms[self.drag][2]
                n.setAttribute("p",f"{x:.6f} {y:.6f}")
                self.doc.atoms[self.drag] = (x,y,n)
            self.drag = None
            self.area.queue_draw()

    def draw(self,_,c):
        c.set_source_rgb(1,1,1)
        c.paint()
        c.set_source_rgb(.12,.17,.21)
        c.set_line_width(1.7)
        for b,(x,y),(u,v) in self.edges():
            length = math.hypot(u-x,v-y) or 1
            dx,dy = (y-v)/length,(u-x)/length
            style = b.getAttribute("Display")
            if style in ("WedgeEnd","WedgedHashEnd"):
                x,y,u,v = u,v,x,y
            if style in ("WedgeBegin","WedgeEnd"):
                c.move_to(x,y)
                c.line_to(u+4*dx,v+4*dy)
                c.line_to(u-4*dx,v-4*dy)
                c.close_path()
                c.fill()
                continue
            if style in ("WedgedHashBegin","WedgedHashEnd"):
                for i in range(1,9):
                    t = i/8
                    cx,cy = x+t*(u-x),y+t*(v-y)
                    c.move_to(cx-4*t*dx,cy-4*t*dy)
                    c.line_to(cx+4*t*dx,cy+4*t*dy)
                    c.stroke()
                continue
            offsets = {"2":(-2,2),"3":(-4,0,4)}.get(b.getAttribute("Order"),(0,))
            for off in offsets:
                c.move_to(x+off*dx,y+off*dy)
                c.line_to(u+off*dx,v+off*dy)
                c.stroke()
        c.set_font_size(15)
        for k,(_,__,n) in self.doc.atoms.items():
            x,y = self.screen(k)
            elem = n.getAttribute("Element") or "6"
            label = ELEMENTS[int(elem)] if elem.isdecimal() and int(elem)<len(ELEMENTS) else "?"
            c.set_source_rgb(.08,.44,.63) if k == self.selected else c.set_source_rgb(.12,.17,.21)
            if elem == "6":
                c.arc(x,y,3,0,2*math.pi)
                c.fill()
            else:
                c.set_source_rgb(1,1,1)
                c.rectangle(x-9,y-9,22,18)
                c.fill()
                c.set_source_rgb(.08,.34,.55)
                c.move_to(x-6,y+5)
                c.show_text(label)


if __name__ == "__main__":
    Editor(Path(sys.argv[1]) if len(sys.argv)>1 else None)
    Gtk.main()
