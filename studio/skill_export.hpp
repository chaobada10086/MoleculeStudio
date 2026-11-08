#pragma once
// Task 9/10 support: the AI drawing skill ships with Core and can be saved to a folder the
// user picks; the update downloader is only started from an explicit button click. Nothing in
// this file is called from startup, from opening settings, or from a timer: every entry point
// below requires a user action.
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <algorithm>

namespace skillexport {

struct Result {
    bool ok=false,cancelled=false;
    std::wstring target,error;
    int files=0,folders=0;
};

inline std::wstring executableDir(){
    wchar_t path[32768]={};DWORD length=GetModuleFileNameW(nullptr,path,32768);
    if(!length||length>=32768)return std::wstring();
    std::wstring value(path,length);size_t slash=value.find_last_of(L"\\/");
    return slash==std::wstring::npos?std::wstring():value.substr(0,slash);
}

// Built-in copy produced by studio/build.py next to the executable.
inline std::wstring builtinSkillDir(){
    std::wstring dir=executableDir();
    return dir.empty()?std::wstring():dir+L"\\skills\\moleculestudio-draw";
}

inline bool ensureDirectory(const std::wstring& path){
    if(path.empty())return false;
    DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES)return (attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
    size_t slash=path.find_last_of(L"\\/");
    if(slash!=std::wstring::npos&&slash>1&&!(slash==2&&path[1]==L':')){if(!ensureDirectory(path.substr(0,slash)))return false;}
    return CreateDirectoryW(path.c_str(),nullptr)!=FALSE||GetLastError()==ERROR_ALREADY_EXISTS;
}

inline bool removeTree(const std::wstring& path){
    DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES)return true;
    if(!(attributes&FILE_ATTRIBUTE_DIRECTORY))return DeleteFileW(path.c_str())!=FALSE;
    WIN32_FIND_DATAW data={};HANDLE find=FindFirstFileW((path+L"\\*").c_str(),&data);
    if(find!=INVALID_HANDLE_VALUE){
        do{
            std::wstring name=data.cFileName;
            if(name==L"."||name==L"..")continue;
            std::wstring child=path+L"\\"+name;
            if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!removeTree(child)){FindClose(find);return false;}}
            else if(!DeleteFileW(child.c_str())){FindClose(find);return false;}
        }while(FindNextFileW(find,&data));
        FindClose(find);
    }
    return RemoveDirectoryW(path.c_str())!=FALSE;
}

inline bool copyTree(const std::wstring& source,const std::wstring& target,int* files,int* folders,std::wstring* error){
    WIN32_FIND_DATAW data={};HANDLE find=FindFirstFileW((source+L"\\*").c_str(),&data);
    if(find==INVALID_HANDLE_VALUE){if(error)*error=L"无法读取源文件夹："+source;return false;}
    bool ok=true;
    do{
        std::wstring name=data.cFileName;
        if(name==L"."||name==L"..")continue;
        std::wstring from=source+L"\\"+name,to=target+L"\\"+name;
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
            if(!ensureDirectory(to)||!copyTree(from,to,files,folders,error)){ok=false;break;}
            if(folders)++*folders;
        }else{
            if(!CopyFileW(from.c_str(),to.c_str(),FALSE)){if(error)*error=L"无法复制文件："+from;ok=false;break;}
            if(files)++*files;
        }
    }while(FindNextFileW(find,&data));
    FindClose(find);
    return ok;
}

// Copy the built-in skill into <folder>\moleculestudio-draw. The target directory is rebuilt so
// "complete copy" stays true even when an older export is present.
inline Result exportTo(const std::wstring& source,const std::wstring& folder){
    Result result;
    if(source.empty()||GetFileAttributesW(source.c_str())==INVALID_FILE_ATTRIBUTES){
        result.error=L"内置 AI 绘图 Skill 不存在，请重新安装或重新构建："+(source.empty()?std::wstring(L"(空路径)"):source);return result;
    }
    if(GetFileAttributesW((source+L"\\SKILL.md").c_str())==INVALID_FILE_ATTRIBUTES){
        result.error=L"内置 AI 绘图 Skill 缺少 SKILL.md："+source;return result;
    }
    if(folder.empty()||GetFileAttributesW(folder.c_str())==INVALID_FILE_ATTRIBUTES){
        result.error=L"目标文件夹无效";return result;
    }
    DWORD attributes=GetFileAttributesW(folder.c_str());
    if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)){result.error=L"目标不是文件夹："+folder;return result;}
    result.target=folder+L"\\moleculestudio-draw";
    if(GetFileAttributesW(result.target.c_str())!=INVALID_FILE_ATTRIBUTES&&!removeTree(result.target)){
        result.error=L"无法清理旧的目标文件夹："+result.target;return result;
    }
    if(!ensureDirectory(result.target)){result.error=L"无法创建目标文件夹："+result.target;return result;}
    std::wstring error;
    if(!copyTree(source,result.target,&result.files,&result.folders,&error)){result.error=error.empty()?std::wstring(L"复制失败"):error;return result;}
    if(result.files<=0){result.error=L"内置 Skill 为空，未复制任何文件";return result;}
    result.ok=true;
    return result;
}

