#pragma once
void App::clipboardCheck(const std::string& dir){
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    // Keep the user's clipboard, including the original bitmap/metafile handles.
    struct Backup {
        std::vector<std::pair<UINT,HANDLE>> items;HWND owner;
        Backup(HWND h):owner(h){
            if(!OpenClipboard(h))throw std::runtime_error("clipboard backup locked");
            for(UINT f=EnumClipboardFormats(0);f;f=EnumClipboardFormats(f)){
                HANDLE source=GetClipboardData(f),copy=nullptr;if(!source)continue;
                if(f==CF_BITMAP)copy=CopyImage(source,IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION);
                else if(f==CF_ENHMETAFILE)copy=CopyEnhMetaFileW((HENHMETAFILE)source,nullptr);
                else if(f==CF_METAFILEPICT||f==CF_PALETTE||f==CF_OWNERDISPLAY){CloseClipboard();throw std::runtime_error("clipboard contains an unsupported backup handle");}
                else {auto n=GlobalSize(source);if(n){auto p=GlobalLock(source);if(p){copy=GlobalAlloc(GMEM_MOVEABLE,n);auto q=copy?GlobalLock(copy):nullptr;if(q){memcpy(q,p,n);GlobalUnlock(copy);}GlobalUnlock(source);}}}
                if(copy)items.push_back({f,copy});
            }CloseClipboard();
        }
        ~Backup(){if(OpenClipboard(owner)){EmptyClipboard();for(auto& p:items)if(SetClipboardData(p.first,p.second))p.second=nullptr;CloseClipboard();}for(auto p:items)if(p.second){if(p.first==CF_BITMAP)DeleteObject(p.second);else if(p.first==CF_ENHMETAFILE)DeleteEnhMetaFile((HENHMETAFILE)p.second);else GlobalFree(p.second);}}
    } backup(canvas);
    auto put=[&](UINT format,const void* data,size_t bytes){require(OpenClipboard(canvas),"open test clipboard");EmptyClipboard();auto result=studioio::put(format,data,bytes);CloseClipboard();require(result.ok(),"publish test image");};
    auto reset=[&](){s.cancel();s.doc=Document();s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();};
    shortcuts.reset();require(shortcuts.match('Y',kModControl,0)->command=="@redo","Ctrl+Y binding");require(shortcuts.match('Z',kModControl|kModShift,0)->command=="@redo","Ctrl+Shift+Z binding");
    auto pasteCheck=[&](const char* message){reset();auto before=s.doc.digest();require(shortcut('V',kModControl),"Ctrl+V dispatch");require(s.all().size()==1&&s.selected.size()==1&&s.selected[0].kind==ObjectKind::Path,message);auto after=s.doc.digest();require(shortcut('Z',kModControl)&&s.doc.digest()==before,"undo image paste");require(shortcut('Y',kModControl)&&s.doc.digest()==after,"Ctrl+Y restores image");};
    const int width=120,height=80;Bitmap sample(width,height,PixelFormat32bppARGB);
    {Graphics g(&sample);g.Clear(Color(0,0,0,0));SolidBrush fill(Color(255,30,150,210));g.FillRectangle(&fill,10,10,50,50);}
    UINT n=0,size=0;GetImageEncodersSize(&n,&size);std::vector<BYTE> codecs(size);auto enc=(ImageCodecInfo*)codecs.data();GetImageEncoders(n,size,enc);CLSID png={};bool found=false;for(UINT i=0;i<n;++i)if(wcscmp(enc[i].MimeType,L"image/png")==0){png=enc[i].Clsid;found=true;}require(found,"PNG encoder");
    clipboard_image::Stream stream;require(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&stream.value)),"PNG stream");require(sample.Save(stream.value,&png,nullptr)==Ok,"encode clipboard PNG");HGLOBAL memory=nullptr;GetHGlobalFromStream(stream.value,&memory);STATSTG stat={};stream.value->Stat(&stat,STATFLAG_NONAME);auto p=GlobalLock(memory);put(RegisterClipboardFormatW(L"PNG"),p,size_t(stat.cbSize.QuadPart));GlobalUnlock(memory);
    // A browser may publish a URL beside the picture: paste the picture.
    require(OpenClipboard(canvas),"open mixed clipboard");const wchar_t url[]=L"https://example.invalid/image.png";studioio::put(CF_UNICODETEXT,url,sizeof(url));CloseClipboard();pasteCheck("PNG takes priority over its URL");
    auto refs=s.all();auto oldBounds=studioio::selectionBounds(s);auto beforeMove=s.doc.digest();require(s.move(refs,Vec2(32,18)).ok(),"move pasted image");s.selected=refs;auto bounds=studioio::selectionBounds(s);require(std::abs(bounds.min.x-oldBounds.min.x-32)<.01&&std::abs(bounds.min.y-oldBounds.min.y-18)<.01,"image movement matches gesture");auto moved=s.doc.digest();require(shortcut('Z',kModControl)&&s.doc.digest()==beforeMove,"undo move");require(shortcut('Y',kModControl)&&s.doc.digest()==moved,"redo move");
    auto matrix=selection::Affine2::scaling((bounds.min+bounds.max)*.5,1.4,1.4);
    require(s.submit(Command("图片缩放",kInvalidateGeometry,[refs,matrix](CommandContext& c){selection::transformObjects(c.document,refs,matrix,selection::TransformOptions());return CommandResult::success();}),"图片缩放").ok(),"resize pasted image");
    s.selected=refs;auto scaledBounds=studioio::selectionBounds(s);require(scaledBounds.max.x-scaledBounds.min.x>1.35*(bounds.max.x-bounds.min.x),"image rendering follows resize");
    const auto savedCorners=s.doc.paths().alive(refs[0].id)->points;
    require(s.save(dir+"/pasted-image.r62doc").ok(),"save native image");require(s.save(dir+"/pasted-image.cdxml",true).ok(),"save CDXML image");
    for(auto file:{"/pasted-image.r62doc","/pasted-image.cdxml"}){Session loaded;require(loaded.open(dir+file).ok()&&loaded.all().size()==1,"reopen embedded image");auto corners=loaded.doc.paths().alive(loaded.all()[0].id)->points;for(size_t i=0;i<4;++i)require(core::length(corners[i]-savedCorners[i])<.001,"resized image corners roundtrip");bool rendered=false;for(auto& primitive:loaded.scene.primitives())if(primitive.text.compare(0,10,"CDXML-EMF:")==0){auto image=embedded::metafile(primitive.text.substr(10));require(image!=nullptr,"reload image pixels");Bitmap preview(width,height,PixelFormat32bppARGB);Graphics g(&preview);g.Clear(Color(0,0,0,0));g.DrawImage(image,Rect(0,0,width,height));Color transparent,color;preview.GetPixel(100,65,&transparent);preview.GetPixel(25,25,&color);require(transparent.GetA()==0&&color.GetA()>240&&color.GetB()>150,"PNG colors and transparency roundtrip");rendered=true;}require(rendered,"embedded image scene");}
    s.selected.clear();s.fit();screenshot(dir+"/pasted-image.png",false,false);
    // Exercise both orientation conventions and the common opaque 32-bit DIB.
    for(int orientation:{1,-1}){std::vector<BYTE> dib(sizeof(BITMAPINFOHEADER)+width*height*4,0);BITMAPINFOHEADER header={};header.biSize=sizeof(header);header.biWidth=width;header.biHeight=height*orientation;header.biPlanes=1;header.biBitCount=32;memcpy(dib.data(),&header,sizeof(header));for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto offset=sizeof(header)+(y*width+x)*4;dib[offset]=BYTE(y<height/2?200:30);dib[offset+2]=BYTE(y<height/2?30:200);}put(CF_DIB,dib.data(),dib.size());auto decoded=clipboard_image::decode(canvas);require(decoded!=nullptr,"DIB decoder");Color top;decoded->GetPixel(0,0,&top);require(top.GetA()==255&&top.GetB()==(orientation>0?30:200),"DIB orientation and reserved alpha");pasteCheck("paste DIB");}
    {std::vector<BYTE> dib(sizeof(BITMAPV5HEADER)+width*height*4,0);BITMAPV5HEADER header={};header.bV5Size=sizeof(header);header.bV5Width=width;header.bV5Height=-height;header.bV5Planes=1;header.bV5BitCount=32;header.bV5Compression=BI_BITFIELDS;header.bV5RedMask=0xff0000;header.bV5GreenMask=0xff00;header.bV5BlueMask=0xff;header.bV5AlphaMask=0xff000000;memcpy(dib.data(),&header,sizeof(header));dib[sizeof(header)]=200;dib[sizeof(header)+3]=128;put(CF_DIBV5,dib.data(),dib.size());auto decoded=clipboard_image::decode(canvas);Color color;require(decoded&&decoded->GetPixel(0,0,&color)==Ok&&color.GetA()==128,"DIBV5 alpha");pasteCheck("paste DIBV5");}
    {HBITMAP bitmap=nullptr;require(sample.GetHBITMAP(Color::White,&bitmap)==Ok,"HBITMAP fixture");require(OpenClipboard(canvas),"bitmap clipboard");EmptyClipboard();require(SetClipboardData(CF_BITMAP,bitmap)!=nullptr,"publish HBITMAP");CloseClipboard();pasteCheck("paste HBITMAP");}
    // Our native clipboard wins over the image preview it may also contain.
    reset();ObjectId atom;s.doc.addAtom(Vec2(),8,&atom);s.refresh();s.selected=s.all();require(studioio::copy(s,canvas,false).ok(),"native clipboard copy");auto chemical=studioio::paste(s,canvas);require(chemical.ok()&&s.doc.stats().aliveAtoms==2&&s.doc.paths().size()==0,"chemical paste stays editable");
    std::ofstream(dir+"/clipboard-checks.json")<<"{\"passed\":true,\"ctrl_y_redo\":true,\"ctrl_shift_z\":true,\"png\":true,\"dib\":true,\"dib_v5_alpha\":true,\"bitmap\":true,\"image_move_undo_redo\":true,\"native_cdxml_roundtrip\":true,\"transparency\":true,\"chemical_priority\":true}";
    s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}
