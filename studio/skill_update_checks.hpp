#pragma once
// Task 9/10 user-facing checks: the AI drawing skill can be exported from Settings without
// any AI addon, and the bundled update downloader can only be started by an explicit click.
void App::skillUpdateCheck(const std::string& dir){
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    std::wstring base=W(dir),target=base+L"\\skill-export",log=base+L"\\updater-launch.txt";
    DeleteFileW(log.c_str());
    skillexport::removeTree(target);skillexport::ensureDirectory(target);
    SetEnvironmentVariableW(L"MOLECULE_STUDIO_SKILL_EXPORT_DIR",target.c_str());
    SetEnvironmentVariableW(L"MOLECULE_STUDIO_TEST_UPDATER",L"1");
    SetEnvironmentVariableW(L"MOLECULE_STUDIO_TEST_UPDATER_LOG",log.c_str());
    showSettings(1);
    auto& sw=settingsWindow;
    require(IsWindow(sw.hwnd),"settings window created");
    auto insideClient=[&](int id){
        RECT control,client;if(!GetDlgItem(sw.hwnd,id))return false;
        GetWindowRect(GetDlgItem(sw.hwnd,id),&control);MapWindowPoints(nullptr,sw.hwnd,(POINT*)&control,2);
        GetClientRect(sw.hwnd,&client);
        return control.left>=0&&control.top>=0&&control.right<=client.right+1&&control.bottom<=client.bottom+1;
    };
    require(IsWindowVisible(GetDlgItem(sw.hwnd,927))&&insideClient(927),"skill export button visible inside the AI page");
    require(IsWindowVisible(GetDlgItem(sw.hwnd,928))==FALSE,"updater button hidden outside its own page");
    require(GetFileAttributesW(log.c_str())==INVALID_FILE_ATTRIBUTES,"opening settings never starts the updater");
    SendMessageW(sw.hwnd,WM_COMMAND,MAKEWPARAM(927,BN_CLICKED),0);
    std::wstring exported=target+L"\\moleculestudio-draw\\SKILL.md";
    require(GetFileAttributesW(exported.c_str())!=INVALID_FILE_ATTRIBUTES,"skill export writes SKILL.md");
    auto source=skillexport::listFiles(skillexport::builtinSkillDir());
    auto copied=skillexport::listFiles(target+L"\\moleculestudio-draw");
    require(!source.empty()&&source.size()==copied.size(),"exported skill keeps every file");
    for(size_t i=0;i<source.size()&&i<copied.size();++i){
        require(source[i].first==copied[i].first,"exported skill keeps relative paths");
        std::string a,b;require(skillexport::sha256File(source[i].second,&a)&&skillexport::sha256File(copied[i].second,&b)&&a==b,"exported skill bytes match the built-in copy");
    }
    require(GetFileAttributesW(log.c_str())==INVALID_FILE_ATTRIBUTES,"exporting a skill does not start the updater");
    sw.select(8);
    require(sw.category==8,"update page can be selected");
    require(IsWindowVisible(GetDlgItem(sw.hwnd,928)),"updater button visible on its own page");
    require(insideClient(928)&&insideClient(983)&&insideClient(984),"update page controls stay inside small windows");
    require(GetFileAttributesW(log.c_str())==INVALID_FILE_ATTRIBUTES,"switching pages never starts the updater");
    SendMessageW(sw.hwnd,WM_COMMAND,MAKEWPARAM(928,BN_CLICKED),0);
    require(GetFileAttributesW(log.c_str())!=INVALID_FILE_ATTRIBUTES,"explicit updater click resolves the bundled downloader");
    {HANDLE file=CreateFileW(log.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
     require(file!=INVALID_HANDLE_VALUE,"updater log readable");
     DWORD size=GetFileSize(file,nullptr);std::string bytes(size,'\0');DWORD read=0;ReadFile(file,&bytes[0],size,&read,nullptr);CloseHandle(file);
     require(bytes.find("MoleculeStudio-OnlineSetup.exe")!=std::string::npos,"updater log points at the bundled installer");}
    SetEnvironmentVariableW(L"MOLECULE_STUDIO_SKILL_EXPORT_DIR",nullptr);
    SetEnvironmentVariableW(L"MOLECULE_STUDIO_TEST_UPDATER",nullptr);
    SetEnvironmentVariableW(L"MOLECULE_STUDIO_TEST_UPDATER_LOG",nullptr);
    sw.select(1);ShowWindow(sw.hwnd,SW_HIDE);
    std::ofstream(dir+"/skill-update-checks.json")<<"{\"skill_export\":true,\"skill_hash_matches\":true,\"updater_manual_only\":true,\"updater_path\":true,\"update_category\":true}";
}
