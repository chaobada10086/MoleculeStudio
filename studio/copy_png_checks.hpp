{
    // 右键菜单：复制为 PNG 的位置、标签与空选区禁用状态。
    // The product builds this menu through the same helper, so these assertions
    // describe what a right-click on a box-selected drawing really offers.
    {
        HMENU menu=CreatePopupMenu();
        appendSelectionContextMenu(menu,true,false);
        require(GetMenuItemCount(menu)==9,"selection context menu keeps nine entries");
        require(GetMenuItemID(menu,0)==1,"复制 stays first");
        require(GetMenuItemID(menu,1)==kContextCopyPngId,"copy-as-PNG sits right after 复制");
        require(GetMenuItemID(menu,2)==2&&GetMenuItemID(menu,3)==3,"剪切 and 删除 keep their order");
        require(GetMenuItemID(menu,5)==4&&GetMenuItemID(menu,6)==7&&GetMenuItemID(menu,7)==5&&GetMenuItemID(menu,8)==6,"existing 整理/文字整理/核磁/命名 entries keep their order");
        require((GetMenuState(menu,1,MF_BYPOSITION)&MF_GRAYED)!=0,"copy-as-PNG is greyed without a selection");
        wchar_t label[64]={};
        require(GetMenuStringW(menu,1,label,64,MF_BYPOSITION)>0,"copy-as-PNG menu label exists");
        require(std::wstring(label)==studio_i18n::tr(L"复制为 PNG")&&(studio_i18n::current()!=0||std::wstring(label)==L"复制为 PNG"),"copy-as-PNG menu label is 复制为 PNG");
        DestroyMenu(menu);
        menu=CreatePopupMenu();
        appendSelectionContextMenu(menu,true,true);
        require((GetMenuState(menu,1,MF_BYPOSITION)&MF_GRAYED)==0,"copy-as-PNG is enabled with a selection");
        DestroyMenu(menu);
    }
    auto releaseClipboard=[&](){if(OpenClipboard(canvas)){EmptyClipboard();CloseClipboard();}};
    const UINT pngFormat=RegisterClipboardFormatW(L"PNG");
    require(pngFormat!=0,"PNG clipboard format is registered");
    auto readClipboardImage=[&](std::vector<uint8_t>& png,BITMAPINFOHEADER& dib)->bool{
        png.clear();dib=BITMAPINFOHEADER();
        if(!OpenClipboard(canvas))return false;
        if(HANDLE handle=GetClipboardData(pngFormat)){
            const SIZE_T bytes=GlobalSize(handle);
            const void* data=GlobalLock(handle);
            if(data&&bytes){png.assign(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+bytes);}
            if(data)GlobalUnlock(handle);
        }
        if(HANDLE handle=GetClipboardData(CF_DIB)){
            const BITMAPINFOHEADER* header=static_cast<const BITMAPINFOHEADER*>(GlobalLock(handle));
            if(header){dib=*header;}
            if(header)GlobalUnlock(handle);
        }
        CloseClipboard();
        return !png.empty();
    };
    auto pngDimensions=[](const std::vector<uint8_t>& png){
        auto be32=[&](size_t at){return (uint32_t(png[at])<<24)|(uint32_t(png[at+1])<<16)|(uint32_t(png[at+2])<<8)|uint32_t(png[at+3]);};
        return std::make_pair(be32(16),be32(20));
    };
    releaseClipboard();
    // 空选区：命令守卫拒绝并给出提示，且绝不写剪贴板。
    s.selected.clear();
    execute(Button("ctxCopyPng","复制为 PNG","","@copyPng"));
    require(status=="请先选择要复制的结构","copy-as-PNG without a selection asks the user to select something");
    require(!IsClipboardFormatAvailable(pngFormat)&&!IsClipboardFormatAvailable(CF_DIB),"copy-as-PNG without a selection leaves the clipboard untouched");
    // 苯环：全选后复制为 PNG。
    s.doc=Document();drawingSettings.apply(s.doc);s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
    s.activateCommand("chooseRingTool_Benzene");
    s.gesture(PointerPhase::Down,Vec2(),0);s.gesture(PointerPhase::Up,Vec2(),0);
    s.selected=s.all();s.refresh();
    require(!s.selected.empty(),"benzene selection for copy-as-PNG");
    const Box pngBounds=studioio::selectionBounds(s);
    require(!pngBounds.empty(),"selection bounds for copy-as-PNG");
    const auto pngDigest=s.doc.digest();const size_t pngUndoDepth=s.history.cursor();const auto pngSelection=s.selected;
    execute(Button("ctxCopyPng","复制为 PNG","","@copyPng"));
    require(s.status.find("已复制为 PNG")==0&&status==s.status,"copy-as-PNG reports success in the status bar");
    std::vector<uint8_t> pngBytes;BITMAPINFOHEADER pngDib;
    require(readClipboardImage(pngBytes,pngDib),"copy-as-PNG publishes both clipboard formats");
    require(pngBytes.size()>8&&pngBytes[0]==0x89&&pngBytes[1]==0x50&&pngBytes[2]==0x4E&&pngBytes[3]==0x47,"clipboard PNG starts with the PNG signature");
    require(IsClipboardFormatAvailable(pngFormat)&&IsClipboardFormatAvailable(CF_DIB),"clipboard exposes PNG and CF_DIB");
    const auto pngSize=pngDimensions(pngBytes);
    const uint32_t pngWidth=pngSize.first,pngHeight=pngSize.second;
    require(pngWidth>0&&pngHeight>0,"PNG has a positive decoded size");
    require(pngDib.biWidth==int(pngWidth)&&(pngDib.biHeight<0?-pngDib.biHeight:pngDib.biHeight)==int(pngHeight)&&pngDib.biBitCount==24&&pngDib.biCompression==BI_RGB,"CF_DIB matches the PNG size and is a plain 24-bit DIB");
    require(pngWidth<2000&&pngHeight<2000,"copy-as-PNG renders the selection, not the whole canvas");
    const double expectedRatio=pngBounds.width()/std::max(1e-9,pngBounds.height());
    const double actualRatio=double(pngWidth-20)/double(std::max(1u,pngHeight>20?pngHeight-20:0u));
    require(std::abs(actualRatio-expectedRatio)<=0.15*std::max(1.0,expectedRatio),"PNG aspect ratio follows the selection bounds");
    // Decode the clipboard PNG with the same GDI+ decoder a paste target uses:
    // white paper, actual drawn pixels, and the dimensions the DIB advertises.
    std::unique_ptr<Bitmap> decoded;
    {
        HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE,pngBytes.size());
        require(copy!=nullptr,"clone clipboard PNG bytes");
        void* locked=GlobalLock(copy);require(locked!=nullptr,"lock cloned PNG bytes");
        memcpy(locked,pngBytes.data(),pngBytes.size());GlobalUnlock(copy);
        IStream* stream=nullptr;
        if(SUCCEEDED(CreateStreamOnHGlobal(copy,TRUE,&stream))&&stream){
            Bitmap image(stream);
            if(image.GetLastStatus()==Ok)decoded.reset(image.Clone(0,0,image.GetWidth(),image.GetHeight(),PixelFormat32bppPARGB));
            stream->Release();
        }
    }
    require(decoded&&decoded->GetLastStatus()==Ok,"clipboard PNG decodes");
    require(decoded->GetWidth()==int(pngWidth)&&decoded->GetHeight()==int(pngHeight),"decoded PNG keeps the advertised size");
    Color corner;require(decoded->GetPixel(0,0,&corner)==Ok&&corner.GetR()>245&&corner.GetG()>245&&corner.GetB()>245,"copy-as-PNG has a white background");
    size_t inkPixels=0;
    for(UINT y=0;y<UINT(pngHeight);y+=4)for(UINT x=0;x<UINT(pngWidth);x+=4){Color pixel;if(decoded->GetPixel(int(x),int(y),&pixel)==Ok&&(pixel.GetR()<245||pixel.GetG()<245||pixel.GetB()<245))++inkPixels;}
    require(inkPixels>0,"copy-as-PNG contains the rendered structure");
    {std::ofstream imageFile(testOut+"/copy-as-png-benzene.png",std::ios::binary);imageFile.write(reinterpret_cast<const char*>(pngBytes.data()),std::streamsize(pngBytes.size()));}
    require(s.doc.digest()==pngDigest,"copy-as-PNG does not modify the document");
    require(s.history.cursor()==pngUndoDepth,"copy-as-PNG does not add an undo step");
    require(s.selected==pngSelection,"copy-as-PNG keeps the selection");
    // 重复执行：第二次仍是完整的一对格式，不残留上一张。
    execute(Button("ctxCopyPng","复制为 PNG","","@copyPng"));
    std::vector<uint8_t> repeatedPng;BITMAPINFOHEADER repeatedDib;
    require(readClipboardImage(repeatedPng,repeatedDib),"repeated copy-as-PNG keeps both formats");
    require(repeatedPng.size()>8&&repeatedPng[0]==0x89&&repeatedPng[1]==0x50&&repeatedPng[2]==0x4E&&repeatedPng[3]==0x47,"repeated copy-as-PNG publishes a PNG");
    require(repeatedDib.biWidth==int(pngDimensions(repeatedPng).first)&&(repeatedDib.biHeight<0?-repeatedDib.biHeight:repeatedDib.biHeight)==int(pngDimensions(repeatedPng).second),"repeated copy-as-PNG keeps PNG and DIB in sync");
    require(s.doc.digest()==pngDigest&&s.history.cursor()==pngUndoDepth,"repeated copy-as-PNG still leaves document and undo unchanged");
    // 文字 + 箭头：合成选区也能作为一张图复制。
    s.doc=Document();drawingSettings.apply(s.doc);s.history.clear();s.selected.clear();s.input.bindDocument(s.doc.id());s.refresh();
    s.activateCommand("chooseRingTool_Benzene");
    s.gesture(PointerPhase::Down,Vec2(-45,0),0);s.gesture(PointerPhase::Up,Vec2(-45,0),0);
    ObjectId mixedText=kInvalidObjectId;
    require(s.doc.addText(Vec2(-20,-70),12,"Benzene 中文",&mixedText).ok(),"add caption to mixed selection");
    s.activateCommand("chooseArrowTool_Large_Solid");
    s.gesture(PointerPhase::Down,Vec2(-130,90),0);s.gesture(PointerPhase::Up,Vec2(80,90),0);
    s.refresh();
    require(s.doc.stats().aliveTexts>=1&&s.doc.arrows().size()>=1,"mixed selection has text and an arrow");
    s.selected=s.all();s.refresh();
    bool mixedTextSelected=false,mixedArrowSelected=false;
    for(const auto& ref:studioio::expanded(s.doc,s.selected)){if(ref.kind==ObjectKind::Text)mixedTextSelected=true;if(ref.kind==ObjectKind::Arrow)mixedArrowSelected=true;}
    require(mixedTextSelected&&mixedArrowSelected,"expanded mixed selection contains text and arrow");
    const auto mixedDigest=s.doc.digest();const size_t mixedUndoDepth=s.history.cursor();
    execute(Button("ctxCopyPng","复制为 PNG","","@copyPng"));
    std::vector<uint8_t> mixedPng;BITMAPINFOHEADER mixedDib;
    require(readClipboardImage(mixedPng,mixedDib),"text and arrow selection copies as PNG");
    require(mixedPng.size()>8&&mixedPng[0]==0x89&&mixedPng[1]==0x50&&mixedPng[2]==0x4E&&mixedPng[3]==0x47,"mixed selection publishes a PNG");
    require(pngDimensions(mixedPng).first>0&&pngDimensions(mixedPng).second>0,"mixed selection PNG has content");
    require(mixedDib.biWidth==int(pngDimensions(mixedPng).first)&&(mixedDib.biHeight<0?-mixedDib.biHeight:mixedDib.biHeight)==int(pngDimensions(mixedPng).second),"mixed selection DIB matches the PNG");
    {std::ofstream imageFile(testOut+"/copy-as-png-text-arrow.png",std::ios::binary);imageFile.write(reinterpret_cast<const char*>(mixedPng.data()),std::streamsize(mixedPng.size()));}
    // Orientation: the caption sits above the ring and the arrow below it in the
    // document, so neutral black caption pixels in the top quarter of the image
    // prove the offscreen raster was not vertically mirrored.
    std::unique_ptr<Bitmap> mixedDecoded;
    {
        HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE,mixedPng.size());
        require(copy!=nullptr,"clone mixed PNG bytes");
        void* locked=GlobalLock(copy);require(locked!=nullptr,"lock mixed PNG bytes");
        memcpy(locked,mixedPng.data(),mixedPng.size());GlobalUnlock(copy);
        IStream* stream=nullptr;
        if(SUCCEEDED(CreateStreamOnHGlobal(copy,TRUE,&stream))&&stream){
            Bitmap image(stream);
            if(image.GetLastStatus()==Ok)mixedDecoded.reset(image.Clone(0,0,image.GetWidth(),image.GetHeight(),PixelFormat32bppPARGB));
            stream->Release();
        }
    }
    require(mixedDecoded&&mixedDecoded->GetWidth()==int(pngDimensions(mixedPng).first)&&mixedDecoded->GetHeight()==int(pngDimensions(mixedPng).second),"mixed PNG decodes with the advertised size");
    bool captionOnTop=false;
    const int topRows=std::max(1,int(pngDimensions(mixedPng).second)/4);
    for(int y=0;y<topRows&&!captionOnTop;++y)for(UINT x=0;x<pngDimensions(mixedPng).first;x+=2){
        Color pixel;
        if(mixedDecoded->GetPixel(int(x),y,&pixel)!=Ok)continue;
        const int r=pixel.GetR(),g=pixel.GetG(),b=pixel.GetB();
        if(r<110&&g<110&&b<110&&std::abs(r-b)<28){captionOnTop=true;break;}
    }
    require(captionOnTop,"copy-as-PNG keeps the document orientation (caption above the arrow)");
    require(s.doc.digest()==mixedDigest&&s.history.cursor()==mixedUndoDepth,"mixed copy-as-PNG leaves document and undo unchanged");
    // Never leave this image on the clipboard for later clipboard assertions.
    releaseClipboard();
    require(!IsClipboardFormatAvailable(pngFormat)&&!IsClipboardFormatAvailable(CF_DIB),"copy-as-PNG test releases the clipboard");
}