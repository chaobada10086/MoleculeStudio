#pragma once
void App::interopRingCheck(const std::string& dir){
    auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);};
    std::ofstream log(dir+"/interop-ring-check.txt");
    std::string bytes,xml,why;require(persistence::readFileBytes(dir+"/actual.cdx",&bytes).ok(),"CDX fixture read");
    require(cdxbinary::toXml(bytes,xml,why),why);Session imported;auto err=persistence::readCdxml(xml,imported.doc);require(err.ok(),"CDX import: "+err.message);imported.refresh();
    std::string reference;require(persistence::readFileBytes(dir+"/reference.cdxml",&reference).ok(),"reference read");Session expected;err=persistence::readCdxml(reference,expected.doc);require(err.ok(),err.message);expected.refresh();
    require(imported.doc.stats().aliveAtoms==expected.doc.stats().aliveAtoms&&imported.doc.stats().aliveBonds==expected.doc.stats().aliveBonds,"CDX and CDXML topology counts");
    for(size_t i=0;i<expected.doc.atoms().size();++i){auto* a=expected.doc.atoms().alive(ObjectId(i));auto* b=imported.doc.atoms().alive(ObjectId(i));if(!a)continue;require(b&&a->element==b->element&&a->semantics.charge==b->semantics.charge&&core::length(a->position-b->position)<.02,"CDX atom identity/position/charge");}
    for(size_t i=0;i<expected.doc.bonds().size();++i){auto* a=expected.doc.bonds().alive(ObjectId(i));auto* b=imported.doc.bonds().alive(ObjectId(i));if(!a)continue;require(b&&a->beginAtom==b->beginAtom&&a->endAtom==b->endAtom&&a->order==b->order&&a->display==b->display,"CDX bond endpoints/order/stereo");}
    require(std::abs(imported.doc.defaultBondLength()-expected.doc.defaultBondLength())<.001&&std::abs(imported.doc.defaultTextSize()-10)<.001,"CDX preserves bond length and font size");
    for(size_t n:{size_t(0),size_t(8),bytes.size()-3}){std::string bad;require(!cdxbinary::toXml(bytes.substr(0,n),bad,why),"reject truncated binary");}
    auto publish=[&](const std::string& b){require(OpenClipboard(canvas)!=FALSE,"open test clipboard");EmptyClipboard();auto e=studioio::put(cdxbinary::format(),b.data(),b.size());CloseClipboard();require(e.ok(),"publish CDX");};
    Session pasted;auto before=pasted.doc.digest();publish(bytes);err=studioio::paste(pasted,canvas);require(err.ok(),"ordinary Ctrl+C format paste: "+err.message);require(pasted.doc.stats().aliveAtoms==imported.doc.stats().aliveAtoms,"binary paste creates editable atoms");require(pasted.undo().ok()&&pasted.doc.digest()==before,"binary paste one undo");
    publish(bytes.substr(0,bytes.size()-3));require(!studioio::paste(pasted,canvas).ok()&&pasted.doc.digest()==before,"malformed paste leaves document unchanged");
    imported.selected=imported.all();require(studioio::copy(imported,canvas,false).ok(),"publish outgoing clipboard");IDataObject* data=nullptr;require(SUCCEEDED(OleGetClipboard(&data))&&data,"outgoing OLE data");
    FORMATETC f={};f.cfFormat=CLIPFORMAT(ole::fmtNativeStorage());f.dwAspect=DVASPECT_CONTENT;f.lindex=-1;f.tymed=TYMED_ISTORAGE;STGMEDIUM m={};HRESULT hr=data->GetData(&f,&m);require(SUCCEEDED(hr)&&m.tymed==TYMED_ISTORAGE,"ChemDraw Native storage offered");auto native=ole::readContents(m.pstg);ReleaseStgMedium(&m);
    FORMATETC plain={CF_UNICODETEXT,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};require(FAILED(data->QueryGetData(&plain)),"structure copy does not advertise XML as plain text");
    IEnumFORMATETC* formats=nullptr;require(SUCCEEDED(data->EnumFormatEtc(DATADIR_GET,&formats)),"enumerate clipboard");FORMATETC entry={};ULONG got=0;bool vectorOffered=false;
    while(formats->Next(1,&entry,&got)==S_OK){require(entry.cfFormat!=CF_UNICODETEXT,"no plain XML in format list");vectorOffered|=entry.cfFormat==CF_ENHMETAFILE;if(entry.ptd)CoTaskMemFree(entry.ptd);}formats->Release();require(vectorOffered,"Office vector fallback offered");
    FORMATETC emf={CF_ENHMETAFILE,nullptr,DVASPECT_CONTENT,-1,TYMED_ENHMF};STGMEDIUM picture={};require(SUCCEEDED(data->GetData(&emf,&picture)),"Office vector picture rendered");ENHMETAHEADER header={};require(GetEnhMetaFileHeader(picture.hEnhMetaFile,sizeof(header),&header)>0&&header.nRecords>5,"nonempty EMF picture");ReleaseStgMedium(&picture);
    f.tymed=TYMED_HGLOBAL;STGMEDIUM chem={};require(SUCCEEDED(data->GetData(&f,&chem)),"ChemDraw Native HGLOBAL offered");void* mem=GlobalLock(chem.hGlobal);require(mem&&GlobalSize(chem.hGlobal)>512&&static_cast<unsigned char*>(mem)[0]==0xd0&&static_cast<unsigned char*>(mem)[1]==0xcf,"Native compound storage bytes");GlobalUnlock(chem.hGlobal);ReleaseStgMedium(&chem);
    data->Release();require(native.compare(0,8,"VjCD0100")==0,"Native storage holds CDX");require(cdxbinary::toXml(native,xml,why),"outgoing CDX decodes");Session outgoing;persistence::writeFileAtomic(dir+"/outgoing-decoded.cdxml",xml);auto outError=persistence::readCdxml(xml,outgoing.doc);require(outError.ok()&&outgoing.doc.stats().aliveAtoms==imported.doc.stats().aliveAtoms&&outgoing.doc.stats().aliveBonds==imported.doc.stats().aliveBonds,"outgoing binary retains all editable atoms and bonds: "+outError.message+" atoms="+std::to_string(outgoing.doc.stats().aliveAtoms)+" expected="+std::to_string(imported.doc.stats().aliveAtoms)+" bonds="+std::to_string(outgoing.doc.stats().aliveBonds)+" expected="+std::to_string(imported.doc.stats().aliveBonds));require(persistence::writeFileAtomic(dir+"/outgoing.cdx",native).ok(),"save outgoing fixture");
    std::string chinese=u8"<CDXML LabelSize=\"10\"><fonttable><font id=\"3\" name=\"Arial\" charset=\"utf-8\"/></fonttable><page id=\"1\"><t id=\"2\" p=\"10 20\"><s font=\"3\" size=\"10\" face=\"1\">中文 H2O</s><s font=\"3\" size=\"8\" face=\"32\">第二行</s></t></page></CDXML>";
    require(cdxbinary::fromXml(chinese,native,why)&&cdxbinary::toXml(native,xml,why),"Unicode and rich-text roundtrip");require(xml.find(u8"中文 H2O")!=std::string::npos&&xml.find(u8"第二行")!=std::string::npos,"Unicode content preserved");log<<"clipboard formats, topology, styles, undo, malformed input, Unicode passed\n";log.flush();
    for(int side=0;side<2;++side)for(double angle:{0.,90.,180.,37.})for(double zoom:{1.,4.}){
        Session ring;ring.doc.setDefaultBondLength(14.4);const double L=14.4,H=L*std::sqrt(3.)/2;std::vector<Vec2> points={Vec2(0,0),Vec2(0,-L),Vec2(-H,L/2),Vec2(-2*H,0),Vec2(-2*H,-L),Vec2(-H,-1.5*L),Vec2(H,L/2),Vec2(2*H,0),Vec2(2*H,-L),Vec2(H,-1.5*L)};
        double a=angle*3.14159265358979323846/180.;for(auto& p:points)p=Vec2(p.x*cos(a)-p.y*sin(a),p.x*sin(a)+p.y*cos(a));std::vector<ObjectId> ids;for(auto p:points){ObjectId id;ring.doc.addAtom(p,6,&id);ids.push_back(id);}
        int edges[][3]={{0,1,1},{0,2,2},{2,3,1},{3,4,2},{4,5,1},{5,1,2},{1,9,1},{9,8,2},{8,7,1},{7,6,2},{6,0,1}};
        for(auto& e:edges){ObjectId id;ring.doc.addBond(ids[e[0]],ids[e[1]],&id);ring.doc.setBondOrder(id,e[2]==2?BondOrder::Double:BondOrder::Single);}ring.refresh();auto v=ring.view();v.scale=zoom;ring.setView(v);ring.activateCommand("chooseRingTool_Benzene");auto digest=ring.doc.digest();Vec2 mid=(points[0]+points[side?6:2])*.5;
        ring.gesture(PointerPhase::Down,mid);ring.gesture(PointerPhase::Move,mid);auto* tool=dynamic_cast<rings_chains::RingTool*>(ring.entries[ring.activeIndex].tool.get());require(tool&&tool->pendingPlacement().valid,"valid bay preview");auto plan=tool->pendingPlacement();require(std::count(plan.reuseAtom.begin(),plan.reuseAtom.end(),kInvalidObjectId)==3,"bay preview reuses three atoms");require(ring.doc.digest()==digest,"bay preview is non-mutating");err=ring.gesture(PointerPhase::Up,mid);require(err.ok(),"bay commit: "+err.message);
        int doubles=0,ch2=0;require(ring.doc.stats().aliveAtoms==13&&ring.doc.stats().aliveBonds==15,"bay creates 13 atoms and 15 bonds");for(const auto& b:ring.doc.bonds().records())if(b.isAlive()){doubles+=b.order==BondOrder::Double;double len=core::length(ring.doc.atoms().get(b.beginAtom)->position-ring.doc.atoms().get(b.endAtom)->position);require(std::abs(len-L)<1e-6,"bay preserves all bond lengths");}
        for(const auto& atom:ring.doc.atoms().records())if(atom.isAlive()){int valence=0;for(auto id:ring.doc.incidentBonds(atom.id))valence+=ring.doc.bonds().get(id)->order==BondOrder::Double?2:1;require(valence<=4,"no five-valent carbon");if(ring.doc.incidentBonds(atom.id).size()==2&&valence==2)++ch2;}
        require(doubles==6&&ch2==0,"3a1H phenalene six perimeter double bonds");for(auto id:ring.doc.incidentBonds(ids[0]))require(ring.doc.bonds().get(id)->order==BondOrder::Single,"shared bridgehead has three single bonds as requested");require(ring.doc.validate().ok(),"bay graph validates");for(size_t i=0;i<10;++i)require(core::length(ring.doc.atoms().get(ids[i])->position-points[i])<1e-8,"old atoms do not move");auto after=ring.doc.digest();require(ring.undo().ok()&&ring.doc.digest()==digest,"bay single undo");require(ring.undo(true).ok()&&ring.doc.digest()==after,"bay redo");
        log<<"bay side="<<side<<" angle="<<angle<<" zoom="<<zoom<<" passed\n";log.flush();if(side==0&&angle==0&&zoom==1)ring.save(dir+"/bay-result.cdxml",true);
    }
    for(int side=0;side<3;++side)for(double angle:{0.,37.,90.,180.})for(double zoom:{1.,4.}) {
        Session ring; const double L=14.4,H=L*std::sqrt(3.)/2; ring.doc.setDefaultBondLength(L);
        const double rad=angle*3.14159265358979323846/180.;
        auto rotate=[&](Vec2 p){return Vec2(p.x*cos(rad)-p.y*sin(rad),p.x*sin(rad)+p.y*cos(rad));};
        for(Vec2 centre:{Vec2(0,0),Vec2(2*H,0),Vec2(4*H,0),Vec2(H,1.5*L)}) {
            std::vector<ObjectId> ids;
            for(Vec2 offset:{Vec2(0,-L),Vec2(H,-L/2),Vec2(H,L/2),Vec2(0,L),Vec2(-H,L/2),Vec2(-H,-L/2)}) {
                Vec2 p=rotate(centre+offset);ObjectId id=kInvalidObjectId;
                for(const auto& a:ring.doc.atoms().records())if(a.isAlive()&&core::length(a.position-p)<1e-6){id=a.id;break;}
                if(id==kInvalidObjectId)ring.doc.addAtom(p,6,&id); ids.push_back(id);
            }
            for(int j=0;j<6;++j)if(ring.doc.bondBetween(ids[j],ids[(j+1)%6])==kInvalidObjectId){ObjectId id;ring.doc.addBond(ids[j],ids[(j+1)%6],&id);if((centre.y==0 && (j==0 || j==2 || (centre.x==0 && j==4))) || (centre.y>0 && j==3))ring.doc.setBondOrder(id,BondOrder::Double);}
        }
        require(ring.doc.stats().aliveAtoms==17&&ring.doc.stats().aliveBonds==20,"four-ring fixture");
        ring.refresh();auto v=ring.view();v.scale=zoom;ring.setView(v);ring.activateCommand("chooseRingTool_Benzene");
        Vec2 mids[]={Vec2(3.5*H,.75*L),Vec2(2.5*H,.75*L),Vec2(2*H,1.5*L)};Vec2 mid=rotate(mids[side]);auto before=ring.doc.digest();
        ring.gesture(PointerPhase::Down,mid);ring.gesture(PointerPhase::Move,mid);
        auto* tool=dynamic_cast<rings_chains::RingTool*>(ring.entries[ring.activeIndex].tool.get());require(tool&&tool->pendingPlacement().valid,"three-edge preview valid");
        auto plan=tool->pendingPlacement();require(plan.reusedAtomCount==4&&plan.reusedBondCount==3&&plan.createdAtomCount==2,"three-edge fusion reuses four atoms and three bonds");
        require(ring.doc.digest()==before,"three-edge preview non-mutating");require(ring.gesture(PointerPhase::Up,mid).ok(),"three-edge commit");
        require(ring.doc.stats().aliveAtoms==19&&ring.doc.stats().aliveBonds==23,"five-ring topology");int doubles=0;
        for(const auto& b:ring.doc.bonds().records())if(b.isAlive()){doubles+=b.order==BondOrder::Double;require(std::abs(core::length(ring.doc.atoms().get(b.beginAtom)->position-ring.doc.atoms().get(b.endAtom)->position)-L)<1e-6,"five-ring bond length");}
        for(const auto& a:ring.doc.atoms().records())if(a.isAlive()){int valence=0;for(auto id:ring.doc.incidentBonds(a.id))valence+=ring.doc.bonds().get(id)->order==BondOrder::Double?2:1;require(valence<=4,"five-ring carbon valence");}
        require(doubles==9&&ring.doc.validate().ok(),"five-ring nine doubles and valid graph");auto after=ring.doc.digest();require(ring.undo().ok()&&ring.doc.digest()==before,"three-edge undo");require(ring.undo(true).ok()&&ring.doc.digest()==after,"three-edge redo");
        log<<"three-edge side="<<side<<" angle="<<angle<<" zoom="<<zoom<<" passed\n";log.flush();
        if(side==0&&angle==0&&zoom==1)ring.save(dir+"/five-ring-result.cdxml",true);
    }
    for(int large=0;large<2;++large)for(int seed=0;seed<2;++seed)for(double angle:{0.,37.})for(double zoom:{1.,4.}) {
        Session ring;const double L=14.4,X=L/2,Y=L*sqrt(3.)/2;ring.doc.setDefaultBondLength(L);
        double rad=angle*3.14159265358979323846/180.;
        auto pos=[&](Vec2 p){p=Vec2(p.x*X,p.y*Y);return Vec2(p.x*cos(rad)-p.y*sin(rad),p.x*sin(rad)+p.y*cos(rad));};
        std::vector<Vec2> centres={Vec2(0,0),Vec2(-3,1),Vec2(-3,3),Vec2(0,2),Vec2(0,4),Vec2(3,1),Vec2(3,3),Vec2(6,2)};
        if(large){centres.clear();for(int k=0;k<15;++k)centres.push_back(Vec2(3*k,k%2));}
        Vec2 offsets[]={Vec2(-1,-1),Vec2(1,-1),Vec2(2,0),Vec2(1,1),Vec2(-1,1),Vec2(-2,0)};
        for(size_t k=0;k<centres.size();++k){
            std::vector<ObjectId> ids;
            for(Vec2 off:offsets){Vec2 p=pos(centres[k]+off);ObjectId id=kInvalidObjectId;
                for(const auto& a:ring.doc.atoms().records())if(a.isAlive()&&core::length(a.position-p)<1e-6){id=a.id;break;}
                if(id==kInvalidObjectId)ring.doc.addAtom(p,6,&id);ids.push_back(id);
            }
            for(int j=0;j<6;++j){ObjectId id=ring.doc.bondBetween(ids[j],ids[(j+1)%6]);if(id==kInvalidObjectId)ring.doc.addBond(ids[j],ids[(j+1)%6],&id);
                bool dbl=large?(k==0&&j==0):((k==0&&(j==5||j==1))||(k==1&&(j==5||j==2))||(k==2&&(j==5||j==3))||(k==3&&(j==1||j==3))||(k==4&&j==3)||(k==5&&j==1)||(k==6&&j==3)||(k==7&&(j==1||j==3)));
                if(seed&&dbl)ring.doc.setBondOrder(id,BondOrder::Double);
            }
        }
        ring.refresh();auto view=ring.view();view.scale=zoom;ring.setView(view);ring.activateCommand("chooseRingTool_Benzene");
        auto before=ring.doc.digest();std::vector<std::pair<ObjectId,Vec2>> old;for(const auto& a:ring.doc.atoms().records())if(a.isAlive())old.push_back({a.id,a.position});
        Vec2 mid=pos(large?Vec2(43.5,.5):Vec2(4.5,.5));
        ring.gesture(PointerPhase::Down,mid);ring.gesture(PointerPhase::Move,mid);require(ring.doc.digest()==before,"aromatic preview non-mutating");
        auto result=ring.gesture(PointerPhase::Up,mid);require(result.ok(),"aromatic completion commit: "+result.message);
        require(ring.doc.stats().aliveAtoms==(large?66:30),"complete skeleton atom count");int doubles=0;
        for(const auto& b:ring.doc.bonds().records())if(b.isAlive()){doubles+=b.order==BondOrder::Double;require(std::abs(core::length(ring.doc.atoms().get(b.beginAtom)->position-ring.doc.atoms().get(b.endAtom)->position)-L)<1e-6,"aromatic bond length");}
        for(const auto& a:ring.doc.atoms().records())if(a.isAlive()){int dbl=0;for(auto id:ring.doc.incidentBonds(a.id))dbl+=ring.doc.bonds().get(id)->order==BondOrder::Double;require(dbl==1,"every carbon has exactly one double bond");}
        require(doubles==(large?33:15),"complete 15 or 33 double bonds");for(const auto& a:old)require(core::length(ring.doc.atoms().get(a.first)->position-a.second)<1e-8,"aromatization retains coordinates");
        auto after=ring.doc.digest();require(ring.undo().ok()&&ring.doc.digest()==before,"aromatic undo");require(ring.undo(true).ok()&&ring.doc.digest()==after,"aromatic redo");
        log<<"complete aromatic carbons="<<(large?66:30)<<" seed="<<seed<<" angle="<<angle<<" zoom="<<zoom<<" passed\n";log.flush();
        if(!large&&seed&&angle==0&&zoom==1)ring.save(dir+"/nine-ring-15-doubles.cdxml",true);
    }
    {
        Session caption;std::string source;
        if(persistence::readFileBytes(dir+"/font.cdxml",&source).ok()){
            require(persistence::readCdxml(source,caption.doc).ok(),"ChemDraw original font fixture import");
            auto check=[&](Session& doc){int found=0;
                for(const auto& t:doc.doc.texts().records())if(t.isAlive()){
                    auto runs=doc.doc.runs(t.id);bool target=false;for(const auto& r:runs)target|=r.utf8=="450";if(!target)continue;
                    ++found;text::RichLayout layout;text::LayoutOptions options;require(text::layoutRichText(runs,Vec2(),options,&doc.glyphs,&layout).ok(),"caption layout");
                    int digits=0;bool sub=false;
                    for(const auto& r:layout.glyphs){
                        double shift=layout.lines[r.lineIndex].baseline-r.y;
                        if(runs[r.runIndex].utf8=="450"){require(std::abs(r.size-10)<1e-8&&std::abs(shift)<1e-8,"450 stays 10pt on baseline");++digits;}
                        sub|=shift<0;
                    }
                    require(digits==3&&sub,"450 preserved and CO2 still subscript");
                }
                require(found==3,"all three wavelength captions verified");
            };check(caption);
            require(caption.save(dir+"/font-roundtrip.cdxml",true).ok(),"caption roundtrip save");
            Session again;std::string saved;require(persistence::readFileBytes(dir+"/font-roundtrip.cdxml",&saved).ok()&&persistence::readCdxml(saved,again.doc).ok(),"caption roundtrip reload");check(again);
            log<<"original ChemDraw three wavelength captions and roundtrip passed\n";
        }
        for(const std::string value:{"450-460","450+460","Fe2+","SO4","13C"}){
            TextRun run;run.utf8=value;run.fontName="Arial";run.size=10;run.flags=kFormula;
            text::RichLayout layout;text::LayoutOptions options;require(text::layoutRichText({run},Vec2(),options,&caption.glyphs,&layout).ok(),"formula regression layout");
            bool raised=false,lowered=false;for(const auto& r:layout.glyphs){double shift=layout.lines[r.lineIndex].baseline-r.y;raised|=shift>0;lowered|=shift<0;}
            if(value.find("450")==0)require(!raised&&!lowered,"numeric range remains normal");
            else if(value=="SO4")require(lowered&&!raised,"formula count remains subscript");
            else require(raised&&!lowered,"charge/isotope remains superscript");
        }
        log<<"numeric range, charge, isotope and subscript regressions passed\n";
    }
    {
        Session full;std::string original;const Document* drawing=&imported.doc;if(persistence::readFileBytes(dir+"/font.cdxml",&original).ok()){require(persistence::readCdxml(original,full.doc).ok(),"full reaction sheet import");drawing=&full.doc;}auto* object=new ole::DocumentObject();std::ostringstream source;require(integration::saveSafeNative(*drawing,source).ok(),"vector native source");
        object->setContents(source.str(),false);SIZE extent={18000,11000};object->setExtentHimetric(extent);int previewBuilds=0;object->setPreview([&previewBuilds](const std::string& bytes,SIZE size){++previewBuilds;return olepreview::fromNative(bytes,size);});
        FORMATETC format={CLIPFORMAT(ole::fmtEmbedSource()),nullptr,DVASPECT_CONTENT,-1,TYMED_ISTORAGE};STGMEDIUM storage={};require(SUCCEEDED(object->GetData(&format,&storage)),"vector embedded storage");
        IStream* stream=nullptr;require(SUCCEEDED(storage.pstg->OpenStream(L"\2OlePres000",nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&stream)),"vector OLE presentation stream");
        STATSTG stat={};require(SUCCEEDED(stream->Stat(&stat,STATFLAG_NONAME))&&stat.cbSize.QuadPart>128,"nonempty stored vector");std::vector<unsigned char> bits(size_t(stat.cbSize.QuadPart));ULONG read=0;require(SUCCEEDED(stream->Read(bits.data(),ULONG(bits.size()),&read))&&read==bits.size(),"read stored vector");stream->Release();
        DWORD type=0,size=0;memcpy(&type,bits.data()+4,4);memcpy(&size,bits.data()+36,4);require(type==CF_ENHMETAFILE&&size==bits.size()-40,"correct vector presentation header");
        IOleCache* cache=nullptr;require(SUCCEEDED(CreateDataCache(nullptr,ole::kDocClsid,IID_IOleCache,reinterpret_cast<void**>(&cache))),"create Windows OLE cache");
        IPersistStorage* persist=nullptr;require(SUCCEEDED(cache->QueryInterface(IID_IPersistStorage,reinterpret_cast<void**>(&persist))),"cache persistence");
        require(SUCCEEDED(persist->Load(storage.pstg)),"Windows OLE cache accepts stored presentation");persist->HandsOffStorage();persist->Release();cache->Release();
        IOleObject* loaded=nullptr;require(SUCCEEDED(OleLoad(storage.pstg,IID_IOleObject,nullptr,reinterpret_cast<void**>(&loaded)))&&loaded,"Office OLE handler loads embedded object");loaded->Release();
        METAFILEPICT mapping={MM_ANISOTROPIC,18000,11000,nullptr};HDC reference=GetDC(nullptr);
        HENHMETAFILE emf=SetWinMetaFileBits(size,bits.data()+40,reference,&mapping);ReleaseDC(nullptr,reference);require(emf!=nullptr,"stored vector wrapper recovers EMF");
        ENHMETAHEADER h={};require(GetEnhMetaFileHeader(emf,sizeof(h),&h)>0&&h.nRecords>5,"stored EMF has drawing records");
        if(drawing==&full.doc)require(GetEnhMetaFileBits(emf,0,nullptr)<1000000,"full reaction sheet uses compact vector outlines");
        struct Records {int raster=0,vector=0,plusPaths=0;} records;
        auto count=[](HDC,HANDLETABLE*,const ENHMETARECORD* r,int,LPARAM p)->int {auto* counts=reinterpret_cast<Records*>(p);if(r->iType==EMR_STRETCHDIBITS||r->iType==EMR_BITBLT||r->iType==EMR_STRETCHBLT||r->iType==EMR_ALPHABLEND)++counts->raster;else ++counts->vector;
            const BYTE* bytes=reinterpret_cast<const BYTE*>(r);
            if(r->iType==EMR_GDICOMMENT&&r->nSize>=28&&memcmp(bytes+12,"EMF+",4)==0){for(DWORD at=16;at+12<=r->nSize;){WORD type=0;DWORD size=0;memcpy(&type,bytes+at,2);memcpy(&size,bytes+at+4,4);if(size<12||size>r->nSize-at)break;if(type==0x4014||type==0x4015)++counts->plusPaths;at+=size;}}
            return 1;};
        require(EnumEnhMetaFile(nullptr,emf,count,&records,nullptr)!=FALSE&&records.plusPaths>0&&records.vector>5,"chemical structure retains smooth EMF+ vector paths alongside legacy fallback");
        DeleteEnhMetaFile(emf);ReleaseStgMedium(&storage);
        for(int pass=0;pass<3;++pass){
            for(auto format:std::vector<FORMATETC>{{CF_ENHMETAFILE,nullptr,DVASPECT_CONTENT,-1,TYMED_ENHMF},{CF_METAFILEPICT,nullptr,DVASPECT_CONTENT,-1,TYMED_MFPICT},{CLIPFORMAT(ole::fmtEmbedSource()),nullptr,DVASPECT_CONTENT,-1,TYMED_ISTORAGE}}){STGMEDIUM medium={};require(SUCCEEDED(object->GetData(&format,&medium)),"repeat Office format request");ReleaseStgMedium(&medium);}
        }
        require(previewBuilds<=2,"Office formats share raw and calibrated preview caches");object->Release();log<<"stored OLE dual vector presentation passed; shared preview builds="<<previewBuilds<<"\n";
    }
    log<<"ALL PASSED\n";s.dirty=false;PostMessageW(h,WM_CLOSE,0,0);
}


