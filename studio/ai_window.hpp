#pragma once
// Modeless UI. The worker receives its credential through an anonymous pipe;
// only DPAPI ciphertext is persisted, and only when Remember is selected.
struct AiWindow {
    ChatTranscript chat;bool paneLocked=false;bool sizing=false,sizingChanged=false;
    // Static chrome (background, title, prompt card) is rendered once per layout into a DIB and
    // blitted per paint; GDI+ repainting the whole client for a 40x20 update cost ~90 ms.
    HDC bgMem=nullptr;HBITMAP bgDib=nullptr;HGDIOBJ bgOld=nullptr;int bgW=0,bgH=0;bool bgValid=false;int bgLanguage=-1;
    HWND hwnd=0,owner=0;HFONT font=0;bool imageTemporary=false;std::string apiStatus="尚未测试连接";float dockScale=1;bool docked=false;RECT floating={0,0,0,0};
    std::function<void()> openSettings,dockChanged,configurationChanged;
    HANDLE process=0,job=0;
    std::wstring directory,resultPath,storageOverride,imagePath,screenshotPath;std::string lastProgress,pendingPrompt,imageName,imageMime;bool testMode=false,modelsMode=false,pluginLogged=false;
    static std::string legacyDefaultPreset(){return "你是 Molecule Studio 的化学绘图助手。\n\n理解用户意图，调用绘图工具将要求完整落实到可编辑画布。真实化合物优先查询可靠数据库，保持化学结构与立体信息准确。\n\n反应式与多步流程应清晰对齐，结构、箭头、条件和编号保持合适间距。优先使用组合绘图和自动排版工具。\n\n使用简洁中文说明完成内容；遇到关键歧义再询问，不能将失败描述为已完成。";}
    static std::string defaultPreset(){return legacyDefaultPreset()+"\n\n反应条件与绘图风格（可按个人习惯修改）：\n试剂与当量放在箭头上方，用 draw_reaction / draw_scheme 的 reagents 数组，每个试剂一项；默认一个试剂一行，超过四项时每行两项。溶剂、温度、反应时间、气体保护放在 conditions_below，用换行分组。箭头保持紧凑，避免把全部条件挤成一长行。\n条件文字默认自动排化学下标，电荷和同位素上标用 ^ 明确指定，例如 SO4^2-、^13CH3OH；不要把 0.5 equiv、25 °C、2 h 中的数字变成下标。可用 formula_layout=false 保留原文字，用 reagents_per_line 或显式换行调整分行，用 arrow_length 指定箭头长度。用户本轮要求优先于此预设。";}
    std::string preset=defaultPreset();
    std::vector<std::pair<std::string,std::string>> history;
    std::function<std::string()> context;
    std::function<std::string(const std::string&)> apply;
    std::function<bool(const std::wstring&)> capture;
    ~AiWindow(){clearImage();stop(false);if(IsWindow(hwnd))DestroyWindow(hwnd);if(font)DeleteObject(font);releaseBackground();}
    static std::wstring controlText(HWND h){int n=studio_i18n::getWindowTextLength(h);std::wstring v(size_t(n)+1,L'\0');studio_i18n::getWindowText(h,&v[0],n+1);v.resize(n);return v;}
    std::string get(int id)const{return U(controlText(GetDlgItem(hwnd,id)));}
    void set(int id,const std::string& value){if(id==710&&value.empty())chat.clear();studio_i18n::setDlgItemText(hwnd,id,W(value).c_str());}
    void syncImageStatus(){if(!IsWindow(hwnd))return;set(709,imagePath.empty()?"绘图风格可在 设置 → AI 预设 中修改 · Ctrl+Enter 发送":"已附加图片："+imageName);}
    void clearImage(){if(imageTemporary&&!imagePath.empty())DeleteFileW(imagePath.c_str());imageTemporary=false;imagePath.clear();imageMime.clear();imageName.clear();syncImageStatus();}
    void chooseImage(){
        wchar_t file[32768]={};OPENFILENAMEW ofn={};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=hwnd;ofn.lpstrFile=file;ofn.nMaxFile=32768;
        ofn.lpstrFilter=L"图片 (*.png;*.jpg;*.jpeg;*.webp;*.gif)\0*.png;*.jpg;*.jpeg;*.webp;*.gif\0所有文件\0*.*\0";
        ofn.lpstrTitle=L"选择要发给 AI 的图片";ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_HIDEREADONLY|OFN_NOCHANGEDIR;
        if(!studio_i18n::openFileName(&ofn))return;
        clearImage();imagePath=file;std::wstring name=imagePath;size_t slash=name.find_last_of(L"\\/");imageName=U(slash==std::wstring::npos?name:name.substr(slash+1));
        std::wstring ext=name;for(auto& ch:ext)ch=std::towlower(ch);
        imageMime=(ext.size()>=4&&ext.substr(ext.size()-4)==L".jpg")||(ext.size()>=5&&ext.substr(ext.size()-5)==L".jpeg")?"image/jpeg":
                  (ext.size()>=5&&ext.substr(ext.size()-5)==L".webp")?"image/webp":
                  (ext.size()>=4&&ext.substr(ext.size()-4)==L".gif")?"image/gif":"image/png";
        syncImageStatus();log("已选择图片："+imageName);
    }
    static bool clipboardImageAvailable(){return IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG"))||IsClipboardFormatAvailable(CF_BITMAP)||IsClipboardFormatAvailable(CF_DIB)||IsClipboardFormatAvailable(CF_DIBV5);}
    bool pasteImage(){
        if(process){log("请等待当前任务完成，再粘贴图片。");return false;}
        if(!clipboardImageAvailable()){log("剪贴板中没有图片。可复制截图或图片后再按 Ctrl+V。");return false;}
        if(!OpenClipboard(hwnd)){log("暂时无法读取剪贴板，请稍后再试。");return false;}
        IStream* stream=nullptr;std::unique_ptr<Bitmap> bitmap;HGLOBAL copy=nullptr;std::wstring path;
        try{
            HANDLE png=GetClipboardData(RegisterClipboardFormatW(L"PNG"));
            if(png){SIZE_T bytes=GlobalSize(png);if(!bytes||bytes>12*1024*1024)throw std::runtime_error("剪贴板图片超过 12 MB，请缩小图片后粘贴");
                void* src=GlobalLock(png);if(!src)throw std::runtime_error("无法读取剪贴板图片");copy=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!copy){GlobalUnlock(png);throw std::runtime_error("图片内存不足");}void* dst=GlobalLock(copy);if(!dst){GlobalUnlock(png);throw std::runtime_error("图片内存不足");}memcpy(dst,src,bytes);GlobalUnlock(copy);GlobalUnlock(png);
                if(FAILED(CreateStreamOnHGlobal(copy,TRUE,&stream)))throw std::runtime_error("无法读取剪贴板 PNG");copy=nullptr;bitmap.reset(Bitmap::FromStream(stream));
            }else{
                HBITMAP native=(HBITMAP)GetClipboardData(CF_BITMAP);if(!native)throw std::runtime_error("剪贴板图片格式暂不支持，请先复制为截图");BITMAP details={};GetObjectW(native,sizeof(details),&details);if(details.bmWidth>16000||std::abs(details.bmHeight)>16000)throw std::runtime_error("剪贴板图片尺寸过大");bitmap.reset(Bitmap::FromHBITMAP(native,nullptr));
            }
            if(!bitmap||bitmap->GetLastStatus()!=Ok||!bitmap->GetWidth()||!bitmap->GetHeight())throw std::runtime_error("剪贴板中的图片无法解码");
            GUID guid;CoCreateGuid(&guid);wchar_t name[64];StringFromGUID2(guid,name,64);path=directory+L"\\paste-"+name+L".png";
            UINT count=0,bytes=0;GetImageEncodersSize(&count,&bytes);std::vector<BYTE> storage(bytes);auto encoders=(ImageCodecInfo*)storage.data();GetImageEncoders(count,bytes,encoders);const CLSID* encoder=nullptr;for(UINT i=0;i<count;i++)if(wcscmp(encoders[i].MimeType,L"image/png")==0)encoder=&encoders[i].Clsid;
            if(!encoder)throw std::runtime_error("PNG 编码器不可用");
            if(bitmap->GetWidth()>1600||bitmap->GetHeight()>1600){double factor=1600./std::max(bitmap->GetWidth(),bitmap->GetHeight());Bitmap scaled(std::max(1,int(bitmap->GetWidth()*factor)),std::max(1,int(bitmap->GetHeight()*factor)),PixelFormat32bppARGB);Graphics g(&scaled);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.DrawImage(bitmap.get(),0,0,scaled.GetWidth(),scaled.GetHeight());if(scaled.Save(path.c_str(),encoder,nullptr)!=Ok)throw std::runtime_error("无法保存粘贴图片");}
            else if(bitmap->Save(path.c_str(),encoder,nullptr)!=Ok)throw std::runtime_error("无法保存粘贴图片");
            bitmap.reset();if(stream){stream->Release();stream=nullptr;}CloseClipboard();clearImage();imagePath=path;imageTemporary=true;imageMime="image/png";imageName="剪贴板图片";syncImageStatus();log("已附加剪贴板图片。可以继续输入要求，然后发送并绘制。");return true;
        }catch(const std::exception& e){bitmap.reset();if(stream)stream->Release();if(copy)GlobalFree(copy);CloseClipboard();if(!path.empty())DeleteFileW(path.c_str());log(std::string("未粘贴：")+e.what());return false;}
    }
    static std::string protect(const std::string& bytes,bool encrypt){DATA_BLOB in={DWORD(bytes.size()),(BYTE*)bytes.data()},out={};BOOL ok=encrypt?CryptProtectData(&in,L"Molecule Studio API",0,0,0,CRYPTPROTECT_UI_FORBIDDEN,&out):CryptUnprotectData(&in,0,0,0,0,CRYPTPROTECT_UI_FORBIDDEN,&out);if(!ok)throw std::runtime_error("无法读取或保存 Windows 加密的 API 密钥");std::string result((char*)out.pbData,out.cbData);SecureZeroMemory(out.pbData,out.cbData);LocalFree(out.pbData);return result;}
    void log(const std::string& text){
        chat.append(text);
        auto box=GetDlgItem(hwnd,710);std::wstring value=W(text+"\r\n\r\n");studio_i18n::sendMessage(box,EM_SETSEL,-1,-1);
        CHARFORMAT2W fmt={};fmt.cbSize=sizeof(fmt);fmt.dwMask=CFM_COLOR|CFM_BOLD;fmt.crTextColor=text.find("你：")==0?RGB(24,117,112):RGB(48,64,76);fmt.dwEffects=text.find("你：")==0?CFE_BOLD:0;
        studio_i18n::sendMessage(box,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&fmt);studio_i18n::sendMessage(box,EM_REPLACESEL,FALSE,(LPARAM)value.c_str());studio_i18n::sendMessage(box,EM_SCROLLCARET,0,0);
    }
    void busy(bool running){
        for(int id:{702,704,706,707,708,712,714,716,717,718,719,724,725})EnableWindow(GetDlgItem(hwnd,id),!running);
        EnableWindow(GetDlgItem(hwnd,713),running);set(712,running?"正在绘制…":"发送并绘制");
        set(715,running?"正在处理 · 你可以继续查看画布":"就绪 · 本轮绘图可按 Ctrl+Z 撤销");
        set(723,"模型 · "+get(704));if(configurationChanged)configurationChanged();
    }
    void cleanup(){if(!resultPath.empty()){DeleteFileW(resultPath.c_str());auto status=resultPath.substr(0,resultPath.size()-4)+L".status";DeleteFileW(status.c_str());resultPath.clear();}if(!screenshotPath.empty()){DeleteFileW(screenshotPath.c_str());screenshotPath.clear();}}
    void stop(bool announce=true){if(process){TerminateProcess(process,4);WaitForSingleObject(process,500);CloseHandle(process);process=0;if(job){CloseHandle(job);job=0;}if(IsWindow(hwnd)){KillTimer(hwnd,1);busy(false);if(announce)log("已取消，本轮没有修改画布。");}}cleanup();}
    void load(){
        if(storageOverride.empty()){wchar_t local[32768]={};GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);directory=std::wstring(local)+L"\\MoleculeStudio";CreateDirectoryW(directory.c_str(),0);directory+=L"\\AI";}else directory=storageOverride;CreateDirectoryW(directory.c_str(),0);
        try{tinyxml2::XMLDocument xml;auto bytes=chemservice::read(directory+L"\\settings.xml");if(xml.Parse(bytes.c_str())==tinyxml2::XML_SUCCESS)if(auto root=xml.RootElement()){if(auto p=root->FirstChildElement("preset")){if(p->GetText()&&strlen(p->GetText())<=24000)preset=p->GetText();if(preset==legacyDefaultPreset())preset=defaultPreset();}if(auto e=root->Attribute("endpoint"))set(702,e);if(auto m=root->Attribute("model"))set(704,m);if(root->BoolAttribute("remember")){auto key=protect(chemservice::read(directory+L"\\credential.bin"),false);set(706,key);SecureZeroMemory(&key[0],key.size());CheckDlgButton(hwnd,707,BST_CHECKED);}if(root->BoolAttribute("advanced"))CheckDlgButton(hwnd,725,BST_CHECKED);}}catch(const std::exception&){}
    }
    void save(){
        bool remember=IsDlgButtonChecked(hwnd,707)==BST_CHECKED;tinyxml2::XMLDocument xml;auto root=xml.NewElement("ai");root->SetAttribute("endpoint",get(702).c_str());root->SetAttribute("model",get(704).c_str());root->SetAttribute("remember",remember);root->SetAttribute("advanced",IsDlgButtonChecked(hwnd,725)==BST_CHECKED);auto preferences=xml.NewElement("preset");preferences->SetText(preset.c_str());root->InsertEndChild(preferences);xml.InsertEndChild(root);tinyxml2::XMLPrinter p;xml.Print(&p);chemservice::write(directory+L"\\settings.xml",p.CStr());
        if(remember){auto key=get(706);auto encrypted=protect(key,true);SecureZeroMemory(&key[0],key.size());chemservice::write(directory+L"\\credential.bin",encrypted);}else DeleteFileW((directory+L"\\credential.bin").c_str());
    }
    void updateModels(const tinyxml2::XMLElement* models){
        if(!models)return;
        std::vector<std::string> names;
        for(auto item=models->FirstChildElement("model");item&&names.size()<500;item=item->NextSiblingElement("model")){
            std::string name=item->Attribute("id")?item->Attribute("id"):"";
            if(name.empty()||name.size()>480||name.find_first_of("\r\n\t ")!=std::string::npos)continue;
            if(std::find(names.begin(),names.end(),name)==names.end())names.push_back(name);
        }
        if(names.empty())throw std::runtime_error("服务没有返回可用模型，请手动填写或重新获取");
        auto current=get(704);auto combo=GetDlgItem(hwnd,704);studio_i18n::sendMessage(combo,CB_RESETCONTENT,0,0);studio_i18n::sendMessage(combo,CB_ADDSTRING,0,(LPARAM)L"auto");
        for(const auto& name:names)if(name!="auto")studio_i18n::sendMessage(combo,CB_ADDSTRING,0,(LPARAM)W(name).c_str());
        set(704,current.empty()?"auto":current);
    }
    void start(bool testing,bool fetchingModels=false){
        if(process)return;
        if(testing)apiStatus=fetchingModels?"正在获取模型…":"正在测试连接…";
        try{
            if(get(706).empty())throw std::runtime_error("请先填写 API Key");
            pendingPrompt=get(711);if(!testing&&pendingPrompt.empty())throw std::runtime_error("请输入绘图要求");
            if(!testing&&!fetchingModels&&IsDlgButtonChecked(hwnd,719)==BST_CHECKED&&capture){GUID shotGuid;CoCreateGuid(&shotGuid);wchar_t shotName[64];StringFromGUID2(shotGuid,shotName,64);screenshotPath=directory+L"\\"+shotName+L".png";bool ok=false;try{ok=capture(screenshotPath);}catch(...){ok=false;}if(!ok){screenshotPath.clear();log("画布截图失败，本轮只发送文字与对象信息。");}}
            std::string base=testing?"<request/>":context();tinyxml2::XMLDocument xml;if(xml.Parse(base.c_str())!=tinyxml2::XML_SUCCESS)throw std::runtime_error("无法生成文档概况");auto root=xml.RootElement();root->SetAttribute("mode",fetchingModels?"models":testing?"test":"draw");root->SetAttribute("advanced",IsDlgButtonChecked(hwnd,725)==BST_CHECKED?"1":"0");
            auto add=[&](const char* name,const std::string& value){auto e=xml.NewElement(name);e->SetText(value.c_str());root->InsertEndChild(e);};add("endpoint",get(702));add("model",get(704));add("key",get(706));add("prompt",pendingPrompt);add("preset",preset);if(!testing&&!fetchingModels){if(!imagePath.empty()){auto e=xml.NewElement("image");e->SetAttribute("path",U(imagePath).c_str());e->SetAttribute("mime",imageMime.empty()?"image/png":imageMime.c_str());e->SetAttribute("kind","upload");root->InsertEndChild(e);}if(!screenshotPath.empty()){auto e=xml.NewElement("image");e->SetAttribute("path",U(screenshotPath).c_str());e->SetAttribute("mime","image/png");e->SetAttribute("kind","canvas");root->InsertEndChild(e);}}
            auto past=xml.NewElement("history");root->InsertEndChild(past);for(size_t i=history.size()>10?history.size()-10:0;i<history.size();++i){auto item=xml.NewElement("message");item->SetAttribute("role",history[i].first.c_str());item->SetText(history[i].second.c_str());past->InsertEndChild(item);}tinyxml2::XMLPrinter printer;xml.Print(&printer);std::string request=printer.CStr();if(request.size()>2000000)throw std::runtime_error("对话内容过长，请开始新对话");
            save();GUID guid;CoCreateGuid(&guid);wchar_t uuid[64];StringFromGUID2(guid,uuid,64);resultPath=directory+L"\\"+uuid+L".xml";
            wchar_t exe[32768]={},python[32768]={};GetModuleFileNameW(0,exe,32768);std::wstring path=exe,dir=path.substr(0,path.find_last_of(L"\\/")),ini=dir+L"\\chemistry_backend.ini";GetPrivateProfileStringW(L"Backend",L"Python",L"python\\python.exe",python,32768,ini.c_str());std::wstring command=L"\""+chemservice::relativeTo(dir,python)+L"\" -X utf8 \""+dir+L"\\ai_assistant.py\" \""+resultPath+L"\"";
            SECURITY_ATTRIBUTES security={sizeof(security),0,TRUE};HANDLE read=0,write=0;if(!CreatePipe(&read,&write,&security,2097152))throw std::runtime_error("无法创建 AI 通信管道");SetHandleInformation(write,HANDLE_FLAG_INHERIT,0);HANDLE sink=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,0);
            STARTUPINFOW si={};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdInput=read;si.hStdOutput=si.hStdError=sink;PROCESS_INFORMATION pi={};
            BOOL created=CreateProcessW(0,&command[0],0,0,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,0,dir.c_str(),&si,&pi);CloseHandle(read);if(sink!=INVALID_HANDLE_VALUE)CloseHandle(sink);if(!created){CloseHandle(write);throw std::runtime_error("无法启动随附的 Python，请检查 chemistry_backend.ini");}
            process=pi.hProcess;SetPriorityClass(process,BELOW_NORMAL_PRIORITY_CLASS);job=CreateJobObjectW(0,0);if(job){JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits));AssignProcessToJobObject(job,process);}ResumeThread(pi.hThread);CloseHandle(pi.hThread);
            DWORD sent=0;BOOL written=WriteFile(write,request.data(),DWORD(request.size()),&sent,0);CloseHandle(write);SecureZeroMemory(&request[0],request.size());if(!written||sent!=request.size()){stop(false);throw std::runtime_error("AI 请求发送失败");}
            testMode=testing;modelsMode=fetchingModels;lastProgress.clear();busy(true);if(!testing){log("你："+pendingPrompt);set(711,"");}SetTimer(hwnd,1,250,0);
        }catch(const std::exception& e){stop(false);apiStatus=std::string("未开始：")+e.what();log(apiStatus);}
    }
    void poll(){
        if(!process)return;
        
        try{auto text=chemservice::read(resultPath.substr(0,resultPath.size()-4)+L".status",FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);if(text!=lastProgress){lastProgress=text;set(715,text);}}catch(const std::exception&){}
        if(WaitForSingleObject(process,0)!=WAIT_OBJECT_0)return;
        CloseHandle(process);process=0;if(job){CloseHandle(job);job=0;}KillTimer(hwnd,1);busy(false);
        try{auto bytes=chemservice::read(resultPath);tinyxml2::XMLDocument xml;if(xml.Parse(bytes.c_str())!=tinyxml2::XML_SUCCESS||!xml.RootElement())throw std::runtime_error("AI 返回格式不正确");auto root=xml.RootElement();auto message=root->FirstChildElement("message");std::string reply=message&&message->GetText()?message->GetText():"没有返回说明";if(!root->BoolAttribute("ok"))throw std::runtime_error(reply);
            if(!pluginLogged){for(auto plugin=root->FirstChildElement("plugin");plugin;plugin=plugin->NextSiblingElement("plugin")){std::string name=plugin->Attribute("name")?plugin->Attribute("name"):"plugin";std::string version=plugin->Attribute("version")?plugin->Attribute("version"):"";std::string tools;if(auto count=plugin->Attribute("tools"))tools=std::string(" · ")+count+" 个工具";if(plugin->BoolAttribute("ok",true))log("插件已加载："+name+(version.empty()?"":" "+version)+tools);else{std::string error=plugin->Attribute("error")?plugin->Attribute("error"):"未知错误";log("插件未加载："+name+" · "+error);}}pluginLogged=true;}
            updateModels(root->FirstChildElement("models"));
            if(configurationChanged)configurationChanged();
            if(testMode)apiStatus=reply;std::string outcome;if(!testMode)outcome=apply(bytes);if(!outcome.empty())log(outcome);log("AI："+reply);
            for(auto source=root->FirstChildElement("source");source;source=source->NextSiblingElement("source")){std::string text="来源：PubChem CID ";text+=source->Attribute("cid")?source->Attribute("cid"):"";text+=" · ";text+=source->Attribute("formula")?source->Attribute("formula"):"";text+="\r\n";text+=source->Attribute("source")?source->Attribute("source"):"";log(text);}
            if(!testMode){history.push_back({"user",pendingPrompt});history.push_back({"assistant",reply});while(history.size()>10)history.erase(history.begin());}
        }catch(const std::exception& e){if(testMode)apiStatus=std::string("连接未完成：")+e.what();log(std::string("本轮未应用：")+e.what());if(!testMode){try{auto bytes=chemservice::read(resultPath);tinyxml2::XMLDocument failed;if(failed.Parse(bytes.c_str())==tinyxml2::XML_SUCCESS&&failed.RootElement()&&std::string(failed.RootElement()->Name())=="result"&&failed.RootElement()->BoolAttribute("ok")&&failed.RootElement()->FirstChildElement("actions")&&failed.RootElement()->FirstChildElement("actions")->FirstChildElement()){auto saved=directory+L"\\last-unapplied-result.xml";chemservice::write(saved,bytes);log("失败结果已保留："+U(saved));}}catch(...){}if(get(711).empty())set(711,pendingPrompt);}}
        cleanup();
    }
    int dockWidth(int available,float scale)const{return std::min(int(332*scale),std::max(int(300*scale),int(available*.28)));}
    void hide(){stop();ShowWindow(hwnd,SW_HIDE);if(dockChanged)dockChanged();}
    void toggleDock(){
        if(!IsWindow(hwnd)||!IsWindow(owner)||paneLocked)return;
        if(!docked){
            WINDOWPLACEMENT place={};place.length=sizeof(place);GetWindowPlacement(hwnd,&place);floating=place.rcNormalPosition;
            ShowWindow(hwnd,SW_HIDE);SetWindowLongPtrW(hwnd,GWL_STYLE,WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS);
            SetWindowLongPtrW(hwnd,GWL_EXSTYLE,WS_EX_CONTROLPARENT);SetParent(hwnd,owner);docked=true;
            SetWindowPos(hwnd,0,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_FRAMECHANGED);ShowWindow(hwnd,SW_SHOW);
        }else{
            ShowWindow(hwnd,SW_HIDE);SetParent(hwnd,nullptr);SetWindowLongPtrW(hwnd,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN);
            SetWindowLongPtrW(hwnd,GWL_EXSTYLE,WS_EX_APPWINDOW|WS_EX_CONTROLPARENT);docked=false;
            if(floating.right<=floating.left){GetWindowRect(owner,&floating);floating.left+=60;floating.top+=50;floating.right=floating.left+640;floating.bottom=floating.top+720;}
            MONITORINFO mi={sizeof(mi)};GetMonitorInfoW(MonitorFromRect(&floating,MONITOR_DEFAULTTONEAREST),&mi);
            int width=std::min(int(floating.right-floating.left),int(mi.rcWork.right-mi.rcWork.left)),height=std::min(int(floating.bottom-floating.top),int(mi.rcWork.bottom-mi.rcWork.top));
            int x=std::max(int(mi.rcWork.left),std::min(int(floating.left),int(mi.rcWork.right)-width));int y=std::max(int(mi.rcWork.top),std::min(int(floating.top),int(mi.rcWork.bottom)-height));
            SetWindowPos(hwnd,HWND_TOP,x,y,width,height,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        }
        set(721,docked?"独立窗口":"停靠右侧");updateFont();layout();if(dockChanged)dockChanged();SetFocus(GetDlgItem(hwnd,711));
    }
    void layout(){
        if(!IsWindow(hwnd))return;RECT r;GetClientRect(hwnd,&r);int w=r.right,h=r.bottom;
        float scale=docked?dockScale:GetDpiForWindow(hwnd)/96.f;auto px=[&](int n){return int(n*scale);};
        auto move=[&](int id,int x,int y,int width,int height){MoveWindow(GetDlgItem(hwnd,id),x,y,std::max(1,width),std::max(1,height),TRUE);};
        int m=px(10);bool compact=h<px(420);int header=px(68),promptH=px(compact?54:70),promptY=h-px(compact?144:164);bool narrow=w<px(430);if(narrow)promptY-=px(28);
        move(720,w-m-px(176),px(8),px(52),px(28));move(726,w-m-px(120),px(8),px(64),px(28));move(722,w-m-px(52),px(8),px(52),px(28));
        move(721,w-m-px(82),px(38),px(82),px(24));move(723,m,px(39),w-2*m-px(90),px(22));
        chat.scale=scale;move(730,m,header,w-2*m,std::max(px(30),promptY-header-px(26)));move(710,m,header,w-2*m,std::max(px(30),promptY-header-px(26)));ShowWindow(GetDlgItem(hwnd,710),SW_HIDE);
        move(709,m,promptY-px(23),w-2*m,px(20));move(711,m,promptY,w-2*m,promptH);
        int attach=promptY+promptH+px(4);move(719,m,attach,px(86),px(24));move(725,m+px(90),attach,px(150),px(24));move(717,m+px(244),attach,px(48),px(24));move(724,m+px(296),attach,px(66),px(24));move(718,m+px(366),attach,px(44),px(24));
        if(narrow){move(717,m,attach+px(28),px(48),px(24));move(724,m+px(54),attach+px(28),px(66),px(24));move(718,m+px(126),attach+px(28),px(44),px(24));}
        int actions=h-px(58);move(714,m,actions,px(70),px(30));move(713,w-m-px(172),actions,px(64),px(30));move(712,w-m-px(100),actions,px(100),px(30));move(715,m,h-px(23),w-2*m,px(19));
        for(int id:{701,702,703,704,705,706,707,708,716})ShowWindow(GetDlgItem(hwnd,id),SW_HIDE);
        EnableWindow(GetDlgItem(hwnd,721),!paneLocked);bgValid=false;chat.reflow();if(!sizing)InvalidateRect(hwnd,0,FALSE);
    }
    void releaseBackground(){if(bgMem){if(bgOld)SelectObject(bgMem,bgOld);DeleteDC(bgMem);}if(bgDib)DeleteObject(bgDib);bgMem=nullptr;bgDib=nullptr;bgOld=nullptr;bgW=bgH=0;bgValid=false;}
    void renderBackground(const RECT& r){
        if(!bgMem||bgW!=r.right||bgH!=r.bottom){
            releaseBackground();
            BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            HDC screen=GetDC(nullptr);bgMem=CreateCompatibleDC(screen);ReleaseDC(nullptr,screen);
            void* bits=nullptr;bgDib=CreateDIBSection(bgMem,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            if(!bgDib){if(bgMem)DeleteDC(bgMem);bgMem=nullptr;return;}
            bgOld=SelectObject(bgMem,bgDib);bgW=r.right;bgH=r.bottom;
        }
        float dpi=docked?dockScale:GetDpiForWindow(hwnd)/96.f;Graphics g(bgMem);g.ScaleTransform(dpi,dpi);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        float w=r.right/dpi,h=r.bottom/dpi;SolidBrush bg(studio_ui::background());g.FillRectangle(&bg,0.f,0.f,w,h);
        RectF title(12,7,std::max(70.f,w-200),29);if(g.IsVisible(title))studio_ui::label(g,L"AI 助手",title,15,studio_ui::ink(),true);
        RECT box;GetWindowRect(GetDlgItem(hwnd,711),&box);MapWindowPoints(0,hwnd,(POINT*)&box,2);
        RectF prompt(box.left/dpi-2,box.top/dpi-2,(box.right-box.left)/dpi+4,(box.bottom-box.top)/dpi+4);
        if(g.IsVisible(prompt))studio_ui::round(g,prompt,8,Color(255,255,255,255));
        GdiFlush();bgValid=true;
    }
    void paint(){PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);RECT dirty=ps.rcPaint;
        if(dirty.right<=dirty.left||dirty.bottom<=dirty.top||r.right<=0||r.bottom<=0){EndPaint(hwnd,&ps);return;}
        if(bgLanguage!=studio_i18n::current()){bgValid=false;bgLanguage=studio_i18n::current();}
        if(!bgValid||!bgMem||bgW!=r.right||bgH!=r.bottom)renderBackground(r);
        if(bgMem)BitBlt(dc,dirty.left,dirty.top,dirty.right-dirty.left,dirty.bottom-dirty.top,bgMem,dirty.left,dirty.top,SRCCOPY);
        EndPaint(hwnd,&ps);
    }
    static LRESULT CALLBACK promptProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        auto self=(AiWindow*)data;if(m==WM_PASTE&&clipboardImageAvailable()){self->pasteImage();return 0;}
        if(m==WM_KEYDOWN&&w==VK_RETURN&&(GetKeyState(VK_CONTROL)&0x8000)){self->start(false);return 0;}
        if(m==WM_CHAR&&w==10)return 0;return DefSubclassProc(h,m,w,l);
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){auto self=(AiWindow*)GetWindowLongPtrW(h,GWLP_USERDATA);if(m==WM_NCCREATE){self=(AiWindow*)((CREATESTRUCTW*)l)->lpCreateParams;self->hwnd=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)self);}if(!self)return DefWindowProcW(h,m,w,l);
        switch(m){
        case WM_SIZE:self->layout();if(self->sizing)self->sizingChanged=true;return 0;
        // Dragging the frame only needs the controls to follow; text re-wrapping and a full
        // repaint are deferred to the end of the drag.
        case WM_ENTERSIZEMOVE:self->sizing=true;self->sizingChanged=false;self->chat.liveResize=true;return 0;
        case WM_EXITSIZEMOVE:{bool resized=self->sizingChanged;self->sizing=false;self->sizingChanged=false;self->chat.liveResize=false;if(resized)self->layout();}return 0;
        case WM_GETMINMAXINFO:{auto info=(MINMAXINFO*)l;UINT dpi=GetDpiForWindow(h);info->ptMinTrackSize={MulDiv(420,dpi?dpi:96,96),MulDiv(480,dpi?dpi:96,96)};return 0;}
        case WM_DPICHANGED:{if(!self->docked){auto r=(RECT*)l;SetWindowPos(h,0,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}self->updateFont();self->layout();return 0;}
        case WM_COMMAND:{int id=LOWORD(w);
            if(id==702&&HIWORD(w)==EN_CHANGE){auto current=self->get(704);studio_i18n::sendDlgItemMessage(h,704,CB_RESETCONTENT,0,0);studio_i18n::sendDlgItemMessage(h,704,CB_ADDSTRING,0,(LPARAM)L"auto");self->set(704,current.empty()?"auto":current);}
            else if(id==720){if(self->openSettings)self->openSettings();}else if(id==725){if(IsDlgButtonChecked(h,725)==BST_CHECKED){int answer=studio_i18n::messageBox(h,L"高级绘图通过多轮规划与校验，处理更复杂的绘图任务。\n\n· 完成时间更长，具体取决于任务复杂度\n· token 消耗高于普通绘图\n· 使用当前 AI 设置里的服务和模型\n\n是否启用？",L"高级绘图",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2);if(answer!=IDYES)CheckDlgButton(h,725,BST_UNCHECKED);}try{self->save();}catch(...){}}
            else if(id==726){if(!self->paneLocked&&!self->docked)self->toggleDock();self->paneLocked=!self->paneLocked;self->set(726,self->paneLocked?"已固定":"固定");self->layout();}
            else if(id==721)self->toggleDock();else if(id==722||id==IDCANCEL)self->hide();
            else if(id==716)self->start(true,true);else if(id==712)self->start(false);else if(id==708)self->start(true);else if(id==713)self->stop();else if(id==724)self->pasteImage();else if(id==717)self->chooseImage();else if(id==718)self->clearImage();
            else if(id==714){self->history.clear();self->set(710,"");self->clearImage();self->log("新对话\n描述结构、反应条件或排版要求，也可以上传图片作为参考。");}return 0;}
        case WM_TIMER:self->poll();return 0;case WM_CLOSE:self->hide();return 0;
        case WM_DESTROY:self->stop(false);self->hwnd=0;return 0;
        case WM_ERASEBKGND:return 1;case WM_PAINT:self->paint();return 0;
        case WM_DRAWITEM:if(studio_ui::button((DRAWITEMSTRUCT*)l,LOWORD(w)==712,false,self->docked?self->dockScale:GetDpiForWindow(h)/96.f))return TRUE;break;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:{SetTextColor((HDC)w,RGB(105,121,133));SetBkColor((HDC)w,RGB(245,248,249));static HBRUSH bg=CreateSolidBrush(RGB(245,248,249));return (LRESULT)bg;}
        }
        return DefWindowProcW(h,m,w,l);
    }
    void updateFont(){HFONT old=font;font=CreateFontW(-int(13*(docked?dockScale:GetDpiForWindow(hwnd)/96.f)),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");for(int i=701;i<=730;++i)studio_i18n::sendDlgItemMessage(hwnd,i,WM_SETFONT,(WPARAM)font,TRUE);
        for(int id:{710,711}){studio_i18n::sendDlgItemMessage(hwnd,id,EM_SETBKGNDCOLOR,0,RGB(255,255,255));studio_i18n::sendDlgItemMessage(hwnd,id,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(12,12));}
        if(old)DeleteObject(old);bgValid=false;
    }
    void show(HWND parent,const std::string& prompt="",bool settings=false,bool reveal=true){owner=parent;if(!IsWindow(hwnd)){
        LoadLibraryW(L"Msftedit.dll");WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(0);wc.hCursor=LoadCursorW(0,MAKEINTRESOURCEW(32512));wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1));wc.lpszClassName=L"MoleculeStudioAI";RegisterClassW(&wc);
        UINT dpi=GetDpiForWindow(parent);RECT rect;GetWindowRect(parent,&rect);
        MONITORINFO mi={};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST),&mi);int width=std::min(MulDiv(620,dpi,96),int(mi.rcWork.right-mi.rcWork.left)-24),height=std::min(MulDiv(740,dpi,96),int(mi.rcWork.bottom-mi.rcWork.top)-24);
        studio_i18n::createWindowEx(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,L"MoleculeStudioAI",L"Molecule Studio · AI 助手",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,mi.rcWork.left+(mi.rcWork.right-mi.rcWork.left-width)/2,mi.rcWork.top+(mi.rcWork.bottom-mi.rcWork.top-height)/2,width,height,nullptr,0,wc.hInstance,this);
        auto child=[&](const wchar_t* cls,const wchar_t* title,DWORD style,int id){auto c=studio_i18n::createWindowEx(0,cls,title,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,(HMENU)(INT_PTR)id,wc.hInstance,0);if(id==704)studio_i18n::protect(c);return c;};
        child(L"STATIC",L"API 地址",0,701);child(L"EDIT",L"https://api.deepseek.com",WS_TABSTOP|ES_AUTOHSCROLL,702);child(L"STATIC",L"模型",0,703);child(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWN|CBS_AUTOHSCROLL|WS_VSCROLL,704);studio_i18n::sendDlgItemMessage(hwnd,704,CB_ADDSTRING,0,(LPARAM)L"auto");set(704,"auto");studio_i18n::sendDlgItemMessage(hwnd,704,CB_LIMITTEXT,120,0);
        child(L"BUTTON",L"获取模型",WS_TABSTOP,716);child(L"STATIC",L"API Key",0,705);child(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|ES_PASSWORD,706);child(L"BUTTON",L"记住密钥",WS_TABSTOP|BS_AUTOCHECKBOX,707);child(L"BUTTON",L"测试连接",WS_TABSTOP,708);
        child(L"STATIC",L"",0,709);child(L"BUTTON",L"附画布截图",WS_TABSTOP|BS_AUTOCHECKBOX,719);child(L"BUTTON",L"高级绘图",WS_TABSTOP|BS_AUTOCHECKBOX,725);
        child(L"RICHEDIT50W",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,710);child(L"RICHEDIT50W",L"",WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,711);
        child(L"BUTTON",L"发送并绘制",WS_TABSTOP|BS_OWNERDRAW,712);child(L"BUTTON",L"取消",WS_TABSTOP|BS_OWNERDRAW,713);child(L"BUTTON",L"新对话",WS_TABSTOP|BS_OWNERDRAW,714);child(L"BUTTON",L"上传",WS_TABSTOP|BS_OWNERDRAW,717);child(L"BUTTON",L"粘贴图片",WS_TABSTOP|BS_OWNERDRAW,724);child(L"BUTTON",L"清除",WS_TABSTOP|BS_OWNERDRAW,718);child(L"STATIC",L"",0,715);
        chat.create(hwnd,730);child(L"BUTTON",L"固定",WS_TABSTOP|BS_OWNERDRAW,726);child(L"BUTTON",L"设置",WS_TABSTOP|BS_OWNERDRAW,720);child(L"BUTTON",L"停靠右侧",WS_TABSTOP|BS_OWNERDRAW,721);child(L"BUTTON",L"收起",WS_TABSTOP|BS_OWNERDRAW,722);child(L"STATIC",L"模型 · auto",0,723);
        SetWindowSubclass(GetDlgItem(hwnd,711),promptProc,1,(DWORD_PTR)this);studio_i18n::sendDlgItemMessage(hwnd,706,EM_SETLIMITTEXT,512,0);studio_i18n::sendDlgItemMessage(hwnd,711,EM_EXLIMITTEXT,0,6000);studio_i18n::sendDlgItemMessage(hwnd,710,EM_EXLIMITTEXT,0,200000);
        updateFont();load();layout();busy(false);CheckDlgButton(hwnd,719,BST_CHECKED);syncImageStatus();
        log("从一个想法开始\n\n绘制分子与反应式、整理多步流程，或按元素修改画布。\n例如：画出阿司匹林，在结构下方添加名称。\n\nAPI 与模型可在右上角“设置”中配置。");
        }
        if(!prompt.empty())set(711,prompt);
        if(reveal){ShowWindow(hwnd,docked?SW_SHOW:SW_SHOWNORMAL);if(dockChanged)dockChanged();if(!docked)SetForegroundWindow(hwnd);SetFocus(GetDlgItem(hwnd,711));}
        if(settings&&openSettings)openSettings();
    }
};
