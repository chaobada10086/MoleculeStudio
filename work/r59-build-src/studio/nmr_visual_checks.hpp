#pragma once
static void nmrVisualCheck(const std::string& dir){
    NmrDialog nmr;
    if(!nmr.parse("NMR1H\t1\nformula\tC2H6O\ncoverage\t6\t6\npeak\t3.6500\t2\t2\tq（示意）\tCSP5q\tgood\t3\t1.18\npeak\t1.1800\t3\t1\tt（示意）\tCSP5q\tgood\t2\t3.65\npeak\t2.2000\t1\t3\t—\tCSP5q\tmedium\n"))throw std::runtime_error("parse sample");
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};require(nmr.solventIndex==1&&nmr.protonMHz==400,"default CDCl3 and 400 MHz");auto lines=nmr.spectrumLines();require(lines.size()==8,"quartet triplet and unsimulated line counts");
    require(std::abs(lines[1].area/lines[0].area-3)<1e-8&&std::abs(lines[2].area/lines[0].area-3)<1e-8&&std::abs(lines[3].area/lines[0].area-1)<1e-8,"quartet relative intensity");
    require(std::abs(lines[5].area/lines[4].area-2)<1e-8,"triplet relative intensity");require(std::abs((lines[1].ppm-lines[0].ppm)*400-7)<1e-8,"line spacing Hz");
    nmr.protonMHz=800;lines=nmr.spectrumLines();require(std::abs((lines[1].ppm-lines[0].ppm)*800-7)<1e-8,"frequency changes ppm spacing, not J");nmr.protonMHz=400;
    nmr.peaks[0].partner=3.60;lines=nmr.spectrumLines();require(lines.size()==5,"strong coupling remains unsimulated");nmr.peaks[0].partner=1.18;
    double low,high;nmr.chartRange(true,&low,&high);double originalSpan=high-low;RectF rangeRect(16,136,500,198);nmr.zoomChart(rangeRect,300,2);nmr.chartRange(true,&low,&high);require(std::abs((high-low)*2-originalSpan)<1e-8,"chart zoom changes ppm span");
    double exportLow,exportHigh;nmr.chartRange(false,&exportLow,&exportHigh);require(std::abs(exportHigh-exportLow-originalSpan)<1e-8,"report remains full spectrum");nmr.resetChart();
    nmr.structure=std::make_shared<Session>();ObjectId a,b,bondId;require(nmr.structure->doc.addAtom(Vec2(0,0),6,&a).ok()&&nmr.structure->doc.addAtom(Vec2(30,0),8,&b).ok()&&nmr.structure->doc.addBond(a,b,&bondId).ok(),"structure fixture");nmr.structure->refresh();
    RectF moleculeRect(580,136,400,198);nmr.zoomStructure(moleculeRect,Vec2(780,235),2);require(std::abs(nmr.structureZoom-2)<1e-8,"structure zoom");nmr.structureZoom=1;nmr.structurePan=Vec2();
    // Exercise actual Win32 window messages rather than only the math helpers.
    INITCOMMONCONTROLSEX init={sizeof(init),ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&init);
    WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.style=CS_DBLCLKS;wc.lpfnWndProc=NmrDialog::proc;wc.hInstance=GetModuleHandleW(0);wc.lpszClassName=L"NmrZoomRegressionWindow";RegisterClassExW(&wc);
    HWND h=CreateWindowExW(0,wc.lpszClassName,L"NMR test",WS_OVERLAPPEDWINDOW,0,0,1100,720,nullptr,nullptr,wc.hInstance,&nmr);require(h!=nullptr,"create NMR window");
    require(SendMessageW(GetDlgItem(h,732),CB_GETCURSEL,0,0)==1,"CDCl3 selected by default");wchar_t frequency[40]={};GetWindowTextW(GetDlgItem(h,734),frequency,40);require(wcstod(frequency,nullptr)==400,"400 MHz shown by default");
    require(SendMessageW(h,WM_ERASEBKGND,0,0)==1,"no visible background erase");
    RectF chart,molecule;nmr.previewRects(h,&chart,&molecule);auto wheel=[&](double x,double y){POINT point={LONG(x*nmr.dpi/96),LONG(y*nmr.dpi/96)};ClientToScreen(h,&point);SendMessageW(h,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(point.x,point.y));};
    wheel(chart.X+chart.Width*.55,chart.Y+chart.Height*.5);require(nmr.chartZoom>1,"chart wheel message");wheel(molecule.X+molecule.Width*.5,molecule.Y+molecule.Height*.5);require(nmr.structureZoom>1,"structure wheel message");
    {int x=int((molecule.X+molecule.Width*.5)*nmr.dpi/96),y=int((molecule.Y+molecule.Height*.5)*nmr.dpi/96);SendMessageW(h,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(h,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+40,y+20));SendMessageW(h,WM_LBUTTONUP,0,MAKELPARAM(x+40,y+20));require(core::length(nmr.structurePan)>1,"structure pan message");}
    SendMessageW(h,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(int((molecule.X+molecule.Width*.5)*nmr.dpi/96),int((molecule.Y+molecule.Height*.5)*nmr.dpi/96)));require(nmr.structureZoom==1,"structure double click reset");
    SendMessageW(h,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(int((chart.X+chart.Width*.5)*nmr.dpi/96),int((chart.Y+chart.Height*.5)*nmr.dpi/96)));require(nmr.chartZoom==1&&nmr.chartFocus<0,"chart double click reset");
    // Repaint both interactive previews at two DPI scales. The completed buffer
    // must match a fresh render and be reused across same-size frames.
    auto equalPixels=[&](Bitmap& left,Bitmap& right){
        Rect rect(0,0,left.GetWidth(),left.GetHeight());BitmapData a={},b={};
        require(left.LockBits(&rect,ImageLockModeRead,PixelFormat32bppRGB,&a)==Ok,"lock frame");
        require(right.LockBits(&rect,ImageLockModeRead,PixelFormat32bppRGB,&b)==Ok,"lock reference");bool equal=true;
        for(int y=0;y<rect.Height;++y)if(std::memcmp((char*)a.Scan0+y*a.Stride,(char*)b.Scan0+y*b.Stride,size_t(rect.Width)*4))equal=false;
        left.UnlockBits(&a);right.UnlockBits(&b);return equal;
    };
    HDC dc=GetDC(h);RECT client;GetClientRect(h,&client);Bitmap* retained=nullptr;
    ShowWindow(h,SW_SHOWNOACTIVATE);
    for(UINT scale:{96u,144u}){nmr.dpi=scale;
        for(int frame=0;frame<6;++frame){nmr.chartZoom=1+frame*.25;nmr.chartCenter=3;nmr.structureZoom=1+frame*.1;nmr.structurePan=Vec2(frame*4,-frame*2);
            nmr.paint(h,dc);require(nmr.paintBuffer!=nullptr,"completed offscreen frame");if(retained)require(retained==nmr.paintBuffer.get(),"same-size paint reuses buffer");retained=nmr.paintBuffer.get();
            Bitmap expected(client.right,client.bottom,PixelFormat32bppRGB);{Graphics graphics(&expected);nmr.paintContent(graphics,client.right,client.bottom);}require(equalPixels(*nmr.paintBuffer,expected),"buffer equals complete reference frame");
        }
        nmr.previewRects(h,&chart,&molecule);ValidateRect(h,nullptr);nmr.invalidatePreview(h,1);RECT dirty;require(GetUpdateRect(h,&dirty,FALSE)&&dirty.left==LONG(std::floor(chart.X*scale/96))&&dirty.right==LONG(std::ceil(chart.GetRight()*scale/96)),"chart repaint excludes structure and controls");
        ValidateRect(h,nullptr);nmr.invalidatePreview(h,2);require(GetUpdateRect(h,&dirty,FALSE)&&dirty.left==LONG(std::floor(molecule.X*scale/96))&&dirty.right==LONG(std::ceil(molecule.GetRight()*scale/96)),"structure repaint excludes chart and controls");
    }
    ShowWindow(h,SW_HIDE);ReleaseDC(h,dc);nmr.dpi=96;nmr.structureZoom=1;nmr.structurePan=Vec2();nmr.resetChart();
    SendMessageW(GetDlgItem(h,732),CB_SETCURSEL,0,0);nmr.applyConditions(h);require(nmr.solventIndex==1&&SendMessageW(GetDlgItem(h,732),CB_GETCURSEL,0,0)==1,"unspecified solvent resolves to CDCl3");
    DestroyWindow(h);if(nmr.font){DeleteObject(nmr.font);nmr.font=0;}
    Bitmap image(1400,650,PixelFormat32bppARGB);Graphics g(&image);g.Clear(Color::White);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    nmr.drawChart(g,RectF(20,20,1300,260),false);nmr.chartFocus=0;nmr.drawChart(g,RectF(20,320,1300,260));
    UINT count=0,size=0;GetImageEncodersSize(&count,&size);std::vector<uint8_t> buffer(size);auto codecs=reinterpret_cast<ImageCodecInfo*>(buffer.data());GetImageEncoders(count,size,codecs);bool saved=false;
    for(UINT i=0;i<count;++i)if(codecs[i].MimeType&&wcscmp(codecs[i].MimeType,L"image/png")==0){saved=image.Save(W(dir+"/nmr-chart.png").c_str(),&codecs[i].Clsid,0)==Ok;break;}
    if(!saved)throw std::runtime_error("save chart image");
    require(nmr.writeReport(W(dir+"/nmr-report.png")),"report export");require(nmr.csv().find(L"assumed_coupling_hz")!=std::wstring::npos,"CSV identifies assumed J");
    nmr.carbon=true;nmr.chartFocus=-1;nmr.chartZoom=1;nmr.chartRange(true,&low,&high);originalSpan=high-low;nmr.zoomChart(rangeRect,300,2);nmr.chartRange(true,&low,&high);require(std::abs((high-low)*2-originalSpan)<1e-8,"carbon chart zoom");require(nmr.csv().find(L"assumed_coupling_hz")==std::wstring::npos,"carbon CSV unchanged");
    std::ofstream(dir+"/nmr-chart-check.json")<<"{\"default_cdcl3_400mhz\":true,\"unspecified_cdcl3\":true,\"buffered_preview\":true,\"no_background_erase\":true,\"preview_only_repaint\":true,\"dpi_96_144\":true,\"parsing\":true,\"quartet_triplet_intensities\":true,\"hz_spacing\":true,\"strong_coupling_omitted\":true,\"full_chart\":true,\"focused_chart\":true,\"wheel_zoom\":true,\"structure_pan\":true,\"double_click_reset\":true,\"report_full_range\":true,\"report_export\":true,\"carbon_zoom\":true}";
}

