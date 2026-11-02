{
    if(compatible::vmwareAdapter()){
        bool safeBackend=compatibleRenderer!=nullptr;
#ifdef MOLECULE_STUDIO_DX11
        safeBackend=safeBackend||dx11Renderer!=nullptr;
#endif
        require(safeBackend&&rc==nullptr&&wglGetCurrentContext()==nullptr,"VMware must not create an OpenGL context");
        require(!GetModuleHandleW(L"vm3dgl64.dll"),"VMware OpenGL driver must not be loaded");
    }
    const DWORD handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    compatible::Renderer renderer;std::string why;require(renderer.initialize(&why),"initialize compatible renderer");
    render::Scene scene,overlay;render::View view;view.widthPx=320;view.heightPx=240;view.transform.center=Vec2();view.transform.viewportCenter=Vec2(160,120);view.transform.scale=1;view.transform.dpiScale=1;
    render::Primitive polygon;polygon.kind=render::PrimKind::Polygon;polygon.version=1;polygon.style.fill.enabled=true;polygon.style.fill.rgba=0x000000FF;polygon.style.stroke.enabled=false;polygon.style.fillRule=render::FillRule::EvenOdd;
    for(auto coordinates:{std::vector<Vec2>{Vec2(-50,-50),Vec2(50,-50),Vec2(50,50),Vec2(-50,50)},std::vector<Vec2>{Vec2(-20,-20),Vec2(-20,20),Vec2(20,20),Vec2(20,-20)}}){render::Subpath path;path.points=coordinates;path.closed=true;polygon.subpaths.push_back(path);}polygon.bounds=Box(Vec2(-50,-50),Vec2(50,50));scene.add(polygon);
    render::RenderStats first,cached;require(renderer.draw(scene,&overlay,view,&first).ok(),"render polygon with hole");require(renderer.draw(scene,&overlay,view,&cached).ok()&&cached.meshRebuilds==0&&first.meshRebuilds>0,"compatible renderer retains world meshes");
    std::vector<uint8_t> pixels;require(renderer.readPixels(0,0,320,240,&pixels).ok(),"read compatible pixels");
    auto pixel=[&](int x,int y){return pixels[(size_t(239-y)*320+x)*4];};require(pixel(160,120)>245&&pixel(125,120)<10&&pixel(10,10)>245,"hole stays white and solid contour stays black");
    require(!renderer.readPixels(-1,0,320,240,&pixels).ok(),"reject out of bounds readback");
    // Exercise resizing and cache reuse without creating any GPU surfaces.
    double start=now();for(int i=0;i<160;++i){view.widthPx=320+(i%16)*2;view.heightPx=240+(i%8)*2;require(renderer.draw(scene,&overlay,view,&cached).ok(),"repeated compatible resize");}
    double elapsed=now()-start;renderer.destroy();renderer.destroy();
    require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=handles+2,"compatible resize and destruction do not leak GDI handles");
    std::ofstream(testOut+"/compatible_renderer_tests.json")<<"{\"vmware_opengl_bypassed\":"<<(compatible::vmwareAdapter()?"true":"null")<<",\"mesh_cache\":true,\"holes\":true,\"readback_bounds\":true,\"resize_frames\":160,\"resize_ms\":"<<elapsed<<",\"gdi_handles_released\":true}";
}