inline bool sha256File(const std::wstring& path,std::string* hex){
    if(!hex)return false;hex->clear();
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)){CloseHandle(file);return false;}
    if(!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)){CryptReleaseContext(provider,0);CloseHandle(file);return false;}
    BYTE buffer[65536];DWORD read=0;bool ok=true;
    while(ReadFile(file,buffer,sizeof(buffer),&read,nullptr)&&read){if(!CryptHashData(hash,buffer,read,0)){ok=false;break;}}
    if(ok){
        BYTE digest[32]={};DWORD length=32;
        if(CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0)){
            static const char* digits="0123456789abcdef";
            for(DWORD i=0;i<length;++i){hex->push_back(digits[digest[i]>>4]);hex->push_back(digits[digest[i]&15]);}
        }else ok=false;
    }
    CryptDestroyHash(hash);CryptReleaseContext(provider,0);CloseHandle(file);
    return ok;
}

// Relative/absolute pairs, sorted by relative path, for recursive verification.
inline std::vector<std::pair<std::wstring,std::wstring> > listFiles(const std::wstring& root,const std::wstring& relative=L""){
    std::vector<std::pair<std::wstring,std::wstring> > files;
    WIN32_FIND_DATAW data={};HANDLE find=FindFirstFileW((root+L"\\*").c_str(),&data);
    if(find==INVALID_HANDLE_VALUE)return files;
    do{
        std::wstring name=data.cFileName;
        if(name==L"."||name==L"..")continue;
        std::wstring rel=relative.empty()?name:relative+L"\\"+name;
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
            auto nested=listFiles(root+L"\\"+name,rel);
            files.insert(files.end(),nested.begin(),nested.end());
        }else files.push_back(std::make_pair(rel,root+L"\\"+name));
    }while(FindNextFileW(find,&data));
    FindClose(find);
    std::sort(files.begin(),files.end());
    return files;
}

// Folder picker for the export button. Only reached from that button's click handler.
inline bool chooseFolder(HWND owner,std::wstring* out){
    if(!out)return false;
    BROWSEINFOW info={};info.hwndOwner=owner;info.lpszTitle=L"选择 AI 绘图 Skill 的目标文件夹";
    info.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item=SHBrowseForFolderW(&info);
    if(!item)return false;
    wchar_t path[32768]={};bool ok=SHGetPathFromIDListW(item,path)!=FALSE;
    CoTaskMemFree(item);
    if(!ok||!path[0])return false;
    *out=path;
    return true;
}

// Starts the bundled update downloader. This function is reached only from the
// settings button click handler; no startup, timer, poll or show() path calls it.
inline bool launchUpdater(const std::wstring& exeDir,std::wstring* error){
    if(exeDir.empty()){if(error)*error=L"无法确定程序目录";return false;}
    std::wstring path=exeDir+L"\\MoleculeStudio-OnlineSetup.exe";
    if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){
        if(error)*error=L"未找到更新下载器，请重新安装完整核心包。";return false;
    }
    wchar_t test[8]={};GetEnvironmentVariableW(L"MOLECULE_STUDIO_TEST_UPDATER",test,8);
    if(test[0]){
        // Automated tests record the resolved path instead of starting a second installer.
        wchar_t logPath[32768]={};GetEnvironmentVariableW(L"MOLECULE_STUDIO_TEST_UPDATER_LOG",logPath,32768);
        if(logPath[0]){
            HANDLE file=CreateFileW(logPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE){
                std::string bytes;int size=WideCharToMultiByte(CP_UTF8,0,path.c_str(),-1,nullptr,0,nullptr,nullptr);
                if(size>1){bytes.resize(size_t(size-1));WideCharToMultiByte(CP_UTF8,0,path.c_str(),-1,&bytes[0],size,nullptr,nullptr);}
                DWORD written=0;WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr);CloseHandle(file);
            }
        }
        return true;
    }
    HINSTANCE result=ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,exeDir.c_str(),SW_SHOWNORMAL);
    if((INT_PTR)result<=32){if(error)*error=L"无法启动更新下载器，请检查程序目录权限。";return false;}
    return true;
}

} // namespace skillexport
