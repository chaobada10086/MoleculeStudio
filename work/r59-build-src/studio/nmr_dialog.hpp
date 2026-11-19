#include <thread>
#include <atomic>
#ifndef STUDIO_NMR_DIALOG_HPP
#define STUDIO_NMR_DIALOG_HPP
// Native result window. Proton multiplets use explicitly labelled assumptions.
struct NmrDialog {
    struct Peak {double ppm=0,h=0,partner=0;int splitN=-1;std::string atom,mult,reason,label,quality="rough";};
    std::vector<Peak> peaks;std::string formula,coverage,warning,engine="CSP5";bool fluorine=false,carbon=false,mapped=false,annotate=false;HWND owner=0,list=0;HFONT font=0;UINT dpi=96;
    HWND tabs=0,window=0;bool loading=false,embedded=false;int activeTab=0;
    std::function<void()> closePage;
    struct PredictionJob {std::atomic<bool> cancelled{false},finished{false};bool ok=false;int tab=0;std::string result;uint64_t token=0;};
    std::shared_ptr<PredictionJob> predictionJobs[3];bool batchStarted=false;
    std::string failures[3],preparedWarnings[3];bool prepared[3]={false,false,false};
    std::shared_ptr<Session> cachedStructures[3];std::vector<Peak> cachedPeaks[3],cachedOriginalPeaks[3];
    void cancelPredictions(){for(auto& job:predictionJobs){if(job)job->cancelled=true;job.reset();}}
    bool pendingPredictions()const{for(const auto& job:predictionJobs)if(job)return true;return false;}
    std::function<bool(const std::string&,std::string*,const std::atomic<bool>*)> asyncPredictor;
    ~NmrDialog(){cancelPredictions();if(font)DeleteObject(font);}
#ifdef MOLECULE_STUDIO_DX11
    std::unique_ptr<dx11::Renderer> structureRenderer;
#endif
    uint64_t structureFrames=0,chartFrames=0;bool legacyPreview=false;

