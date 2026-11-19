#ifndef STUDIO_APP_PATHS_HPP
#define STUDIO_APP_PATHS_HPP
// Every filesystem location the application reads or writes.
//
// A Microsoft Store / MSIX install lives in
// "C:\Program Files\WindowsApps\<package>\App", which is read-only for the
// signed-in user. User state (settings, caches, templates, logs) therefore must
// not be created next to the executable when the process runs packaged.
//
//   exeDir()        folder that holds the executable; shipped, read-only assets
//   dataDir()       writable folder for user state / caches
//   assetFile(n)    exeDir() + n     (read-only shipped asset)
//   dataFile(n)     dataDir() + n    (writable state / cache)
//   settingsFile(n) exeDir() + n when present, else dataFile(n); used for
//                   chemistry_backend.ini so a packaged user can still point the
//                   backend at another Python without editing the install.
//
// dataDir() resolution order:
//   1. %MOLECULE_STUDIO_DATA_DIR% (non-empty)     explicit override
//   2. packaged()  -> %LOCALAPPDATA%\MoleculeStudio  (created recursively)
//   3. otherwise   -> exeDir()                    portable layout, unchanged
//
// Only packaged() decides the default, so an unpackaged build keeps the
// historical portable behaviour byte for byte.
//
// Deliberately Win32 + std::wstring only: no <filesystem>, no -lstdc++fs, no new
// dependencies. Nothing here throws.
#include <windows.h>
#include <shlobj.h>
#include <string>

namespace apppaths {

// Creates path and every missing parent. Quietly returns false on failure; a
// read-only parent is not an error for callers that only need a best effort.
inline bool createDirectoryTree(const std::wstring& path){
    if(path.empty())return false;
    DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES)return (attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
    size_t slash=path.find_last_of(L"\\/");
    // Never recurse into a drive root such as "C:\" or a UNC share root.
    if(slash!=std::wstring::npos&&slash>1&&!(slash==2&&path[1]==L':'))createDirectoryTree(path.substr(0,slash));
    if(CreateDirectoryW(path.c_str(),nullptr))return true;
    attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
}

// Folder of the running executable. Cached: GetModuleFileNameW is a per-call
// syscall and several settings objects ask for the path while they construct.
inline std::wstring exeDir(){
    static const std::wstring cached=[](){
        wchar_t buffer[32768]={};
        DWORD length=GetModuleFileNameW(nullptr,buffer,DWORD(sizeof(buffer)/sizeof(buffer[0])));
        if(!length||length>=DWORD(sizeof(buffer)/sizeof(buffer[0])))return std::wstring();
        std::wstring path(buffer,length);
        size_t slash=path.find_last_of(L"\\/");
        return slash==std::wstring::npos?std::wstring():path.substr(0,slash);
    }();
    return cached;
}

#ifndef APPMODEL_ERROR_NO_PACKAGE
#define APPMODEL_ERROR_NO_PACKAGE 15700L
#endif
// True when the process runs inside an MSIX/Store package. GetCurrentPackageFullName
// is in kernel32 (Windows 8+); it is resolved dynamically so no import-library
// version can break the link, and its absence simply means "not packaged".
// ERROR_INSUFFICIENT_BUFFER / ERROR_SUCCESS => packaged;
// APPMODEL_ERROR_NO_PACKAGE (15700)        => not packaged.
inline bool packaged(){
    static const bool cached=[](){
        typedef LONG (WINAPI *GetCurrentPackageFullNameFn)(UINT32*,WCHAR*);
        HMODULE kernel=GetModuleHandleW(L"kernel32.dll");
        GetCurrentPackageFullNameFn query=kernel?reinterpret_cast<GetCurrentPackageFullNameFn>(
            reinterpret_cast<void*>(GetProcAddress(kernel,"GetCurrentPackageFullName"))):nullptr;
        if(!query)return false;
        UINT32 length=0;
        LONG result=query(&length,nullptr);
        if(result==ERROR_INSUFFICIENT_BUFFER||result==ERROR_SUCCESS)return true;
        return false;
    }();
    return cached;
}

inline std::wstring localAppDataDir(){
    wchar_t buffer[32768]={};
    DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,DWORD(sizeof(buffer)/sizeof(buffer[0])));
    if(length&&length<DWORD(sizeof(buffer)/sizeof(buffer[0])))return std::wstring(buffer,length);
    wchar_t folder[MAX_PATH]={};
    if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA|CSIDL_FLAG_CREATE,nullptr,SHGFP_TYPE_CURRENT,folder)))return std::wstring(folder);
    return std::wstring();
}

// Writable folder for user state. Resolved once; the folder tree is created on
// first use so callers never have to check for it.
inline std::wstring dataDir(){
    static const std::wstring cached=[]()->std::wstring{
        try{
            wchar_t configured[32768]={};
            DWORD length=GetEnvironmentVariableW(L"MOLECULE_STUDIO_DATA_DIR",configured,DWORD(sizeof(configured)/sizeof(configured[0])));
            if(length&&length<DWORD(sizeof(configured)/sizeof(configured[0]))){
                std::wstring path(configured,length);
                createDirectoryTree(path);
                return path;
            }
            if(packaged()){
                std::wstring local=localAppDataDir();
                if(!local.empty()){
                    std::wstring path=local+L"\\MoleculeStudio";
                    createDirectoryTree(path);
                    return path;
                }
            }
        }catch(...){}
        return exeDir();
    }();
    return cached;
}

// Writable state file: dataDir() + name.
inline std::wstring dataFile(const wchar_t* name){
    std::wstring dir=dataDir();
    if(!name||!*name)return dir;
    if(dir.empty())return std::wstring(name);
    return dir+L"\\"+name;
}

// Shipped asset next to the executable: exeDir() + name.
inline std::wstring assetFile(const wchar_t* name){
    std::wstring dir=exeDir();
    if(!name||!*name)return dir;
    if(dir.empty())return std::wstring(name);
    return dir+L"\\"+name;
}

// A shipped settings file that may legitimately live in dataDir() instead:
// chemistry_backend.ini ships read-only, yet a packaged user must be able to
// override it. Never created here - read-only lookup with a writable fallback.
inline std::wstring settingsFile(const wchar_t* name){
    std::wstring shipped=assetFile(name);
    if(!shipped.empty()){
        DWORD attributes=GetFileAttributesW(shipped.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY))return shipped;
    }
    return dataFile(name);
}

// Makes the resolved folder visible to every child process (python.exe workers)
// so the Python helpers write their caches into the same writable folder.
// Called once from WinMain; harmless when the variable is already set.
inline void applyToChildEnvironment(){
    std::wstring dir=dataDir();
    if(!dir.empty())SetEnvironmentVariableW(L"MOLECULE_STUDIO_DATA_DIR",dir.c_str());
}

} // namespace apppaths
#endif