    int solventIndex=1;double protonMHz=400;bool hzAxis=false;int chartFocus=-1;
    double chartZoom=1,chartCenter=0,structureZoom=1;Vec2 structurePan,panPress,panStart;double panChartCenter=0;int previewDrag=0;
    static const char* solventKey(int i){const char* keys[]={"unknown","cdcl3","dmso-d6","cd3od","acetone-d6","cd2cl2","c6d6","cd3cn","d2o","thf-d8","toluene-d8","dmf-d7"};return keys[std::max(0,std::min(11,i))];}
    static const wchar_t* solventName(int i){const wchar_t* names[]={L"未指定（默认 CDCl₃）",L"CDCl₃",L"DMSO-d₆",L"CD₃OD",L"Acetone-d₆",L"CD₂Cl₂",L"C₆D₆",L"CD₃CN",L"D₂O",L"THF-d₈",L"Toluene-d₈",L"DMF-d₇"};return names[std::max(0,std::min(11,i))];}
    double nucleusMHz()const{return protonMHz*(fluorine?.94094:carbon?.25145:1.);}
    std::string conditions()const{char text[180];snprintf(text,sizeof(text),"溶剂 %s · 仪器 ¹H %.2f MHz · 当前核约 %.2f MHz",U(solventName(solventIndex)).c_str(),protonMHz,nucleusMHz());return text;}
    void applyConditions(HWND h){
        wchar_t text[80]={};studio_i18n::getWindowText(GetDlgItem(h,734),text,80);wchar_t* end=0;double value=wcstod(text,&end);while(end&&iswspace(*end))++end;
        if(end==text||!end||*end||!std::isfinite(value)||value<40||value>1500){studio_i18n::setWindowText(GetDlgItem(h,737),L"请输入 40–1500 MHz 的仪器氢频率，再点应用条件。");SetFocus(GetDlgItem(h,734));return;}
        int selection=std::max(1,int(studio_i18n::sendMessage(GetDlgItem(h,732),CB_GETCURSEL,0,0)));studio_i18n::sendMessage(GetDlgItem(h,732),CB_SETCURSEL,selection,0);bool changed=selection!=solventIndex;solventIndex=selection;protonMHz=value;
        studio_i18n::setWindowText(GetDlgItem(h,737),L"鼠标移到谱图或结构上：滚轮缩放，拖动平移，双击复位。");
        if(changed){cancelPredictions();batchStarted=false;for(int i=0;i<3;++i){completed[i]=false;cached[i].clear();failures[i].clear();prepared[i]=false;cachedStructures[i].reset();cachedPeaks[i].clear();cachedOriginalPeaks[i].clear();preparedWarnings[i].clear();}loadTab(h,activeTab);}else refreshResults(h);
    }
    std::string source,errorMessage,reportStructure;std::string cached[3];bool completed[3]={false,false,false};
    std::function<bool(HWND,const std::string&,std::string*)> predictor;
    std::string nucleus()const{return fluorine?"¹⁹F":carbon?"¹³C":"¹H";}
    bool nonProton()const{return carbon||fluorine;}
    bool simulatedSplit(const Peak& p)const{return !nonProton()&&p.splitN>=0&&p.splitN<=12&&(p.splitN==0||std::abs(p.ppm-p.partner)*protonMHz>=70);}
    std::string displayedMultiplicity(const Peak& p)const{return p.splitN>0&&!simulatedSplit(p)?"—（可能强耦合）":p.mult;}
    struct SpectrumLine {double ppm,area;};
    std::vector<SpectrumLine> spectrumLines()const{
        std::vector<SpectrumLine> lines;
        for(const auto& p:peaks){int n=simulatedSplit(p)?p.splitN:0;double coefficient=1,denominator=std::pow(2.,n);for(int k=0;k<=n;++k){lines.push_back({p.ppm+(k-n*.5)*7./protonMHz,p.h*coefficient/denominator});if(k<n)coefficient=coefficient*(n-k)/(k+1);}}
        return lines;
    }
    void previewRects(HWND h,RectF* chart,RectF* molecule)const{
        RECT client;GetClientRect(h,&client);float width=float(client.right)*96/dpi,height=float(client.bottom)*96/dpi;
        float plotHeight=std::min(198.f,std::max(100.f,height-438));
        *chart=RectF(16,136,width*.55f-20,plotHeight);*molecule=RectF(width*.55f,136,width*.45f-16,plotHeight);
    }
    std::unique_ptr<Bitmap> paintBuffer;
    void invalidatePreview(HWND h,int area){
        RectF chart,molecule;previewRects(h,&chart,&molecule);auto r=area==1?chart:molecule;
        const double scale=double(dpi)/96;
        RECT dirty={LONG(std::floor(r.X*scale)),LONG(std::floor(r.Y*scale)),LONG(std::ceil(r.GetRight()*scale)),LONG(std::ceil(r.GetBottom()*scale))};
        InvalidateRect(h,&dirty,FALSE);
    }
    static bool insidePreview(RectF rect,Vec2 p){return p.x>=rect.X&&p.x<=rect.GetRight()&&p.y>=rect.Y&&p.y<=rect.GetBottom();}
    void baseChartRange(bool useFocus,double* low,double* high)const{
        *high=fluorine?50:carbon?220:12;*low=fluorine?-250:0;
        for(const auto& p:peaks){*high=std::max(*high,std::ceil(p.ppm+1));*low=std::min(*low,std::floor(p.ppm-1));}
        if(useFocus&&chartFocus>=0&&size_t(chartFocus)<peaks.size()){
            const auto& p=peaks[size_t(chartFocus)];double radius=std::max(.15,simulatedSplit(p)?(p.splitN+1)*7.0/protonMHz*.65+.03:.15);
            *low=p.ppm-radius;*high=p.ppm+radius;
        }
    }
    void chartRange(bool interactive,double* low,double* high)const{
        baseChartRange(interactive,low,high);if(interactive&&chartZoom>1){double span=(*high-*low)/chartZoom;double center=std::max(*low+span*.5,std::min(*high-span*.5,chartCenter));*low=center-span*.5;*high=center+span*.5;}
    }
    void resetChart(){chartZoom=1;chartCenter=0;chartFocus=-1;}
    void zoomChart(RectF rect,double pointX,double amount){
        double low,high;chartRange(true,&low,&high);double baseLow,baseHigh;baseChartRange(true,&baseLow,&baseHigh);
        double fraction=std::max(0.,std::min(1.,(pointX-(rect.X+42))/std::max(1.f,rect.Width-62)));
        double anchor=high-fraction*(high-low),next=std::max(1.,std::min(4000.,chartZoom*amount));
        double span=(baseHigh-baseLow)/next;chartCenter=std::max(baseLow+span*.5,std::min(baseHigh-span*.5,anchor-(.5-fraction)*span));chartZoom=next;
    }
    void zoomStructure(RectF rect,Vec2 point,double amount){
        double next=std::max(.35,std::min(20.,structureZoom*amount)),ratio=next/structureZoom;
        Vec2 middle(rect.X+rect.Width*.5,rect.Y+22+(rect.Height-22)*.5);Vec2 anchor=point-middle;
        structurePan=anchor+(structurePan-anchor)*ratio;structureZoom=next;
    }
    std::shared_ptr<Session> structure;
    std::vector<Peak> originalPeaks;
    bool prepareStructure(const std::string& source){
        if(structure)return true;originalPeaks=peaks;auto fallback=[&](){if(reportStructure.empty()||source==reportStructure)return false;bool ok=prepareStructure(reportStructure);if(ok)warning="已展开缩写并重新布局预测结构；源文件不变。\r\n"+warning;return ok;};if(!mapped||source.empty())return fallback();
        auto candidate=std::make_shared<Session>();if(!persistence::readCdxml(source,candidate->doc).ok())return fallback();
        std::map<int,ObjectId> ids;std::map<int,int> numbers;int number=0;
        double factor=std::max(1.,32./std::max(.1,candidate->doc.defaultBondLength()));
        for(const auto& a:candidate->doc.atoms().records())if(a.isAlive()){ids[a.sourceId]=a.id;numbers[a.sourceId]=++number;}
        for(auto entry:ids){auto a=candidate->doc.atoms().get(entry.second);candidate->doc.setAtomPosition(entry.second,a->position*factor);auto& bag=candidate->doc.extensions(ObjectRef(ObjectKind::Atom,entry.second));bag.erase("studio.atom.number");if(auto raw=bag.findValue("studio.atom.tags")){tinyxml2::XMLDocument tags;if(tags.Parse(raw->c_str())==tinyxml2::XML_SUCCESS&&tags.RootElement()){auto root=tags.RootElement();for(auto tag=root->FirstChildElement("tag");tag;){auto next=tag->NextSiblingElement("tag");auto name=tag->Attribute("name");if(name&&std::string(name).find("MoleculeStudio.NMR.")==0)root->DeleteChild(tag);tag=next;}tinyxml2::XMLPrinter printer;tags.Print(&printer);bag.set("studio.atom.tags",printer.CStr());}}}
        candidate->doc.setDefaultBondLength(candidate->doc.defaultBondLength()*factor);
        std::vector<Peak> labels;auto numbered=peaks;
        for(size_t i=0;i<numbered.size();++i){std::istringstream in(numbered[i].atom);std::string token,shown;while(std::getline(in,token,',')){int fileId=atoi(token.c_str());if(!ids.count(fileId))return fallback();if(!shown.empty())shown+=",";shown+=std::to_string(numbers[fileId]);Peak label=peaks[i];label.atom=token;char shift[40];snprintf(shift,sizeof(shift),"%.2f",label.ppm);label.label="#"+std::to_string(numbers[fileId])+": "+shift;labels.push_back(label);}numbered[i].atom=shown;}
        applyAnnotations(candidate->doc,labels,ids,carbon,fluorine);candidate->refresh();peaks=numbered;structure=candidate;if(!nonProton())warning="氢谱序号指氢所连的重原子；同一原子可有多个位移。\r\n"+warning;return true;
    }
    int px(int n)const{return MulDiv(n,int(dpi),96);}
    void spaceCombo(HWND combo){HDC dc=GetDC(combo);auto old=SelectObject(dc,font);TEXTMETRICW metrics={};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(combo,dc);int height=std::max(px(26),int(metrics.tmHeight+metrics.tmExternalLeading)+px(8));studio_i18n::sendMessage(combo,CB_SETITEMHEIGHT,0,height);studio_i18n::sendMessage(combo,CB_SETITEMHEIGHT,WPARAM(-1),height);studio_i18n::sendMessage(combo,CB_SETMINVISIBLE,10,0);}
    static std::vector<std::string> split(const std::string& line){std::vector<std::string> v;std::istringstream in(line);std::string cell;while(std::getline(in,cell,'\t'))v.push_back(cell);return v;}
    bool parse(const std::string& text){std::istringstream in(text);std::string line;if(!std::getline(in,line))return false;if(!line.empty()&&line.back()=='\r')line.pop_back();carbon=line=="NMR13C\t1";fluorine=line=="NMR19F\t1";if(!carbon&&!fluorine&&line!="NMR1H\t1")return false;
        while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();auto c=split(line);if(c.empty())continue;if(c[0]=="formula"&&c.size()>1)formula=c[1];
            else if(c[0]=="engine"&&c.size()>1)engine=c[1];
            else if(c[0]=="mapping"&&c.size()>1)mapped=c[1]=="1";
            else if(c[0]=="structure"&&c.size()>1)reportStructure=c[1];
            else if(c[0]=="solvent"&&c.size()>1){int index=0;for(;index<12;++index)if(c[1]==solventKey(index))break;if(index==12)return false;solventIndex=std::max(1,index);}
            else if(c[0]=="coverage"&&c.size()>2)coverage=c[1]+"/"+c[2];
            else if(c[0]=="warning"&&c.size()>1){if(!warning.empty())warning+="\r\n";warning+=c[1];}
            else if(c[0]=="peak"&&c.size()>=6){Peak p;p.ppm=atof(c[1].c_str());p.h=atof(c[2].c_str());p.atom=c[3];p.mult=c[4];p.reason=c[5];if(c.size()>6)p.quality=c[6];if(c.size()>8){p.splitN=atoi(c[7].c_str());p.partner=atof(c[8].c_str());if(p.splitN<0||p.splitN>12||!std::isfinite(p.partner))return false;}if(!std::isfinite(p.ppm)||!std::isfinite(p.h)||p.h<=0)return false;peaks.push_back(p);}}
        return !peaks.empty();}
    void layout(HWND h){RECT r;GetClientRect(h,&r);int w=r.right,ht=r.bottom;
        if(embedded){UINT native=GetDpiForWindow(h);if(!native)native=96;UINT fitted=UINT(std::max(72,std::min(MulDiv(w,96,850),MulDiv(ht,96,610))));UINT next=std::min(native,fitted);if(next!=dpi){dpi=next;HFONT previous=font;font=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");for(HWND child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))studio_i18n::sendMessage(child,WM_SETFONT,WPARAM(font),TRUE);if(previous)DeleteObject(previous);}}
        int m=px(16);MoveWindow(tabs,m,px(6),w-2*m,px(30),TRUE);MoveWindow(GetDlgItem(h,720),m,px(101),w-2*m,px(24),TRUE);MoveWindow(GetDlgItem(h,721),m,ht-px(188),w-2*m,px(72),TRUE);
        MoveWindow(GetDlgItem(h,731),m,px(42),px(34),px(24),TRUE);MoveWindow(GetDlgItem(h,732),m+px(38),px(39),px(140),px(340),TRUE);
        MoveWindow(GetDlgItem(h,733),m+px(194),px(42),px(128),px(24),TRUE);MoveWindow(GetDlgItem(h,734),m+px(324),px(39),px(90),px(300),TRUE);
        MoveWindow(GetDlgItem(h,735),m+px(430),px(39),px(100),px(26),TRUE);MoveWindow(GetDlgItem(h,736),m+px(548),px(39),px(110),px(26),TRUE);MoveWindow(GetDlgItem(h,738),m+px(670),px(39),px(105),px(26),TRUE);MoveWindow(GetDlgItem(h,737),m,px(72),w-2*m,px(24),TRUE);
        int listTop=px(150)+std::min(px(198),std::max(px(100),ht-px(438)));MoveWindow(list,m,listTop,w-2*m,std::max(px(70),ht-px(204)-listTop),TRUE);MoveWindow(GetDlgItem(h,722),m,ht-px(42),px(120),px(28),TRUE);MoveWindow(GetDlgItem(h,723),m+px(130),ht-px(42),px(120),px(28),TRUE);MoveWindow(GetDlgItem(h,725),m+px(260),ht-px(42),px(140),px(28),TRUE);MoveWindow(GetDlgItem(h,726),m+px(410),ht-px(42),px(120),px(28),TRUE);MoveWindow(GetDlgItem(h,IDCANCEL),w-m-px(90),ht-px(42),px(90),px(28),TRUE);InvalidateRect(h,0,TRUE);}
    void drawChart(Graphics& g,RectF rect,bool allowFocus=true){++chartFrames;
        float left=rect.X+42,right=rect.GetRight()-20,top=rect.Y+16,bottom=rect.GetBottom()-32;
        double high,low;chartRange(allowFocus,&low,&high);double maxH=1;for(auto& p:peaks)maxH=std::max(maxH,p.h);auto x=[&](double shift){return float(left+(high-shift)/(high-low)*(right-left));};
        if(!nonProton()){
            bool focus=allowFocus&&chartFocus>=0&&size_t(chartFocus)<peaks.size();
            Pen grid(Color(235,239,240),1),axis(Color(128,140,143),1),trace(Color(17,115,106),1.35f);
            Font tick(L"Arial",10,FontStyleRegular,UnitPixel),note(L"Microsoft YaHei UI",10,FontStyleRegular,UnitPixel);SolidBrush ink(Color(74,88,90)),muted(Color(105,120,121));
            double range=high-low,raw=range/6,mag=std::pow(10.,std::floor(std::log10(raw))),ratio=raw/mag;
            double step=(ratio<=1?1:ratio<=2?2:ratio<=5?5:10)*mag;
            for(double value=std::ceil((low-1e-10)/step)*step;value<=high+step*1e-8;value+=step){
                float at=x(value);g.DrawLine(&grid,at,top,at,bottom);wchar_t label[48];double shown=hzAxis?value*nucleusMHz():value;if(std::abs(shown)<1e-9)shown=0;swprintf(label,48,hzAxis?L"%.0f":step<.01?L"%.3f":step<1?L"%.2f":L"%.0f",shown);studio_i18n::drawString(g,label,-1,&tick,PointF(at-10,bottom+4),&ink);
            }
            g.DrawLine(&axis,left,bottom,right,bottom);studio_i18n::drawString(g,hzAxis?L"Δν / Hz":L"δ / ppm",-1,&tick,PointF(right-42,bottom+18),&ink);
            studio_i18n::drawString(g,!allowFocus?L"一阶裂分示意 · 假定 J = 7 Hz":focus?L"局部放大 · 假定 J = 7 Hz，线宽示意":L"一阶裂分示意 · 假定 J = 7 Hz · 选中峰表放大",-1,&note,PointF(left,rect.Y),&muted);
            // A minimum display width avoids losing a narrow line between screen
            // pixels. Focus mode uses a 1 Hz Lorentzian, not a predicted linewidth.
            const int samples=std::max(2,int((right-left)*3));double halfWidth=std::max(.5/nucleusMHz(),range/std::max(1.f,right-left)*.65);
            auto lines=spectrumLines();
            std::vector<double> intensities(size_t(samples)+1,0);double maximum=0;
            for(int i=0;i<=samples;++i){double ppm=high-range*i/samples,value=0;for(const auto& line:lines){double q=(ppm-line.ppm)/halfWidth;value+=line.area/(1+q*q);}intensities[size_t(i)]=value;maximum=std::max(maximum,value);}
            if(maximum>0){std::vector<PointF> points;points.reserve(size_t(samples)+1);for(int i=0;i<=samples;++i)points.push_back(PointF(left+(right-left)*i/samples,bottom-float((bottom-top-7)*intensities[size_t(i)]/maximum)));auto saved=g.Save();g.SetClip(RectF(left,top,right-left,bottom-top+1));g.DrawLines(&trace,points.data(),INT(points.size()));g.Restore(saved);}
            return;
        }
        Pen grid(Color(235,238,240),1),axis(Color(100,110,115),1),peakPen(Color(17,127,115),1.6f);Font tick(L"Arial",11,FontStyleRegular,UnitPixel);SolidBrush ink(Color(55,65,70));
        double raw=(high-low)/6,mag=std::pow(10.,std::floor(std::log10(raw))),ratio=raw/mag,step=(ratio<=1?1:ratio<=2?2:ratio<=5?5:10)*mag;
        for(double ppm=std::ceil((low-1e-10)/step)*step;ppm<=high+step*1e-8;ppm+=step){float at=x(ppm);g.DrawLine(&grid,at,top,at,bottom);wchar_t label[40];double shown=hzAxis?ppm*nucleusMHz():ppm;if(std::abs(shown)<1e-9)shown=0;swprintf(label,40,hzAxis?L"%.0f":step<.01?L"%.3f":step<1?L"%.2f":L"%.0f",shown);studio_i18n::drawString(g,label,-1,&tick,PointF(at-10,bottom+4),&ink);}g.DrawLine(&axis,left,bottom,right,bottom);studio_i18n::drawString(g,hzAxis?L"Δν / Hz":L"δ / ppm",-1,&tick,PointF(right-42,bottom+18),&ink);
        auto clip=g.Save();g.SetClip(RectF(left,top,right-left,bottom-top+1));for(auto& p:peaks)if(p.ppm>=low&&p.ppm<=high){float at=x(p.ppm),height=float((bottom-top-14)*p.h/maxH);g.DrawLine(&peakPen,at,bottom,at,bottom-height);}g.Restore(clip);
    }
    void drawStructure(Graphics& g,RectF rect,bool interactive=true){++structureFrames;
        SolidBrush ink(Color(55,65,70));Font title(L"Microsoft YaHei UI",11,FontStyleRegular,UnitPixel);studio_i18n::drawString(g,L"原子标注：#序号：δ (ppm)，引线指向原子",-1,&title,PointF(rect.X+6,rect.Y),&ink);
        if(!structure){studio_i18n::drawString(g,L"无法建立原子对应关系，结构标注不可用",-1,&title,PointF(rect.X+6,rect.Y+30),&ink);return;}
        auto bounds=structure->scene.bounds();if(bounds.empty())return;
        int width=std::max(1,int(rect.Width)),height=std::max(1,int(rect.Height-22));r62::render::View view;view.widthPx=width;view.heightPx=height;view.defaultStrokeWidth=structure->doc.defaultLineWidth();view.transform.center=(bounds.min+bounds.max)*.5;view.transform.scale=std::min((width-20)/std::max(1.,bounds.width()),(height-18)/std::max(1.,bounds.height()))*(interactive?structureZoom:1);view.transform.dpiScale=1;view.transform.viewportCenter=Vec2(width*.5,height*.5)+(interactive?structurePan:Vec2());
#ifdef MOLECULE_STUDIO_DX11
        if(interactive&&!legacyPreview){
            if(!structureRenderer){auto candidate=std::unique_ptr<dx11::Renderer>(new dx11::Renderer());std::string why;if(candidate->initialize(&why))structureRenderer=std::move(candidate);}
            if(structureRenderer){
                double ratio=double(dpi)/96;view.widthPx=std::max(1,int(rect.Width*ratio));view.heightPx=std::max(1,int((rect.Height-22)*ratio));
                view.transform.dpiScale=ratio;view.transform.viewportCenter=(Vec2(width*.5,height*.5)+structurePan)*ratio;
                render::RenderStats stats;
                if(structureRenderer->draw(structure->scene,nullptr,view,&stats).ok()){
                    auto state=g.Save();g.SetClip(RectF(rect.X,rect.Y+22,rect.Width,rect.Height-22));
                    bool shown=structureRenderer->composite(g,RectF(rect.X,rect.Y+22,rect.Width,rect.Height-22));g.Restore(state);if(shown)return;
                }
            }
        }
#endif
        auto scene=structure->scene;for(auto& primitive:scene.mutablePrimitives())primitive.source=ObjectRef();r62::render::Batcher batcher;r62::render::BatchStats stats;r62::render::Batch batch;batcher.sync(scene,view,structure->measurer.get(),&stats);batcher.buildBatch(scene,view,false,&batch,&stats);
        auto saved=g.Save();g.SetClip(RectF(rect.X,rect.Y+22,rect.Width,rect.Height-22));g.TranslateTransform(rect.X,rect.Y+22);olepreview::fillBatch(g,batch,view.transform.scale*view.defaultStrokeWidth);g.Restore(saved);
    }
    void drawConfidence(Graphics& g,float width,float y){
        Font font(L"Microsoft YaHei UI",11,FontStyleRegular,UnitPixel),note(L"Microsoft YaHei UI",10,FontStyleRegular,UnitPixel);
        std::wstring limits[3];if(fluorine){limits[0]=L"蓝色 较高：σ ≤ 5 ppm";limits[1]=L"紫色 中等：5 < σ ≤ 15 ppm";limits[2]=L"红色 较低：σ > 15 ppm";}
        else if(carbon){limits[0]=L"蓝色 较高：σ ≤ 3 ppm";limits[1]=L"紫色 中等：3 < σ ≤ 8 ppm";limits[2]=L"红色 较低：σ > 8 ppm";}
        else{limits[0]=L"蓝色 较高：σ ≤ 0.25 ppm";limits[1]=L"紫色 中等：0.25 < σ ≤ 0.6 ppm";limits[2]=L"红色 较低：σ > 0.6 ppm";}
        const Color colors[]={Color(0,0,255),Color(204,0,204),Color(221,34,34)};for(int i=0;i<3;++i){SolidBrush brush(colors[i]);studio_i18n::drawString(g,limits[i].c_str(),-1,&font,PointF(16+i*(width-32)/3,y),&brush);}
        SolidBrush ink(Color(70,80,85));studio_i18n::drawString(g,L"σ 是模型分位区间估计的不确定度；颜色不是实验准确率。罕见结构或环境可能超出模型适用范围。",-1,&note,RectF(16,y+22,width-32,34),0,&ink);
    }
    void paintContent(Graphics& g,int pixelWidth,int pixelHeight,const RECT* dirty=nullptr){
        if(dirty)g.SetClip(Rect(dirty->left,dirty->top,dirty->right-dirty->left,dirty->bottom-dirty->top));SolidBrush background(Color::White);g.FillRectangle(&background,0,0,pixelWidth,pixelHeight);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);auto saved=g.Save();g.ScaleTransform(float(dpi)/96,float(dpi)/96);float width=float(pixelWidth)*96/dpi,height=float(pixelHeight)*96/dpi;
        auto intersects=[&](RectF r){return !dirty||(r.X*dpi/96<dirty->right&&r.GetRight()*dpi/96>dirty->left&&r.Y*dpi/96<dirty->bottom&&r.GetBottom()*dpi/96>dirty->top);};
        if(!peaks.empty()){float plotHeight=std::min(198.f,std::max(100.f,height-438));RectF chart(16,136,width*.55f-20,plotHeight),molecule(width*.55f,136,width*.45f-16,plotHeight);if(intersects(chart))drawChart(g,chart);if(intersects(molecule))drawStructure(g,molecule);}
        else{Font font(L"Microsoft YaHei UI",14,FontStyleRegular,UnitPixel);SolidBrush ink(Color(70,80,85));auto message=W(loading?"正在进行本地核磁预测，请稍候…":errorMessage.empty()?"请选择谱种开始预测":errorMessage);studio_i18n::drawString(g,message.c_str(),-1,&font,RectF(35,160,width-70,100),0,&ink);}
        if(intersects(RectF(0,height-108,width,108)))drawConfidence(g,width,height-108);g.Restore(saved);
    }
    void paint(HWND h,HDC dc,const RECT* dirty=nullptr){
        RECT client;GetClientRect(h,&client);const int width=client.right,height=client.bottom;if(width<=0||height<=0)return;
        if(!paintBuffer||paintBuffer->GetWidth()!=UINT(width)||paintBuffer->GetHeight()!=UINT(height)){
            paintBuffer.reset(new Bitmap(width,height,PixelFormat32bppRGB));dirty=nullptr;}
        // Clear and render offscreen, then present the complete frame once. The
        // destination DC clips this copy to the invalid preview and excludes the
        // native controls, so neither a white clear nor partial geometry is visible.
        {Graphics frame(paintBuffer.get());paintContent(frame,width,height,dirty);}
        Graphics target(dc);target.DrawImage(paintBuffer.get(),0,0,width,height);
    }
    void refreshResults(HWND h){
        refreshPredictionStatus(h);studio_i18n::setWindowText(GetDlgItem(h,721),W(warning).c_str());studio_i18n::setWindowText(GetDlgItem(h,737),W(conditions()+"；谱图 / 结构：滚轮缩放、拖动平移、双击复位").c_str());
        ListView_DeleteAllItems(list);LVCOLUMNW column={};column.mask=LVCF_TEXT;column.pszText=const_cast<wchar_t*>(fluorine?L"等价 F 数":carbon?L"等价 C 数":L"积分 (H)");studio_i18n::sendMessage(list,LVM_SETCOLUMNW,1,(LPARAM)&column);
        for(size_t i=0;i<peaks.size();++i){auto& p=peaks[i];wchar_t ppm[32],amount[32];swprintf(ppm,32,L"%.3f",p.ppm);swprintf(amount,32,L"%.2f",p.h);wchar_t hz[40];swprintf(hz,40,L"%.2f",p.ppm*nucleusMHz());std::wstring cells[]={ppm,amount,W(p.atom),W(displayedMultiplicity(p)),W(p.reason),hz};LVITEMW item={};item.mask=LVIF_TEXT;item.iItem=int(i);item.pszText=&cells[0][0];studio_i18n::sendMessage(list,LVM_INSERTITEMW,0,(LPARAM)&item);for(int col=1;col<6;++col){item.iSubItem=col;item.pszText=&cells[col][0];studio_i18n::sendMessage(list,LVM_SETITEMTEXTW,i,(LPARAM)&item);}}
        for(int id:{732,734,735})EnableWindow(GetDlgItem(h,id),TRUE);
        for(int id:{722,723,725,726})EnableWindow(GetDlgItem(h,id),!peaks.empty()&&!loading);InvalidateRect(h,0,TRUE);
    }
    static const char* predictionAction(int index){const char* actions[]={"nmr1h","nmr13c","nmr19f"};return actions[index];}
    static const char* predictionNucleus(int index){const char* names[]={"1H","13C","19F"};return names[index];}
    static bool absentNucleus(const std::string& error){return error=="当前结构不含可预测的氢原子"||error=="当前结构不含碳原子"||error=="当前结构不含氟原子，无需预测氟谱";}
    void refreshPredictionStatus(HWND h){
        int finished=0;const wchar_t* names[]={L"¹H 氢谱",L"¹³C 碳谱",L"¹⁹F 氟谱"};
        for(int i=0;i<3;++i){std::wstring label=names[i];if(batchStarted){if(predictionJobs[i])label+=L" · 计算中";else if(completed[i]){label+=L" · 已完成";++finished;}else if(!failures[i].empty()){label+=absentNucleus(failures[i])?L" · 不适用":L" · 未完成";++finished;}}TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(label.c_str());studio_i18n::sendMessage(tabs,TCM_SETITEMW,i,LPARAM(&item));}
        std::string progress=batchStarted?"   ·   后台处理 "+std::to_string(finished)+"/3":"";
        auto heading=W(nucleus()+" NMR 预测   ·   "+formula+"   ·   覆盖 "+coverage+"   ·   "+engine+"   ·   "+std::to_string(int(protonMHz))+" MHz (¹H)"+progress);studio_i18n::setWindowText(GetDlgItem(h,720),heading.c_str());
    }
    void startPredictions(HWND h){
        if(batchStarted||!asyncPredictor)return;batchStarted=true;
        for(int i=0;i<3;++i){
            if(completed[i]||!failures[i].empty())continue;
            auto job=std::make_shared<PredictionJob>();static std::atomic<uint64_t> next{1};job->token=next++;job->tab=i;predictionJobs[i]=job;
            std::string action=std::string(predictionAction(i))+";solvent="+solventKey(std::max(1,solventIndex));auto calculate=asyncPredictor;
            try{std::thread([job,calculate,action,h](){try{if(!job->cancelled)job->ok=calculate(action,&job->result,&job->cancelled);}catch(const std::exception& e){job->result=e.what();job->ok=false;}catch(...){job->result="核磁预测未完成";job->ok=false;}job->finished=true;if(!job->cancelled)PostMessageW(h,WM_APP+94,WPARAM(job->token),0);}).detach();}
            catch(const std::exception& e){failures[i]=std::string("无法启动核磁后台任务：")+e.what();predictionJobs[i].reset();}
        }
    }
    void finishPrediction(HWND h,uint64_t token){
        for(int i=0;i<3;++i){auto job=predictionJobs[i];if(!job||job->token!=token||!job->finished||job->cancelled)continue;
            predictionJobs[i].reset();
            if(job->ok&&job->result.find(std::string("NMR")+predictionNucleus(i)+"\t")==0){cached[i]=job->result;completed[i]=true;}
            else failures[i]=job->ok?"模型返回的核类型与谱种不匹配":job->result.empty()?"核磁预测未完成":job->result;
            if(i==activeTab)loadTab(h,i);else refreshPredictionStatus(h);return;
        }
    }
    void loadTab(HWND h,int index){
        if(index<0||index>2)return;
#ifdef MOLECULE_STUDIO_DX11
        structureRenderer.reset();
#endif
        loading=false;resetChart();structureZoom=1;structurePan=Vec2();activeTab=index;studio_i18n::sendMessage(tabs,TCM_SETCURSEL,index,0);peaks.clear();originalPeaks.clear();structure.reset();reportStructure.clear();formula.clear();coverage.clear();warning.clear();errorMessage.clear();engine="CSP5";carbon=index==1;fluorine=index==2;
        startPredictions(h);loading=bool(predictionJobs[index]);
        if(!asyncPredictor&&!completed[index]&&failures[index].empty()&&predictor){loading=true;refreshResults(h);UpdateWindow(h);std::string result;std::string action=std::string(predictionAction(index))+";solvent="+solventKey(std::max(1,solventIndex));bool ok=predictor(h,action,&result);loading=false;if(ok){cached[index]=result;completed[index]=true;}else failures[index]=result;}
        if(completed[index]){
            if(!parse(cached[index])){errorMessage="无法读取核磁预测结果";failures[index]=errorMessage;completed[index]=false;}
            else if(prepared[index]){structure=cachedStructures[index];peaks=cachedPeaks[index];originalPeaks=cachedOriginalPeaks[index];warning=preparedWarnings[index];}
            else{prepareStructure(source);cachedStructures[index]=structure;cachedPeaks[index]=peaks;cachedOriginalPeaks[index]=originalPeaks;preparedWarnings[index]=warning;prepared[index]=true;}
        }else if(!failures[index].empty())errorMessage=failures[index];
        refreshResults(h);
    }
    std::unique_ptr<Bitmap> reportImage(){
        int tableHeight=0;for(const auto& p:peaks)tableHeight+=std::max(30,int(ceil(p.atom.size()/32.))*26);int height=1000+tableHeight;std::unique_ptr<Bitmap> bitmap(new Bitmap(1800,height,PixelFormat32bppARGB));Graphics g(bitmap.get());g.Clear(Color::White);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        Font heading(L"Microsoft YaHei UI",25,FontStyleBold,UnitPixel),font(L"Microsoft YaHei UI",20,FontStyleRegular,UnitPixel);SolidBrush ink(Color(35,45,50));auto name=W(nucleus()+" NMR   "+formula+"   |   "+engine+"   |   coverage "+coverage);studio_i18n::drawString(g,name.c_str(),-1,&heading,PointF(35,24),&ink);auto settings=W(conditions()+"；ppm 不随频率缩放，Hz = ppm × 当前核 MHz");studio_i18n::drawString(g,settings.c_str(),-1,&font,PointF(35,62),&ink);
        auto saved=g.Save();g.ScaleTransform(1.6f,1.6f);drawChart(g,RectF(15,65,630,290),false);drawStructure(g,RectF(665,65,445,290),false);g.Restore(saved);
        std::wstring legend=nonProton()?L"标注格式 #原子序号：化学位移（ppm）；峰表的序号与结构图一致。":L"标注格式 #原子序号：氢化学位移（ppm）；序号指氢所连的重原子，同一原子可有多个位移。";studio_i18n::drawString(g,legend.c_str(),-1,&font,PointF(35,590),&ink);
        int y=635;const wchar_t* headers[]={L"δ / ppm",fluorine?L"等价 F 数":carbon?L"等价 C 数":L"积分 H",L"原子序号",L"预测依据 / 模型不确定度"};const float columns[]={35,195,370,790};for(int i=0;i<4;++i)studio_i18n::drawString(g,headers[i],-1,&font,PointF(columns[i],float(y)),&ink);y+=35;
        Pen line(Color(215,220,223),1);for(const auto& p:peaks){int rowHeight=std::max(30,int(ceil(p.atom.size()/32.))*26);g.DrawLine(&line,35.f,float(y-3),1765.f,float(y-3));wchar_t ppm[40],amount[40];swprintf(ppm,40,L"%.3f",p.ppm);swprintf(amount,40,L"%.2f",p.h);studio_i18n::drawString(g,ppm,-1,&font,PointF(35,float(y)),&ink);studio_i18n::drawString(g,amount,-1,&font,PointF(195,float(y)),&ink);auto atom=W(p.atom);studio_i18n::drawString(g,atom.c_str(),-1,&font,RectF(370,float(y),400,float(rowHeight)),0,&ink);auto detail=W(p.reason);studio_i18n::drawString(g,detail.c_str(),-1,&font,RectF(790,float(y),975,30),0,&ink);y+=rowHeight;}
        auto savedLegend=g.Save();g.TranslateTransform(20,float(y+20));g.ScaleTransform(1.6f,1.6f);drawConfidence(g,1090,0);g.Restore(savedLegend);auto notice=W(warning);studio_i18n::drawString(g,notice.c_str(),-1,&font,RectF(35,float(y+110),1730,135),0,&ink);return bitmap;
    }
    bool writeReport(const std::wstring& file){auto bitmap=reportImage();UINT count=0,size=0;GetImageEncodersSize(&count,&size);std::vector<uint8_t> enc(size);auto codecs=reinterpret_cast<ImageCodecInfo*>(enc.data());GetImageEncoders(count,size,codecs);for(UINT i=0;i<count;++i)if(wcscmp(codecs[i].MimeType,L"image/png")==0)return bitmap->Save(file.c_str(),&codecs[i].Clsid,0)==Ok;return false;}
    void saveReport(HWND h){wchar_t file[32768]={};wcscpy(file,fluorine?L"19F-NMR-report.png":carbon?L"13C-NMR-report.png":L"1H-NMR-report.png");OPENFILENAMEW of={};of.lStructSize=sizeof(of);of.hwndOwner=h;of.lpstrFilter=L"谱图与标注结构 (PNG)\0*.png\0\0";of.lpstrFile=file;of.nMaxFile=32768;of.lpstrDefExt=L"png";of.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;if(studio_i18n::saveFileName(&of)&&!writeReport(file))studio_i18n::messageBox(h,L"无法保存报告图",L"导出失败",MB_OK|MB_ICONERROR);}
    void copyReport(HWND h){auto bitmap=reportImage();Rect rect(0,0,bitmap->GetWidth(),bitmap->GetHeight());BitmapData pixels={};if(bitmap->LockBits(&rect,ImageLockModeRead,PixelFormat32bppARGB,&pixels)!=Ok)return;size_t row=size_t(rect.Width)*4,bytes=sizeof(BITMAPINFOHEADER)+row*rect.Height;HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(memory){auto data=static_cast<uint8_t*>(GlobalLock(memory));if(data){BITMAPINFOHEADER header={};header.biSize=sizeof(header);header.biWidth=rect.Width;header.biHeight=-rect.Height;header.biPlanes=1;header.biBitCount=32;header.biCompression=BI_RGB;memcpy(data,&header,sizeof(header));for(int y=0;y<rect.Height;++y)memcpy(data+sizeof(header)+row*y,static_cast<uint8_t*>(pixels.Scan0)+pixels.Stride*y,row);GlobalUnlock(memory);if(OpenClipboard(h)){EmptyClipboard();if(SetClipboardData(CF_DIB,memory))memory=0;CloseClipboard();}}if(memory)GlobalFree(memory);}bitmap->UnlockBits(&pixels);}
    std::wstring csv()const{std::ostringstream out;out<<(fluorine?"delta_ppm,equivalent_F,atom_ids,multiplicity,assignment,solvent,instrument_1H_MHz,nucleus_MHz,delta_Hz":carbon?"delta_ppm,equivalent_C,atom_ids,multiplicity,assignment,solvent,instrument_1H_MHz,nucleus_MHz,delta_Hz":"delta_ppm,integral_H,atom_ids,multiplicity_estimate,assignment,solvent,instrument_1H_MHz,nucleus_MHz,delta_Hz");if(!nonProton())out<<",assumed_coupling_hz";out<<"\r\n";for(auto& p:peaks){auto quote=[](std::string s){size_t at=0;while((at=s.find('"',at))!=std::string::npos){s.insert(at,1,'"');at+=2;}return "\""+s+"\"";};out<<std::fixed<<std::setprecision(4)<<p.ppm<<','<<p.h<<','<<quote(p.atom)<<','<<quote(displayedMultiplicity(p))<<','<<quote(p.reason)<<','<<quote(solventKey(solventIndex))<<','<<protonMHz<<','<<nucleusMHz()<<','<<p.ppm*nucleusMHz();if(!nonProton()){out<<',';if(p.splitN>0&&simulatedSplit(p))out<<7.0;}out<<"\r\n";}return W(out.str());}
    void copy(HWND h){if(OpenClipboard(h)){EmptyClipboard();auto text=csv();HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));if(memory){void* data=GlobalLock(memory);if(data){memcpy(data,text.c_str(),(text.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);}else GlobalFree(memory);}CloseClipboard();}}
    void saveCSV(HWND h){wchar_t file[32768]={};wcscpy(file,fluorine?L"19F-NMR-prediction.csv":carbon?L"13C-NMR-prediction.csv":L"1H-NMR-prediction.csv");OPENFILENAMEW of={};of.lStructSize=sizeof(of);of.hwndOwner=h;of.lpstrFilter=L"CSV 文件\0*.csv\0\0";of.lpstrFile=file;of.nMaxFile=32768;of.lpstrDefExt=L"csv";of.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;if(studio_i18n::saveFileName(&of)){try{chemservice::write(file,"\xEF\xBB\xBF"+U(csv()));}catch(const std::exception& e){studio_i18n::messageBox(h,W(e.what()).c_str(),L"导出失败",MB_OK|MB_ICONERROR);}}}
    static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l){auto d=reinterpret_cast<NmrDialog*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(msg==WM_NCCREATE){d=static_cast<NmrDialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(d));}
        if(!d)return DefWindowProcW(h,msg,w,l);
        if(msg==WM_CREATE){d->window=h;SetWindowLongPtrW(h,GWL_STYLE,GetWindowLongPtrW(h,GWL_STYLE)|WS_CLIPCHILDREN);d->dpi=GetDpiForWindow(h);if(!d->dpi)d->dpi=96;d->font=CreateFontW(-MulDiv(10,d->dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            auto child=[&](const wchar_t* kind,const wchar_t* text,DWORD style,int id){HWND c=studio_i18n::createWindowEx(0,kind,text,WS_CHILD|WS_VISIBLE|style,0,0,10,10,h,(HMENU)(INT_PTR)id,GetModuleHandleW(0),0);studio_i18n::sendMessage(c,WM_SETFONT,(WPARAM)d->font,TRUE);return c;};
            d->tabs=child(WC_TABCONTROLW,L"",WS_TABSTOP,730);const wchar_t* tabNames[]={L"¹H 氢谱",L"¹³C 碳谱",L"¹⁹F 氟谱"};for(int i=0;i<3;++i){TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(tabNames[i]);studio_i18n::sendMessage(d->tabs,TCM_INSERTITEMW,i,(LPARAM)&item);}auto heading=W(d->nucleus()+" NMR 预测");child(L"STATIC",heading.c_str(),0,720);child(L"STATIC",W(d->warning).c_str(),0,721);
            child(L"STATIC",L"溶剂",0,731);auto solvent=child(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,732);for(int i=0;i<12;++i)studio_i18n::sendMessage(solvent,CB_ADDSTRING,0,(LPARAM)solventName(i));studio_i18n::sendMessage(solvent,CB_SETCURSEL,d->solventIndex,0);
            child(L"STATIC",L"仪器 ¹H 频率 / MHz",0,733);auto frequency=child(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL,734);for(auto value:{60,100,200,300,400,500,600,700,800,900,1000,1200}){auto text=std::to_wstring(value);studio_i18n::sendMessage(frequency,CB_ADDSTRING,0,(LPARAM)text.c_str());}studio_i18n::setWindowText(frequency,L"400");
            d->spaceCombo(solvent);d->spaceCombo(frequency);
            child(L"BUTTON",L"应用条件",WS_TABSTOP,735);child(L"BUTTON",L"Hz 横轴",WS_TABSTOP|BS_AUTOCHECKBOX,736);child(L"STATIC",L"谱图 / 结构：滚轮缩放、拖动平移、双击复位",0,737);child(L"BUTTON",L"显示全谱",WS_TABSTOP,738);
            d->list=child(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,724);studio_i18n::sendMessage(d->list,LVM_SETEXTENDEDLISTVIEWSTYLE,0,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
            const wchar_t* titles[]={L"δ (ppm)",d->carbon?L"等价碳数":L"积分 (H)",L"原子序号",L"裂分（示意）",L"预测依据",L"Δν / Hz"};int widths[]={80,80,100,110,440,110};for(int i=0;i<6;++i){LVCOLUMNW col={};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=const_cast<wchar_t*>(titles[i]);col.cx=d->px(widths[i]);studio_i18n::sendMessage(d->list,LVM_INSERTCOLUMNW,i,(LPARAM)&col);}
            child(L"BUTTON",L"复制峰表",WS_TABSTOP,722);child(L"BUTTON",L"导出报告图…",WS_TABSTOP,723);child(L"BUTTON",L"复制报告图",WS_TABSTOP,725);child(L"BUTTON",L"导出 CSV…",WS_TABSTOP,726);child(L"BUTTON",L"关闭",WS_TABSTOP|BS_DEFPUSHBUTTON,IDCANCEL);d->layout(h);d->refreshResults(h);if(d->predictor||d->asyncPredictor)PostMessageW(h,WM_APP+93,0,0);return 0;}
        if(msg==WM_APP+93){d->loadTab(h,0);return 0;}if(msg==WM_APP+94){d->finishPrediction(h,uint64_t(w));return 0;}if(msg==WM_NOTIFY){auto header=reinterpret_cast<NMHDR*>(l);if(header->hwndFrom==d->tabs&&header->code==TCN_SELCHANGE){d->loadTab(h,int(studio_i18n::sendMessage(d->tabs,TCM_GETCURSEL,0,0)));}else if(header->hwndFrom==d->list&&header->code==LVN_ITEMCHANGED){auto change=reinterpret_cast<NMLISTVIEW*>(l);if((change->uChanged&LVIF_STATE)&&(change->uNewState&LVIS_SELECTED)){d->chartFocus=change->iItem;d->chartZoom=1;d->invalidatePreview(h,1);}}return 0;}
        if(msg==WM_MOUSEWHEEL&&!d->peaks.empty()){
            POINT point={GET_X_LPARAM(l),GET_Y_LPARAM(l)};ScreenToClient(h,&point);Vec2 p(point.x*96.0/d->dpi,point.y*96.0/d->dpi);RectF chart,molecule;d->previewRects(h,&chart,&molecule);double factor=std::pow(1.25,double(GET_WHEEL_DELTA_WPARAM(w))/WHEEL_DELTA);
            if(insidePreview(chart,p)){d->zoomChart(chart,p.x,factor);d->invalidatePreview(h,1);return 0;}
            if(insidePreview(molecule,p)&&d->structure){d->zoomStructure(molecule,p,factor);d->invalidatePreview(h,2);return 0;}
        }
        if((msg==WM_LBUTTONDOWN||msg==WM_LBUTTONDBLCLK)&&!d->peaks.empty()){
            Vec2 p(GET_X_LPARAM(l)*96.0/d->dpi,GET_Y_LPARAM(l)*96.0/d->dpi);RectF chart,molecule;d->previewRects(h,&chart,&molecule);int area=insidePreview(chart,p)?1:insidePreview(molecule,p)&&d->structure?2:0;
            if(area){if(msg==WM_LBUTTONDBLCLK){if(area==1)d->resetChart();else{d->structureZoom=1;d->structurePan=Vec2();}d->previewDrag=0;if(GetCapture()==h)ReleaseCapture();d->invalidatePreview(h,area);return 0;}
                d->previewDrag=area;d->panPress=p;d->panStart=d->structurePan;double low,high;d->chartRange(true,&low,&high);d->panChartCenter=(low+high)*.5;SetCapture(h);return 0;}
        }
        if(msg==WM_MOUSEMOVE&&d->previewDrag){
            Vec2 p(GET_X_LPARAM(l)*96.0/d->dpi,GET_Y_LPARAM(l)*96.0/d->dpi),delta=p-d->panPress;RectF chart,molecule;d->previewRects(h,&chart,&molecule);
            if(d->previewDrag==2)d->structurePan=d->panStart+delta;
            else if(d->chartZoom>1){double low,high,baseLow,baseHigh;d->chartRange(true,&low,&high);d->baseChartRange(true,&baseLow,&baseHigh);double span=high-low;d->chartCenter=std::max(baseLow+span*.5,std::min(baseHigh-span*.5,d->panChartCenter+delta.x*span/std::max(1.f,chart.Width-62)));}
            d->invalidatePreview(h,d->previewDrag);return 0;
        }
        if(msg==WM_LBUTTONUP&&d->previewDrag){d->previewDrag=0;if(GetCapture()==h)ReleaseCapture();return 0;}
        if(msg==WM_CAPTURECHANGED)d->previewDrag=0;
        if(msg==WM_ERASEBKGND)return 1;
        if(msg==WM_SIZE){d->layout(h);return 0;}if(msg==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);d->paint(h,dc,&ps.rcPaint);EndPaint(h,&ps);return 0;}
        if(msg==WM_GETMINMAXINFO){auto m=(MINMAXINFO*)l;m->ptMinTrackSize={d->px(850),d->px(610)};return 0;}
        if(msg==WM_COMMAND){if(LOWORD(w)==735)d->applyConditions(h);if(LOWORD(w)==736){d->hzAxis=studio_i18n::sendMessage(GetDlgItem(h,736),BM_GETCHECK,0,0)==BST_CHECKED;d->invalidatePreview(h,1);}if(LOWORD(w)==738){d->resetChart();ListView_SetItemState(d->list,-1,0,LVIS_SELECTED);d->invalidatePreview(h,1);}if(LOWORD(w)==722)d->copy(h);if(LOWORD(w)==723)d->saveReport(h);if(LOWORD(w)==725)d->copyReport(h);if(LOWORD(w)==726)d->saveCSV(h);if(LOWORD(w)==IDCANCEL){if(d->closePage)d->closePage();else DestroyWindow(h);}return 0;}
        if(msg==WM_DESTROY){d->cancelPredictions();d->window=0;return 0;}if(msg==WM_CLOSE){if(d->closePage)d->closePage();else DestroyWindow(h);return 0;}return DefWindowProcW(h,msg,w,l);}
    static void applyAnnotations(Document& doc,const std::vector<Peak>& peaks,const std::map<int,ObjectId>& ids,bool carbon,bool fluorine=false){
        std::map<ObjectId,std::vector<Peak>> atoms;for(auto p:peaks){std::istringstream in(p.atom);std::string id;while(std::getline(in,id,',')){auto found=ids.find(atoi(id.c_str()));if(found!=ids.end())atoms[found->second].push_back(p);}}
        std::vector<Box> occupied;for(const auto& atom:doc.atoms().records())if(atom.isAlive()){double r=doc.atomLabel(atom.id).empty()?2:doc.defaultTextSize()*.65;occupied.push_back(Box(atom.position-Vec2(r,r),atom.position+Vec2(r,r)));}
        for(const auto& entry:atoms){auto ref=ObjectRef(ObjectKind::Atom,entry.first);const auto& atom=*doc.atoms().get(entry.first);auto& bag=doc.extensions(ref);tinyxml2::XMLDocument tags;
            if(auto raw=bag.findValue("studio.atom.tags"))tags.Parse(raw->c_str());if(!tags.RootElement())tags.InsertEndChild(tags.NewElement("tags"));auto root=tags.RootElement();
            for(auto t=root->FirstChildElement("tag");t;){auto next=t->NextSiblingElement("tag");auto name=t->Attribute("name");if(name&&std::string(name).find("MoleculeStudio.NMR.")==0)root->DeleteChild(t);t=next;}
            double size=std::max(6.,doc.defaultTextSize()*.8),w=0;std::vector<std::string> labels;
            for(auto p:entry.second){char label[32];snprintf(label,sizeof(label),"%.2f",p.ppm);labels.push_back(p.label.empty()?std::string(label):p.label);w=std::max(w,size*.56*labels.back().size());}
            double height=size*1.15*labels.size(),best=1e100;Vec2 offset;
            for(int step=0;step<32;++step){double angle=step*6.283185307179586/32;Vec2 dir(cos(angle),sin(angle));double radius=doc.defaultTextSize()*1.5+std::max(w,height)*.35;Vec2 center=atom.position+dir*radius;Box box(center-Vec2(w*.5,height*.5),center+Vec2(w*.5,height*.5));double score=0;
                for(auto b:occupied){double x=std::max(0.,std::min(box.max.x,b.max.x+2)-std::max(box.min.x,b.min.x-2)),y=std::max(0.,std::min(box.max.y,b.max.y+2)-std::max(box.min.y,b.min.y-2));score+=x*y*100;}
                for(const auto& b:doc.bonds().records())if(b.isAlive()&&(b.beginAtom==entry.first||b.endAtom==entry.first)){Vec2 v=doc.atoms().get(b.beginAtom==entry.first?b.endAtom:b.beginAtom)->position-atom.position;double l=length(v);if(l>0)score+=std::pow(std::max(0.,dot(dir,v*(1/l))),8)*100;}
                score+=step*.001;if(score<best){best=score;offset=box.min-atom.position+Vec2(0,size*.85);}}
            occupied.push_back(Box(atom.position+offset-Vec2(0,size*.85),atom.position+offset+Vec2(w,height-size*.85)));
            for(size_t i=0;i<labels.size();++i){auto t=tags.NewElement("tag");t->SetAttribute("name",fluorine?"MoleculeStudio.NMR.19F":carbon?"MoleculeStudio.NMR.13C":"MoleculeStudio.NMR.1H");t->SetAttribute("dx",offset.x);t->SetAttribute("dy",offset.y+size*1.15*i);auto quality=entry.second[i].quality;t->SetAttribute("color",quality=="good"?0x0000ffffu:quality=="medium"?0xcc00ccffu:0xdd2222ffu);auto r=tags.NewElement("s");r->SetAttribute("font",doc.defaultFont().c_str());r->SetAttribute("size",size);r->SetText(labels[i].c_str());t->InsertEndChild(r);root->InsertEndChild(t);}
            tinyxml2::XMLPrinter printer;tags.Print(&printer);bag.set("studio.atom.tags",printer.CStr());
        }
    }
    HWND createEmbedded(HWND parent){
        embedded=true;WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.style=CS_DBLCLKS;wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(0);wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.lpszClassName=L"MoleculeStudioNmrPage";RegisterClassExW(&wc);
        RECT r;GetClientRect(parent,&r);return studio_i18n::createWindowEx(WS_EX_CONTROLPARENT,wc.lpszClassName,L"核磁预测",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,40,std::max(1,int(r.right)),std::max(1,int(r.bottom)-40),parent,0,wc.hInstance,this);
    }
    static bool show(HWND owner,const std::string& result,std::vector<Peak>* assigned=0,const std::string& source="",std::shared_ptr<Session>* illustrated=0,bool interactive=false){NmrDialog d;d.owner=owner;d.source=source;if(interactive){d.asyncPredictor=[source](const std::string& action,std::string* result,const std::atomic<bool>* cancel){return chemservice::run(nullptr,source,"text/xml","text/x-moleculestudio-nmr",action,result,cancel);};}else{if(!d.parse(result))return false;if(!source.empty())d.prepareStructure(source);}WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.style=CS_DBLCLKS;wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(0);wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.hbrBackground=nullptr;wc.lpszClassName=L"MoleculeStudioNmrResults";RegisterClassExW(&wc);
        UINT dpi=GetDpiForWindow(owner);if(!dpi)dpi=96;RECT parent;GetWindowRect(owner,&parent);int width=MulDiv(1100,dpi,96),height=MulDiv(720,dpi,96);MONITORINFO mi={};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&mi);width=std::min(width,int(mi.rcWork.right-mi.rcWork.left));height=std::min(height,int(mi.rcWork.bottom-mi.rcWork.top));
        HWND h=studio_i18n::createWindowEx(WS_EX_DLGMODALFRAME,wc.lpszClassName,L"预测核磁 — Molecule Studio",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,(mi.rcWork.left+mi.rcWork.right-width)/2,(mi.rcWork.top+mi.rcWork.bottom-height)/2,width,height,owner,0,wc.hInstance,&d);if(!h)return false;
        EnableWindow(owner,FALSE);ShowWindow(h,SW_SHOW);MSG msg;while(IsWindow(h)&&GetMessageW(&msg,0,0,0)>0){if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}EnableWindow(owner,TRUE);SetForegroundWindow(owner);if(d.font){DeleteObject(d.font);d.font=0;}if(assigned&&d.annotate)*assigned=d.originalPeaks.empty()?d.peaks:d.originalPeaks;if(illustrated)*illustrated=d.structure;return true;}
};
#endif

