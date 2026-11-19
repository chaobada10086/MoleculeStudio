#include "cdx_binary.hpp"
#ifndef STUDIO_OLE_EMBED_HPP
#define STUDIO_OLE_EMBED_HPP
// Minimal out-of-place OLE document server core, modelled on the ChemDraw OLE
// behaviour documented in deliverables/ChemDraw_OLE_OLE_server_research.md:
//   * LocalServer32 + REGCLS_SINGLEUSE class object, -Embedding launch
//   * IPersistStorage reads/writes the CONTENTS stream
//   * "Embed Source" storage built with OleSave, so OLE writes \1Ole/\1CompObj
//   * EMF presentation through IViewObject2::Draw and clipboard formats
#include <windows.h>
#include <ole2.h>
#include <oleidl.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>

namespace ole {

static const CLSID kDocClsid = {0xC000BC8C,0x8C39,0x4722,{0xB9,0x3C,0xD4,0x0D,0x33,0x19,0xE7,0xB5}};
static const wchar_t kProgId[] = L"MoleculeStudio.Document.1";
static const wchar_t kProgIdVerInd[] = L"MoleculeStudio.Document";
static const wchar_t kUserType[] = L"Molecule Studio 2D Structure";
static const wchar_t kNativeFormat[] = L"MoleculeStudio.DocumentSelection.v2";
static const wchar_t kCdxmlFormat[] = L"ChemDraw XML";
static const wchar_t kContentsStream[] = L"CONTENTS";
static const wchar_t kPreviewStream[] = L"\1OlePres";

static void logLine(const char* text){
    wchar_t exe[32768]={};GetModuleFileNameW(nullptr,exe,32768);std::wstring file=exe;
    size_t slash=file.find_last_of(L"\\/");if(slash!=std::wstring::npos)file.resize(slash+1);else file.clear();
    file+=L"molecule_studio_ole.log";
    HANDLE h=CreateFileW(file.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    if(h==INVALID_HANDLE_VALUE)return;
    char line[2200];SYSTEMTIME st;GetLocalTime(&st);
    int n=snprintf(line,sizeof(line),"%02d:%02d:%02d.%03d %s\r\n",st.wHour,st.wMinute,st.wSecond,st.wMilliseconds,text);
    DWORD written=0;if(n>0)WriteFile(h,line,DWORD(n),&written,0);
    CloseHandle(h);
    OutputDebugStringA(line);
}
static void logf(const char* fmt,...){
    char buf[2048];va_list ap;va_start(ap,fmt);vsnprintf(buf,sizeof(buf),fmt,ap);va_end(ap);logLine(buf);
}
static void tracef(const char* fmt,...){
    static const bool enabled=[](){wchar_t value[2]={};return GetEnvironmentVariableW(L"MOLECULE_STUDIO_OLE_TRACE",value,2)>0;}();
    if(!enabled)return;
    char buf[2048];va_list ap;va_start(ap,fmt);vsnprintf(buf,sizeof(buf),fmt,ap);va_end(ap);logLine(buf);
}

inline std::wstring guidText(REFGUID g){wchar_t b[64]={};StringFromGUID2(g,b,64);return b;}

// The -Embedding server object loaded from Word has no m_preview of its own. The
// container asks it for CF_ENHMETAFILE during paste/cache initialization; without
// a factory the preview comes back null and Word inserts a blank, 0.45pt object.
inline std::function<HENHMETAFILE(const std::string&,SIZE)>& previewFactory(){static std::function<HENHMETAFILE(const std::string&,SIZE)> f;return f;}
inline void setPreviewFactory(std::function<HENHMETAFILE(const std::string&,SIZE)> f){previewFactory()=std::move(f);}

// ---------------------------------------------------------------- registry
inline bool setReg(HKEY root,const std::wstring& path,const wchar_t* name,const std::wstring& value){
    HKEY key=nullptr;
    LONG create=RegCreateKeyExW(root,path.c_str(),0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr);
    if(create!=ERROR_SUCCESS){logf("RegCreateKeyExW failed %ld path=%ls",(long)create,path.c_str());return false;}
    const wchar_t* data=value.empty()?L"":value.c_str();
    DWORD bytes=DWORD((value.size()+1)*sizeof(wchar_t));
    LONG rc=RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)data,bytes);
    RegCloseKey(key);if(rc!=ERROR_SUCCESS)logf("RegSetValueExW failed %ld name=%ls",(long)rc,name?name:L"(default)");
    return rc==ERROR_SUCCESS;
}
inline std::wstring exePath(){wchar_t b[32768]={};GetModuleFileNameW(nullptr,b,32768);return b;}
inline bool registerServer(const std::wstring& exe,bool unregister){
    std::wstring clsid=L"Software\\Classes\\CLSID\\"+guidText(kDocClsid);
    std::wstring prog=L"Software\\Classes\\"+std::wstring(kProgId);
    std::wstring progv=L"Software\\Classes\\"+std::wstring(kProgIdVerInd);
    HKEY root=HKEY_CURRENT_USER;
    if(unregister){
        RegDeleteTreeW(root,clsid.c_str());RegDeleteTreeW(root,prog.c_str());RegDeleteTreeW(root,progv.c_str());
        return true;
    }
    std::wstring server=L"\""+exe+L"\" -Embedding";
    std::wstring icon=L"\""+exe+L"\",0";
    bool ok=true;
    ok=setReg(root,clsid,nullptr,L"Molecule Studio Structure")&&ok;
    ok=setReg(root,clsid+L"\\AuxUserType\\2",nullptr,L"Molecule Studio 2D Structure")&&ok;
    ok=setReg(root,clsid+L"\\AuxUserType\\3",nullptr,L"Molecule Studio 2026")&&ok;
    ok=setReg(root,clsid+L"\\DefaultIcon",nullptr,icon)&&ok;
    ok=setReg(root,clsid+L"\\InprocHandler32",nullptr,L"ole32.dll")&&ok;
    ok=setReg(root,clsid+L"\\Insertable",nullptr,L"")&&ok;
    ok=setReg(root,clsid+L"\\LocalServer32",nullptr,server)&&ok;
    // COM passes this named value as CreateProcess's application name. An old
    // value overrides the executable in the command line even after an update.
    // Keep it unquoted, as required by the LocalServer32 registration contract.
    ok=setReg(root,clsid+L"\\LocalServer32",L"ServerExecutable",exe)&&ok;
    ok=setReg(root,clsid+L"\\ProgID",nullptr,kProgId)&&ok;
    ok=setReg(root,clsid+L"\\VersionIndependentProgID",nullptr,kProgIdVerInd)&&ok;
    ok=setReg(root,clsid+L"\\Verb\\0",nullptr,L"&Edit,0,2")&&ok;
    ok=setReg(root,clsid+L"\\Verb\\1",nullptr,L"&Open,-2,2")&&ok;
    ok=setReg(root,prog,nullptr,L"Molecule Studio 2D Structure")&&ok;
    ok=setReg(root,prog+L"\\CLSID",nullptr,guidText(kDocClsid))&&ok;
    ok=setReg(root,prog,L"Insertable",L"")&&ok;
    ok=setReg(root,prog,L"DefaultIcon",icon)&&ok;
    ok=setReg(root,prog+L"\\shell\\open\\command",nullptr,L"\""+exe+L"\" \"%1\"")&&ok;
    ok=setReg(root,prog+L"\\shell\\edit\\command",nullptr,server)&&ok;
    ok=setReg(root,progv,nullptr,L"Molecule Studio 2D Structure")&&ok;
    ok=setReg(root,progv+L"\\CLSID",nullptr,guidText(kDocClsid))&&ok;
    ok=setReg(root,progv+L"\\CurVer",nullptr,kProgId)&&ok;
    logf("registerServer exe=%ls ok=%d",exe.c_str(),ok?1:0);
    return ok;
}
inline bool registeredFor(const std::wstring& exe){
    const std::wstring key=L"Software\\Classes\\CLSID\\"+guidText(kDocClsid)+L"\\LocalServer32";
    wchar_t command[32768]={};DWORD bytes=sizeof(command);
    LONG rc=RegGetValueW(HKEY_CURRENT_USER,key.c_str(),nullptr,RRF_RT_REG_SZ,nullptr,command,&bytes);
    if(rc!=ERROR_SUCCESS||_wcsicmp(command,(L"\""+exe+L"\" -Embedding").c_str())!=0)return false;
    wchar_t application[32768]={};bytes=sizeof(application);
    rc=RegGetValueW(HKEY_CURRENT_USER,key.c_str(),L"ServerExecutable",RRF_RT_REG_SZ,nullptr,application,&bytes);
    return rc==ERROR_SUCCESS&&_wcsicmp(application,exe.c_str())==0;
}
inline bool ensureRegistered(){
    const auto exe=exePath();
    if(registeredFor(exe))return true;
    return registerServer(exe,false)&&registeredFor(exe);
}

// ---------------------------------------------------------------- helpers
inline std::string readContents(IStorage* stg){
    std::string data;
    if(!stg)return data;
    IStream* s=nullptr;
    if(FAILED(stg->OpenStream(kContentsStream,0,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&s)))return data;
    STATSTG stat={};s->Stat(&stat,STATFLAG_DEFAULT);
    if(stat.cbSize.QuadPart>0&&stat.cbSize.QuadPart<(1ull<<30)){
        data.resize((size_t)stat.cbSize.QuadPart);
        ULONG read=0;s->Read(&data[0],(ULONG)data.size(),&read);data.resize(read);
    }
    s->Release();return data;
}
// Containers marshal storages across processes per Write call; keep each call
// small so a large CONTENTS or presentation cannot overflow the RPC buffer.
inline bool writeStreamChunked(IStorage* stg,const wchar_t* name,const void* data,size_t size){
    if(!stg||(!data&&size))return false;
    IStream* s=nullptr;
    if(FAILED(stg->CreateStream(name,STGM_CREATE|STGM_READWRITE|STGM_SHARE_EXCLUSIVE,0,0,&s)))return false;
    bool ok=true;const char* p=(const char*)data;ULONG written=0;
    for(size_t at=0;ok&&at<size;){
        ULONG chunk=ULONG(std::min<size_t>(size-at,262144));
        ok=SUCCEEDED(s->Write(p+at,chunk,&written))&&written==chunk;
        at+=chunk;
    }
    if(ok)ok=SUCCEEDED(s->Commit(STGC_DEFAULT));
    s->Release();return ok;
}
inline bool writeContents(IStorage* stg,const std::string& data){
    if(!stg)return false;
    return writeStreamChunked(stg,kContentsStream,data.data(),data.size());
}
// GetWinMetaFileBits keeps the original EMF inside META_ESCAPE records, so the
// stored picture is a WMF for legacy readers yet stays vector-perfect for
// modern ones - byte-identical to what ole32's stock cache writes.
inline std::vector<unsigned char> wmfFromEmf(HENHMETAFILE emf){
    std::vector<unsigned char> out;
    if(!emf)return out;
    HDC dc=CreateCompatibleDC(nullptr);
    if(!dc)return out;
    UINT size=GetWinMetaFileBits(emf,0,nullptr,MM_ANISOTROPIC,dc);
    if(size){
        out.resize(size);
        UINT written=GetWinMetaFileBits(emf,size,out.data(),MM_ANISOTROPIC,dc);
        if(written!=size)out.clear();
    }
    DeleteDC(dc);
    return out;
}
// Bounded fallback for oversized vector presentations: play the EMF into a
// modest 24-bpp bitmap and store one StretchDIB record. The size is capped by
// construction (~1.2 MB), so container-side renderers never see a monster.
inline std::vector<unsigned char> wmfRasterFromEmf(HENHMETAFILE emf,SIZE extent){
    std::vector<unsigned char> out;
    if(!emf)return out;
    LONG w=extent.cx>0?extent.cx:5080,h=extent.cy>0?extent.cy:2540;
    int tw=1200;
    int th=int(double(tw)*double(h)/std::max<LONG>(1,w)+.5);
    if(th<60)th=60;
    const int maxPixels=390000; // ~1.17 MB at 24 bpp
    if(tw*th>maxPixels){double k=std::sqrt(double(maxPixels)/(tw*th));tw=int(tw*k);th=int(th*k);if(tw<160)tw=160;if(th<60)th=60;}
    BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=tw;bi.bmiHeader.biHeight=th;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=24;bi.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr);
    HDC dc=CreateCompatibleDC(screen);
    void* bits=nullptr;
    HBITMAP bmp=screen?CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
    std::vector<unsigned char> dib(size_t(tw)*size_t(th)*3);
    HDC mem=nullptr;
    if(dc&&bmp&&bits){
        HGDIOBJ old=SelectObject(dc,bmp);
        RECT rc={0,0,tw,th};
        HBRUSH bg=CreateSolidBrush(RGB(255,255,255));
        FillRect(dc,&rc,bg);DeleteObject(bg);
        RECT dst={0,0,tw,th};
        PlayEnhMetaFile(dc,emf,&dst);
        memcpy(dib.data(),bits,dib.size());
        SelectObject(dc,old);
        // Drawing into a bottom-up DIB section through a DC yields rows stored
        // bottom-first; the stored StretchDIB reads them flipped. Mirror once.
        {
            int stride=((tw*3+3)/4)*4;
            std::vector<unsigned char> row(stride);
            for(int y=0;y<th/2;++y){
                unsigned char* a=&dib[size_t(y)*stride];
                unsigned char* b=&dib[size_t(th-1-y)*stride];
                memcpy(row.data(),a,stride);memcpy(a,b,stride);memcpy(b,row.data(),stride);
            }
        }
        mem=CreateMetaFileW(nullptr);
    }
    if(screen)ReleaseDC(nullptr,screen);
    if(dc)DeleteDC(dc);
    if(bmp)DeleteObject(bmp);
    if(!mem)return out;
    SetMapMode(mem,MM_ANISOTROPIC);
    SetWindowOrgEx(mem,0,0,nullptr);SetViewportOrgEx(mem,0,0,nullptr);
    SetWindowExtEx(mem,w,h,nullptr);SetViewportExtEx(mem,tw,th,nullptr);
    SetStretchBltMode(mem,COLORONCOLOR);
    StretchDIBits(mem,0,0,tw,th,0,0,tw,th,dib.data(),&bi,DIB_RGB_COLORS,SRCCOPY);
    HMETAFILE wmf=CloseMetaFile(mem);
    if(wmf){
        UINT size=GetMetaFileBitsEx(wmf,0,nullptr);
        if(size){out.resize(size);UINT got=GetMetaFileBitsEx(wmf,size,out.data());if(got!=size)out.clear();}
        DeleteMetaFile(wmf);
    }
    return out;
}
// The \1Ole stream marks a storage as an OLE 2 embedded object ([MS-OLEDS]
// 2.3.3). OleSave/WriteFmtUserTypeStg do not write it; the server owns it.
inline bool writeStreamBytes(IStorage* stg,const std::vector<unsigned char>& data){
    IStream* s=nullptr;
    if(FAILED(stg->CreateStream(L"\1Ole",STGM_CREATE|STGM_READWRITE|STGM_SHARE_EXCLUSIVE,0,0,&s)))return false;
    ULONG written=0;bool ok=data.empty()||SUCCEEDED(s->Write(data.data(),(ULONG)data.size(),&written));
    s->Commit(STGC_DEFAULT);s->Release();return ok;
}
inline std::vector<unsigned char> oleLayout(int variant,REFCLSID clsid,const std::wstring& displayName){
    std::vector<unsigned char> b;
    auto put=[&](const void* p,size_t n){const unsigned char* c=(const unsigned char*)p;b.insert(b.end(),c,c+n);};
    auto put32=[&](DWORD v){put(&v,4);};
    DWORD version=(variant==6)?1u:0x02000001u;DWORD zero=0;LONG minusOne=-1;
    put32(version);put32(zero);put32(zero);put32(zero);put32(zero);
    if(variant==2)return b; // minimal
    put(&minusOne,4);put(&clsid,sizeof(CLSID));
    if(variant==3)return b;
    DWORD nameBytes=DWORD((displayName.size()+1)*sizeof(wchar_t));
    put32(variant==4?0u:nameBytes);
    if(variant!=4)put(displayName.c_str(),nameBytes);
    put32(zero);
    if(variant==5||variant==6){FILETIME ft={0,0};put(&ft,8);put(&ft,8);put(&ft,8);}
    return b;
}
inline bool writeOleStream(IStorage* stg,REFCLSID clsid,const std::wstring& displayName){
    return writeStreamBytes(stg,oleLayout(5,clsid,displayName));
}
inline IStorage* createMemoryStorage(){
    ILockBytes* lb=nullptr;
    HRESULT hr=CreateILockBytesOnHGlobal(nullptr,TRUE,&lb);
    if(FAILED(hr)){logf("CreateILockBytesOnHGlobal failed 0x%08lx",(unsigned long)hr);return nullptr;}
    IStorage* stg=nullptr;
    hr=StgCreateDocfileOnILockBytes(lb,STGM_CREATE|STGM_READWRITE|STGM_SHARE_EXCLUSIVE,0,&stg);
    lb->Release();
    if(FAILED(hr)){logf("StgCreateDocfileOnILockBytes failed 0x%08lx",(unsigned long)hr);return nullptr;}
    return stg;
}
inline HGLOBAL hglobalCopy(const void* data,size_t size){
    HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,size?size:1);
    if(!h)return nullptr;
    if(size){void* p=GlobalLock(h);if(!p){GlobalFree(h);return nullptr;}memcpy(p,data,size);GlobalUnlock(h);}
    return h;
}
inline HGLOBAL hglobalString(const std::string& s){return hglobalCopy(s.data(),s.size());}
inline HGLOBAL hglobalWide(const std::wstring& s){return hglobalCopy(s.c_str(),(s.size()+1)*sizeof(wchar_t));}

struct Host {
    virtual ~Host(){}
    virtual void activateDocument(class DocumentObject& obj)=0;
    virtual void deactivateDocument()=0;
    virtual void objectClosed()=0;
};
static Host* g_defaultHost=nullptr;
inline void setDefaultHost(Host* h){g_defaultHost=h;}

// ---------------------------------------------------------------- object
class DocumentObject : public IOleObject, public IDataObject, public IPersistStorage,
                       public IRunnableObject, public IExternalConnection, public IViewObject2 {
    LONG m_ref=1;
    IOleClientSite* m_site=nullptr;
    IStorage* m_stg=nullptr;
    IOleAdviseHolder* m_advise=nullptr;
    IDataAdviseHolder* m_dataAdvise=nullptr;
    IAdviseSink* m_viewSink=nullptr;
    DWORD m_viewAspects=0,m_viewAdvf=0;
    std::wstring m_hostApp,m_hostObj,m_userType;
    std::string m_contents;
    SIZE m_extent{0,0};
    CLIPFORMAT m_cfNative=0;
    bool m_dirty=false,m_closed=false,m_running=false;
    DWORD m_extConn=0;
    Host* m_host=nullptr;
    bool m_writeOle=true,m_writeOd=true;
    // Pure GDI vector records can also supply a WMF presentation. Its explicit
    // HIMETRIC extents avoid Office's monitor-dependent EMF size inference.
    unsigned m_mask=0xff;
    std::function<HENHMETAFILE(const std::string&,SIZE)> m_preview;
    std::function<std::string()> m_serialize,m_cdxml,m_text;
    std::string m_hostBaseline;
    bool m_legacyPresentation=false;
    HENHMETAFILE m_cachedEmf=nullptr;
    std::string m_cachedKey;
    HENHMETAFILE m_cachedContainerEmf=nullptr;
    std::string m_cachedContainerKey;
    // Cache the vector presentation by contents. Resizing changes the container
    // extent, not the resolution of text and paths stored in the EMF.
    std::string m_presKey;
    std::vector<unsigned char> m_presData;
    std::string keyFor(SIZE s)const{return std::to_string(s.cx)+"x"+std::to_string(s.cy)+":"+std::to_string(std::hash<std::string>()(m_contents));}
    void dropCache(){if(m_cachedEmf){DeleteEnhMetaFile(m_cachedEmf);m_cachedEmf=nullptr;}if(m_cachedContainerEmf){DeleteEnhMetaFile(m_cachedContainerEmf);m_cachedContainerEmf=nullptr;}m_cachedKey.clear();m_cachedContainerKey.clear();m_presKey.clear();m_presData.clear();}
    HENHMETAFILE previewFor(SIZE extent){
        std::string key=keyFor(extent);
        if(m_cachedEmf&&key==m_cachedKey)return CopyEnhMetaFileW(m_cachedEmf,nullptr);
        if(m_cachedEmf){DeleteEnhMetaFile(m_cachedEmf);m_cachedEmf=nullptr;}m_cachedKey.clear();
        HENHMETAFILE emf=m_preview?m_preview(m_contents,extent):nullptr;
        if(emf){m_cachedEmf=CopyEnhMetaFileW(emf,nullptr);m_cachedKey=key;}
        return emf;
    }
    // Live advisory updates also enter the container's stock OLE cache.
    // Give them the same physical-size calibration as the stored presentation.
    HENHMETAFILE containerPreviewFor(SIZE extent){
        const std::string key=keyFor(extent);
        if(m_cachedContainerEmf&&key==m_cachedContainerKey)return CopyEnhMetaFileW(m_cachedContainerEmf,nullptr);
        HENHMETAFILE emf=previewFor(extent);
        if(!emf)return nullptr;
        if(!m_legacyPresentation)return emf;
        IOleCache* cache=nullptr;IViewObject2* view=nullptr;
        HRESULT hr=CreateDataCache(nullptr,kDocClsid,IID_IOleCache,reinterpret_cast<void**>(&cache));
        FORMATETC format={CF_ENHMETAFILE,nullptr,DVASPECT_CONTENT,-1,TYMED_ENHMF};DWORD connection=0;
        if(SUCCEEDED(hr))hr=cache->Cache(&format,ADVF_PRIMEFIRST,&connection);
        STGMEDIUM medium={};medium.tymed=TYMED_ENHMF;medium.hEnhMetaFile=emf;
        if(SUCCEEDED(hr))hr=cache->SetData(&format,&medium,FALSE);
        if(SUCCEEDED(hr))hr=cache->QueryInterface(IID_IViewObject2,reinterpret_cast<void**>(&view));
        SIZEL actual={};if(SUCCEEDED(hr))hr=view->GetExtent(DVASPECT_CONTENT,-1,nullptr,&actual);
        if(SUCCEEDED(hr)&&actual.cx>0&&actual.cy>0&&extent.cx>0&&extent.cy>0&&
           (std::abs(actual.cx-extent.cx)>2||std::abs(actual.cy-extent.cy)>2)){
            SIZE calibrated={LONG(double(extent.cx)*extent.cx/actual.cx+.5),LONG(double(extent.cy)*extent.cy/actual.cy+.5)};
            // Calibration is a separate representation; do not evict the raw
            // preview or WMF when Office alternates between clipboard formats.
            HENHMETAFILE adjusted=m_preview?m_preview(m_contents,calibrated):nullptr;
            if(adjusted){DeleteEnhMetaFile(emf);emf=adjusted;}
        }
        if(view)view->Release();if(cache)cache->Release();
        if(m_cachedContainerEmf)DeleteEnhMetaFile(m_cachedContainerEmf);
        m_cachedContainerEmf=CopyEnhMetaFileW(emf,nullptr);m_cachedContainerKey=key;
        return emf;
    }
    bool ensurePresentation(SIZE extent){
        const std::string key=keyFor(extent);
        if(m_presKey==key&&!m_presData.empty())return true;
        HENHMETAFILE emf=previewFor(extent);
        auto bytes=wmfFromEmf(emf);if(emf)DeleteEnhMetaFile(emf);
        m_presData.swap(bytes);m_presKey=m_presData.empty()?std::string():key;
        return !m_presData.empty();
    }
    HENHMETAFILE iconFor(){
        HDC ref=GetDC(nullptr);if(!ref)return nullptr;
        RECT frame={0,0,32,32};
        HDC dc=CreateEnhMetaFileW(ref,nullptr,&frame,L"Molecule Studio\0\0");
        if(!dc){ReleaseDC(nullptr,ref);return nullptr;}
        HBRUSH bg=CreateSolidBrush(RGB(255,255,255));FillRect(dc,&frame,bg);DeleteObject(bg);
        POINT pts[6];for(int i=0;i<6;++i){double a=3.141592653589793*i/3.0;pts[i].x=16+int(11*cos(a));pts[i].y=16+int(11*sin(a));}
        HBRUSH fill=CreateSolidBrush(RGB(225,242,237));HPEN pen=CreatePen(PS_SOLID,2,RGB(24,122,108));
        HGDIOBJ oldBrush=SelectObject(dc,fill),oldPen=SelectObject(dc,pen);
        Polygon(dc,pts,6);
        SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(fill);DeleteObject(pen);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(24,122,108));SetTextAlign(dc,TA_CENTER|TA_BASELINE);
        LOGFONTW lf={};lf.lfHeight=-9;lf.lfWeight=FW_BOLD;wcscpy(lf.lfFaceName,L"Arial");
        HFONT font=CreateFontIndirectW(&lf);HGDIOBJ oldFont=SelectObject(dc,font);
        studio_i18n::textOut(dc,16,20,L"MS",2);
        SelectObject(dc,oldFont);DeleteObject(font);
        HENHMETAFILE emf=CloseEnhMetaFile(dc);ReleaseDC(nullptr,ref);return emf;
    }
    void collectFromHost(){
        if(!m_serialize)return;
        std::string now=m_serialize();
        if(now!=m_hostBaseline){m_hostBaseline=now;m_contents.swap(now);m_dirty=true;dropCache();}
    }
    HRESULT writeContainerStorage(IStorage* stg,bool sameAsLoad){
        if(!stg)return E_INVALIDARG;
        // ChemDraw's WriteTo(IStorage*) writes only CompObj + CONTENTS; the
        // container/OLE owns the \1Ole stream. Match that exactly.
        if(!m_userType.empty())WriteFmtUserTypeStg(stg,m_cfNative,const_cast<LPOLESTR>(m_userType.c_str()));
        if(!writeContents(stg,m_contents))return STG_E_ACCESSDENIED;
        // The container asks the server for CF_ENHMETAFILE before it calls
        // SetExtent, so the extent must travel inside the object storage too.
        {IStream* s=nullptr;if(SUCCEEDED(stg->CreateStream(L"MSXT",STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,0,&s))&&s){SIZEL sz=m_extent;ULONG written=0;s->Write(&sz,sizeof(sz),&written);s->Release();}}
        {IStream* stream=nullptr;if(SUCCEEDED(stg->CreateStream(L"MSPV",STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,0,&stream))&&stream){DWORD version=m_legacyPresentation?1:2;ULONG written=0;stream->Write(&version,sizeof(version),&written);stream->Release();}}
        if(m_contents.empty()){HRESULT emptyHr=stg->Commit(STGC_DEFAULT);if(SUCCEEDED(emptyHr)&&sameAsLoad)m_dirty=false;return emptyHr;}
        // Embed Source must carry a content presentation: the closed handler
        // shows this picture before the server ever runs. Write \2OlePres000
        // by hand - the stock CreateDataCache/IOleCache path returned
        // E_OUTOFMEMORY for large structures and killed the whole transfer,
        // so Word and WPS fell back to a static picture without edit verbs.
        // Layout verified against the Windows cache writer: 40-byte header
        // (including size) followed by WMF bits, even for clipboard format 14.
        HRESULT presHr=S_OK;
        SIZE extent=m_extent;if(extent.cx<=0||extent.cy<=0){extent.cx=5080;extent.cy=2540;}
        // Windows' stock OLE cache stores CF_ENHMETAFILE through a WMF wrapper
        // containing the original EMF in META_ESCAPE records. Raw EMF bits here
        // fail IPersistStorage::Load with E_HANDLE and Office rejects the object.
        // Keep both the original EMF and vector WMF fallback; never rasterize.
        if(!ensurePresentation(extent))presHr=E_FAIL;
        if(!m_presData.empty()){
            std::vector<unsigned char> stream;
            stream.reserve(44+m_presData.size());
            auto put32=[&](DWORD v){stream.insert(stream.end(),(const unsigned char*)&v,(const unsigned char*)&v+4);};
            put32(0xFFFFFFFFu);      // clipboard format marker of the stock writer
            put32(14);               // CF_ENHMETAFILE
            put32(4);                // empty DVTARGETDEVICE size
            put32(DVASPECT_CONTENT);
            put32(0xFFFFFFFFu);      // lindex
            put32(2);                // ADVF_PRIMEFIRST
            put32(0);                // reserved
            put32((DWORD)extent.cx);put32((DWORD)extent.cy);
            put32((DWORD)m_presData.size());
            stream.insert(stream.end(),m_presData.begin(),m_presData.end());
            if(!writeStreamChunked(stg,L"\2OlePres000",stream.data(),stream.size()))presHr=STG_E_WRITEFAULT;
        }
        logf("presentation vector hr=0x%08lx wmf=%zu extent=%ld,%ld",(unsigned long)presHr,m_presData.size(),(long)extent.cx,(long)extent.cy);
        // CONTENTS + CompObj make a valid, activatable object; a failed
        // presentation may cost the closed-state picture but never the
        // double-click editing, so it must not fail the transfer.
        stg->Commit(STGC_DEFAULT);
        if(sameAsLoad)m_dirty=false;
        return S_OK;
    }
public:
    DocumentObject(){m_userType=kUserType;m_cfNative=RegisterClipboardFormatW(kNativeFormat);logf("DocumentObject ctor %p",(void*)this);}
    virtual ~DocumentObject(){logf("DocumentObject dtor %p",(void*)this);if(m_dataAdvise)m_dataAdvise->Release();dropCache();if(m_site)m_site->Release();if(m_stg)m_stg->Release();if(m_advise)m_advise->Release();if(m_viewSink)m_viewSink->Release();}
    void setHost(Host* h){m_host=h;}
    void setWriteOle(bool value){m_writeOle=value;}
    void setWriteObjectDescriptor(bool value){m_writeOd=value;}
    void setFormatMask(unsigned mask){m_mask=mask;}
    Host* activeHost(){return m_host?m_host:g_defaultHost;}
    void setHostNames(const std::wstring& a,const std::wstring& o){m_hostApp=a;m_hostObj=o;}
    void setNativeFormat(CLIPFORMAT cf){m_cfNative=cf;}
    void setContents(std::string data,bool dirty=true){m_contents.swap(data);m_dirty=dirty;dropCache();}
    const std::string& contents()const{return m_contents;}
    bool dirty()const{return m_dirty;}
    SIZE extent()const{return m_extent;}
    void setExtentHimetric(SIZE s){if(s.cx>0&&s.cy>0){m_extent=s;}}
    void setPreview(std::function<HENHMETAFILE(const std::string&,SIZE)> fn){m_preview=fn;}
    void setUserType(const std::wstring& text){m_userType=text;}
    void markDirty(){m_dirty=true;}
    void setPreviewDirty(){dropCache();}
    void setSerializers(std::function<std::string()> native,std::function<std::string()> cdxml,std::function<std::string()> text){m_serialize=native;m_cdxml=cdxml;m_text=text;m_hostBaseline=m_serialize?m_serialize():std::string();}
    void clearSerializers(){m_serialize=nullptr;m_cdxml=nullptr;m_text=nullptr;m_hostBaseline.clear();}
    bool saveNow(){
        collectFromHost();
        // Publish the new presentation before the container saves its cache.
        notifyViewChange();
        HRESULT hr=m_site?m_site->SaveObject():(m_stg?writeContainerStorage(m_stg,true):E_FAIL);
        if(SUCCEEDED(hr)&&m_advise)m_advise->SendOnSave();
        return SUCCEEDED(hr);
    }
    void notifyViewChange(){
        dropCache();
        if(m_dataAdvise)m_dataAdvise->SendOnDataChange(static_cast<IDataObject*>(this),0,0);
        if(m_viewSink)m_viewSink->OnViewChange(DVASPECT_CONTENT,-1);
    }
    bool requestCloseFromEditor(){
        collectFromHost();
        if(m_dirty&&!saveNow())return false;
        if(m_site)m_site->OnShowWindow(FALSE);
        if(activeHost())activeHost()->deactivateDocument();
        clearSerializers();m_closed=true;m_running=false;
        if(m_advise)m_advise->SendOnClose();
        return true;
    }
    bool useStorage(IStorage* stg){if(m_stg)m_stg->Release();m_stg=stg;if(m_stg)m_stg->AddRef();return m_stg!=nullptr;}

    // ---- IUnknown
    STDMETHODIMP QueryInterface(REFIID riid,void** ppv)override{
        if(!ppv)return E_POINTER;*ppv=nullptr;
        if(IsEqualIID(riid,IID_IUnknown)||IsEqualIID(riid,IID_IOleObject))*ppv=static_cast<IOleObject*>(this);
        else if(IsEqualIID(riid,IID_IDataObject))*ppv=static_cast<IDataObject*>(this);
        else if(IsEqualIID(riid,IID_IPersistStorage)||IsEqualIID(riid,IID_IPersist))*ppv=static_cast<IPersistStorage*>(this);
        else if(IsEqualIID(riid,IID_IRunnableObject))*ppv=static_cast<IRunnableObject*>(this);
        else if(IsEqualIID(riid,IID_IExternalConnection))*ppv=static_cast<IExternalConnection*>(this);
        else if(IsEqualIID(riid,IID_IViewObject2))*ppv=static_cast<IViewObject2*>(this);
        else if(IsEqualIID(riid,IID_IViewObject))*ppv=static_cast<IViewObject*>(static_cast<IViewObject2*>(this));
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef()override{LONG r=InterlockedIncrement(&m_ref);tracef("AddRef -> %ld",(long)r);return ULONG(r);}
    STDMETHODIMP_(ULONG) Release()override{LONG r=InterlockedDecrement(&m_ref);tracef("Release -> %ld",(long)r);if(!r){delete this;return 0;}return ULONG(r);}

    // ---- IOleObject
    STDMETHODIMP SetClientSite(IOleClientSite* site)override{if(m_site)m_site->Release();m_site=site;if(m_site)m_site->AddRef();return S_OK;}
    STDMETHODIMP GetClientSite(IOleClientSite** out)override{if(!out)return E_POINTER;*out=m_site;if(*out)(*out)->AddRef();return S_OK;}
    STDMETHODIMP SetHostNames(LPCOLESTR app,LPCOLESTR obj)override{if(app)m_hostApp=app;if(obj)m_hostObj=obj;return S_OK;}
    STDMETHODIMP Close(DWORD opt)override{
        logf("IOleObject::Close opt=%lu",(unsigned long)opt);
        collectFromHost();
        if(opt==OLECLOSE_SAVEIFDIRTY||opt==OLECLOSE_PROMPTSAVE){if(m_dirty&&!saveNow())return E_FAIL;}
        if(activeHost())activeHost()->deactivateDocument();
        if(m_site)m_site->OnShowWindow(FALSE);
        clearSerializers();m_closed=true;m_running=false;
        if(m_advise)m_advise->SendOnClose();
        return S_OK;
    }
    STDMETHODIMP SetMoniker(DWORD,IMoniker*)override{return E_NOTIMPL;}
    STDMETHODIMP GetMoniker(DWORD,DWORD,IMoniker** pp)override{if(pp)*pp=nullptr;return E_NOTIMPL;}
    STDMETHODIMP InitFromData(IDataObject*,WINBOOL,DWORD)override{return E_NOTIMPL;}
    STDMETHODIMP GetClipboardData(DWORD,IDataObject** pp)override{if(pp)*pp=nullptr;return E_NOTIMPL;}
    STDMETHODIMP DoVerb(LONG verb,LPMSG,IOleClientSite*,LONG,HWND,LPCRECT)override{
        logf("IOleObject::DoVerb verb=%ld",(long)verb);
        if(verb==OLEIVERB_HIDE){if(activeHost())activeHost()->deactivateDocument();return S_OK;}
        if(verb==OLEIVERB_INPLACEACTIVATE)return OLEOBJ_S_INVALIDVERB;
        if(verb==OLEIVERB_DISCARDUNDOSTATE)return S_OK;
        if(verb==OLEIVERB_PRIMARY||verb==OLEIVERB_SHOW||verb==OLEIVERB_OPEN||verb==OLEIVERB_UIACTIVATE){
            m_running=true;m_closed=false;
            if(activeHost())activeHost()->activateDocument(*this);
            if(m_site)m_site->OnShowWindow(TRUE);
            return S_OK;
        }
        return OLEOBJ_S_INVALIDVERB;
    }
    STDMETHODIMP EnumVerbs(IEnumOLEVERB** out)override;
    STDMETHODIMP Update()override{return S_OK;}
    STDMETHODIMP IsUpToDate()override{return S_OK;}
    STDMETHODIMP GetUserClassID(CLSID* id)override{if(!id)return E_POINTER;*id=kDocClsid;return S_OK;}
    STDMETHODIMP GetUserType(DWORD form,LPOLESTR* out)override{
        if(!out)return E_POINTER;
        std::wstring text=m_userType;
        if(form==USERCLASSTYPE_SHORT)text=L"Molecule Studio";
        size_t bytes=(text.size()+1)*sizeof(wchar_t);
        *out=(LPOLESTR)CoTaskMemAlloc(bytes);if(!*out)return E_OUTOFMEMORY;
        memcpy(*out,text.c_str(),bytes);return S_OK;
    }
    STDMETHODIMP SetExtent(DWORD aspect,SIZEL* size)override{
        if(aspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;
        if(!size||size->cx<=0||size->cy<=0)return E_INVALIDARG;
        m_extent.cx=size->cx;m_extent.cy=size->cy;dropCache();
        if(m_viewSink)m_viewSink->OnViewChange(DVASPECT_CONTENT,-1);
        logf("SetExtent %ldx%ld",(long)m_extent.cx,(long)m_extent.cy);
        return S_OK;
    }
    STDMETHODIMP GetExtent(DWORD aspect,SIZEL* size)override{
        if(aspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;
        if(!size)return E_POINTER;
        collectFromHost();
        *size=m_extent;
        if(size->cx==0||size->cy==0){size->cx=5080;size->cy=2540;}
        return S_OK;
    }
    STDMETHODIMP Advise(IAdviseSink* sink,DWORD* conn)override{
        if(!m_advise){HRESULT hr=CreateOleAdviseHolder(&m_advise);if(FAILED(hr))return hr;}
        return m_advise->Advise(sink,conn);
    }
    STDMETHODIMP Unadvise(DWORD conn)override{return m_advise?m_advise->Unadvise(conn):OLE_E_NOCONNECTION;}
    STDMETHODIMP EnumAdvise(IEnumSTATDATA** out)override{return m_advise?m_advise->EnumAdvise(out):OLE_E_NOCONNECTION;}
    STDMETHODIMP GetMiscStatus(DWORD aspect,DWORD* status)override{
        if(!status)return E_POINTER;
        *status=(aspect==DVASPECT_CONTENT)?(OLEMISC_SETCLIENTSITEFIRST|OLEMISC_CANTLINKINSIDE|OLEMISC_RECOMPOSEONRESIZE):0;
        return S_OK;
    }
    STDMETHODIMP SetColorScheme(LOGPALETTE*)override{return S_OK;}

    // ---- IDataObject
    STDMETHODIMP GetData(FORMATETC* fmt,STGMEDIUM* medium)override;
    STDMETHODIMP GetDataHere(FORMATETC* fmt,STGMEDIUM* medium)override;
    STDMETHODIMP QueryGetData(FORMATETC* fmt)override;
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC* in,FORMATETC* out)override{if(out)*out=*in,out->ptd=nullptr;return DATA_S_SAMEFORMATETC;}
    STDMETHODIMP SetData(FORMATETC*,STGMEDIUM*,WINBOOL)override{return E_NOTIMPL;}
    STDMETHODIMP EnumFormatEtc(DWORD dir,IEnumFORMATETC** out)override;
    STDMETHODIMP DAdvise(FORMATETC* fmt,DWORD advf,IAdviseSink* sink,DWORD* conn)override{
        if(!m_dataAdvise){HRESULT hr=CreateDataAdviseHolder(&m_dataAdvise);if(FAILED(hr))return hr;}
        logf("IDataObject::DAdvise cf=%u advf=0x%lx",(unsigned)fmt->cfFormat,(unsigned long)advf);
        return m_dataAdvise->Advise(static_cast<IDataObject*>(this),fmt,advf,sink,conn);
    }
    STDMETHODIMP DUnadvise(DWORD conn)override{return m_dataAdvise?m_dataAdvise->Unadvise(conn):OLE_E_NOCONNECTION;}
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA** pp)override{return m_dataAdvise?m_dataAdvise->EnumAdvise(pp):OLE_E_NOCONNECTION;}

    // ---- IPersistStorage
    STDMETHODIMP GetClassID(CLSID* id)override{if(!id)return E_POINTER;*id=kDocClsid;return S_OK;}
    STDMETHODIMP IsDirty()override{collectFromHost();return m_dirty?S_OK:S_FALSE;}
    STDMETHODIMP InitNew(IStorage* stg)override{
        useStorage(stg);m_contents.clear();m_dirty=true;m_closed=false;dropCache();
        logf("IPersistStorage::InitNew");
        return S_OK;
    }
    STDMETHODIMP Load(IStorage* stg)override{
        useStorage(stg);m_contents=readContents(stg);m_dirty=false;dropCache();
        {IStream* s=nullptr;if(SUCCEEDED(stg->OpenStream(L"MSXT",0,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&s))&&s){SIZEL sz={};ULONG read=0;if(SUCCEEDED(s->Read(&sz,sizeof(sz),&read))&&read==sizeof(sz)&&sz.cx>0&&sz.cy>0)m_extent=sz;s->Release();}}
        m_legacyPresentation=true;
        {IStream* stream=nullptr;if(SUCCEEDED(stg->OpenStream(L"MSPV",0,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&stream))&&stream){DWORD version=0;ULONG got=0;stream->Read(&version,sizeof(version),&got);stream->Release();if(got==sizeof(version)&&version>=2)m_legacyPresentation=false;}}
        if(!m_preview)m_preview=previewFactory();
        logf("IPersistStorage::Load bytes=%zu",m_contents.size());
        return S_OK;
    }
    STDMETHODIMP Save(IStorage* stg,WINBOOL sameAsLoad)override{
        collectFromHost();
        HRESULT hr=writeContainerStorage(stg?stg:m_stg,sameAsLoad!=0);
        logf("IPersistStorage::Save same=%d hr=0x%08lx bytes=%zu",sameAsLoad?1:0,(unsigned long)hr,m_contents.size());
        return hr;
    }
    STDMETHODIMP SaveCompleted(IStorage* stgNew)override{
        if(stgNew)useStorage(stgNew);
        m_dirty=false;return S_OK;
    }
    STDMETHODIMP HandsOffStorage()override{if(m_stg){m_stg->Release();m_stg=nullptr;}return S_OK;}

    // ---- IRunnableObject
    STDMETHODIMP GetRunningClass(LPCLSID id)override{if(id)*id=kDocClsid;return S_OK;}
    STDMETHODIMP Run(LPBINDCTX)override{m_running=true;return S_OK;}
    STDMETHODIMP_(WINBOOL) IsRunning()override{return m_running?TRUE:FALSE;}
    STDMETHODIMP LockRunning(WINBOOL,WINBOOL)override{return S_OK;}
    STDMETHODIMP SetContainedObject(WINBOOL)override{return S_OK;}

    // ---- IExternalConnection
    STDMETHODIMP_(DWORD) AddConnection(DWORD,DWORD)override{DWORD n=++m_extConn;tracef("AddConnection -> %lu",(unsigned long)n);return n;}
    STDMETHODIMP_(DWORD) ReleaseConnection(DWORD type,DWORD,WINBOOL last)override{
        if(m_extConn)m_extConn--;
        tracef("ReleaseConnection type=%lu left=%lu last=%d",(unsigned long)type,(unsigned long)m_extConn,last?1:0);
        return m_extConn;
    }
    DWORD externalConnections()const{return m_extConn;}

    // ---- IViewObject(2)
    STDMETHODIMP Draw(DWORD aspect,LONG,void*,DVTARGETDEVICE*,HDC,HDC hdc,LPCRECTL bounds,LPCRECTL,BOOL (STDMETHODCALLTYPE*)(ULONG_PTR),ULONG_PTR)override;
    STDMETHODIMP GetColorSet(DWORD,LONG,void*,DVTARGETDEVICE*,HDC,LOGPALETTE** out)override{if(out)*out=nullptr;return S_FALSE;}
    STDMETHODIMP Freeze(DWORD,LONG,void*,DWORD* out)override{if(out)*out=0;return E_NOTIMPL;}
    STDMETHODIMP Unfreeze(DWORD)override{return E_NOTIMPL;}
    STDMETHODIMP SetAdvise(DWORD aspects,DWORD advf,IAdviseSink* sink)override{
        if(m_viewSink)m_viewSink->Release();m_viewSink=sink;if(m_viewSink)m_viewSink->AddRef();
        m_viewAspects=aspects;m_viewAdvf=advf;return S_OK;
    }
    STDMETHODIMP GetAdvise(DWORD* aspects,DWORD* advf,IAdviseSink** sink)override{
        if(aspects)*aspects=m_viewAspects;if(advf)*advf=m_viewAdvf;
        if(sink){*sink=m_viewSink;if(*sink)(*sink)->AddRef();}return S_OK;
    }
    STDMETHODIMP GetExtent(DWORD aspect,LONG,DVTARGETDEVICE*,LPSIZEL size)override{return GetExtent(aspect,size);}
};

// ------------------------------------------------------------ enumerators
class FormatEnum : public IEnumFORMATETC {
    LONG m_ref=1; std::vector<FORMATETC> m_items; size_t m_pos=0;
public:
    FormatEnum(const std::vector<FORMATETC>& items):m_items(items){}
    STDMETHODIMP QueryInterface(REFIID riid,void** ppv)override{
        if(!ppv)return E_POINTER;*ppv=nullptr;
        if(IsEqualIID(riid,IID_IUnknown)||IsEqualIID(riid,IID_IEnumFORMATETC))*ppv=static_cast<IEnumFORMATETC*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef()override{return ULONG(InterlockedIncrement(&m_ref));}
    STDMETHODIMP_(ULONG) Release()override{LONG r=InterlockedDecrement(&m_ref);if(!r){delete this;return 0;}return ULONG(r);}
    STDMETHODIMP Next(ULONG celt,FORMATETC* out,ULONG* fetched)override{
        ULONG n=0;
        while(n<celt&&m_pos<m_items.size()){out[n]=m_items[m_pos];if(out[n].ptd)out[n].ptd=nullptr;++m_pos;++n;}
        if(fetched)*fetched=n;
        for(ULONG i=0;i<n;++i)logf("FormatEnum::Next cf=%u tymed=0x%lx aspect=%lu lindex=%ld",(unsigned)out[i].cfFormat,(unsigned long)out[i].tymed,(unsigned long)out[i].dwAspect,(long)out[i].lindex);
        return n==celt?S_OK:S_FALSE;
    }
    STDMETHODIMP Skip(ULONG celt)override{m_pos+=celt;if(m_pos>m_items.size()){m_pos=m_items.size();return S_FALSE;}return S_OK;}
    STDMETHODIMP Reset()override{m_pos=0;return S_OK;}
    STDMETHODIMP Clone(IEnumFORMATETC** out)override{
        if(!out)return E_POINTER;
        FormatEnum* copy=new FormatEnum(m_items);copy->m_pos=m_pos;*out=copy;return S_OK;
    }
};

class VerbEnum : public IEnumOLEVERB {
    LONG m_ref=1; std::vector<OLEVERB> m_items; size_t m_pos=0;
public:
    VerbEnum(){
        OLEVERB edit={0,const_cast<LPOLESTR>(L"&Edit"),OLEVERBATTRIB_ONCONTAINERMENU,0};
        OLEVERB open={-2,const_cast<LPOLESTR>(L"&Open"),OLEVERBATTRIB_ONCONTAINERMENU,0};
        m_items.push_back(edit);m_items.push_back(open);
    }
    STDMETHODIMP QueryInterface(REFIID riid,void** ppv)override{
        if(!ppv)return E_POINTER;*ppv=nullptr;
        if(IsEqualIID(riid,IID_IUnknown)||IsEqualIID(riid,IID_IEnumOLEVERB))*ppv=static_cast<IEnumOLEVERB*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef()override{return ULONG(InterlockedIncrement(&m_ref));}
    STDMETHODIMP_(ULONG) Release()override{LONG r=InterlockedDecrement(&m_ref);if(!r){delete this;return 0;}return ULONG(r);}
    STDMETHODIMP Next(ULONG celt,LPOLEVERB out,ULONG* fetched)override{
        ULONG n=0;
        while(n<celt&&m_pos<m_items.size()){
            out[n]=m_items[m_pos];size_t bytes=(wcslen(out[n].lpszVerbName)+1)*sizeof(wchar_t);
            out[n].lpszVerbName=(LPOLESTR)CoTaskMemAlloc(bytes);
            if(!out[n].lpszVerbName)return E_OUTOFMEMORY;
            memcpy(out[n].lpszVerbName,m_items[m_pos].lpszVerbName,bytes);++n;++m_pos;
        }
        if(fetched)*fetched=n;
        return n==celt?S_OK:S_FALSE;
    }
    STDMETHODIMP Skip(ULONG celt)override{m_pos+=celt;if(m_pos>m_items.size()){m_pos=m_items.size();return S_FALSE;}return S_OK;}
    STDMETHODIMP Reset()override{m_pos=0;return S_OK;}
    STDMETHODIMP Clone(IEnumOLEVERB** out)override{
        if(!out)return E_POINTER;
        VerbEnum* copy=new VerbEnum();copy->m_pos=m_pos;*out=copy;return S_OK;
    }
};

// ------------------------------------------------------------ data formats
inline UINT fmtEmbedSource(){static UINT f=RegisterClipboardFormatW(L"Embed Source");return f;}
inline UINT fmtEmbeddedObject(){static UINT f=RegisterClipboardFormatW(L"Embedded Object");return f;}
inline UINT fmtObjectDescriptor(){static UINT f=RegisterClipboardFormatW(L"Object Descriptor");return f;}
inline UINT fmtNativeStorage(){static UINT f=RegisterClipboardFormatW(L"Native");return f;}
inline UINT fmtCdxml(){static UINT f=RegisterClipboardFormatW(kCdxmlFormat);return f;}

inline HGLOBAL objectDescriptorMedium(REFCLSID clsid,SIZE extent,const std::wstring& userType,const std::wstring& source){
    size_t userBytes=(userType.size()+1)*sizeof(wchar_t);
    size_t srcBytes=(source.size()+1)*sizeof(wchar_t);
    size_t offset=sizeof(OBJECTDESCRIPTOR);
    size_t total=offset+userBytes+srcBytes;
    HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,total);
    if(!h)return nullptr;
    unsigned char* p=(unsigned char*)GlobalLock(h);
    if(!p){GlobalFree(h);return nullptr;}
    OBJECTDESCRIPTOR od={};
    od.cbSize=(ULONG)total;
    od.clsid=clsid;
    od.dwDrawAspect=DVASPECT_CONTENT;
    od.sizel=extent;
    od.pointl.x=0;od.pointl.y=0;
    od.dwStatus=OLEMISC_SETCLIENTSITEFIRST|OLEMISC_CANTLINKINSIDE|OLEMISC_RECOMPOSEONRESIZE;
    od.dwFullUserTypeName=(DWORD)offset;
    od.dwSrcOfCopy=(DWORD)(offset+userBytes);
    memcpy(p,&od,sizeof(od));
    memcpy(p+offset,userType.c_str(),userBytes);
    memcpy(p+offset+userBytes,source.c_str(),srcBytes);
    GlobalUnlock(h);
    return h;
}
inline std::vector<FORMATETC> dataFormats(CLIPFORMAT native,unsigned mask=0x7f){
    std::vector<FORMATETC> v;
    auto add=[&v](CLIPFORMAT cf,LONG index,DWORD tymed){FORMATETC f={};f.cfFormat=cf;f.ptd=nullptr;f.dwAspect=DVASPECT_CONTENT;f.lindex=index;f.tymed=tymed;v.push_back(f);};
    // "Embedded Object" first: WPS's direct paste only creates a real object
    // from this format and silently falls back to a static picture when a
    // source app offers "Embed Source" alone. Word accepts either.
    if(mask&128)add(CLIPFORMAT(fmtEmbeddedObject()),-1,TYMED_ISTORAGE);
    if(mask&1)add(CLIPFORMAT(fmtEmbedSource()),-1,TYMED_ISTORAGE);
    if(mask&2)add(CLIPFORMAT(fmtObjectDescriptor()),-1,TYMED_HGLOBAL);
    // Prefer the enhanced vector representation. Legacy WMF has 16-bit
    // coordinates and some presentation consumers reduce its path precision.
    if(mask&8)add(CF_ENHMETAFILE,-1,TYMED_ENHMF);
    if(mask&4)add(CF_METAFILEPICT,-1,TYMED_MFPICT);
    if((mask&16)&&native)add(native,-1,TYMED_HGLOBAL);
    if(mask&32){add(CLIPFORMAT(fmtNativeStorage()),-1,TYMED_ISTORAGE|TYMED_HGLOBAL);add(CLIPFORMAT(cdxbinary::format()),-1,TYMED_HGLOBAL);add(CLIPFORMAT(fmtCdxml()),-1,TYMED_HGLOBAL);}
    if(mask&64)add(CF_UNICODETEXT,-1,TYMED_HGLOBAL);
    return v;
}

inline HMETAFILE emfToWmf(HENHMETAFILE emf){
    if(!emf)return nullptr;
    HDC ref=CreateCompatibleDC(nullptr);
    UINT size=GetWinMetaFileBits(emf,0,nullptr,MM_ANISOTROPIC,ref);
    HMETAFILE wmf=nullptr;
    if(size){
        std::vector<BYTE> bits(size);
        UINT written=GetWinMetaFileBits(emf,size,bits.data(),MM_ANISOTROPIC,ref);
        if(written)wmf=SetMetaFileBitsEx(written,bits.data());
    }
    DeleteDC(ref);return wmf;
}

inline std::wstring utf8ToWide(const std::string& s){
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),0,0);
    std::wstring w(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),&w[0],n);return w;
}
inline std::string wideToUtf8(const std::wstring& s){
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),0,0,0,0);
    std::string u(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),&u[0],n,0,0);return u;
}

// ------------------------------------------------------------ IDataObject impl
inline STDMETHODIMP DocumentObject::GetData(FORMATETC* fmt,STGMEDIUM* medium){
    if(!fmt||!medium)return E_INVALIDARG;
    logf("GetData cf=%u tymed=0x%lx aspect=%lu",(unsigned)fmt->cfFormat,(unsigned long)fmt->tymed,(unsigned long)fmt->dwAspect);
    medium->tymed=0;medium->hGlobal=nullptr;medium->pUnkForRelease=nullptr;
    collectFromHost();
    const bool icon=fmt->dwAspect==DVASPECT_ICON;
    if(fmt->dwAspect!=DVASPECT_CONTENT&&!icon)return DV_E_DVASPECT;
    UINT cf=fmt->cfFormat;
    if(!icon&&cf==fmtNativeStorage()&&(fmt->tymed&(TYMED_ISTORAGE|TYMED_HGLOBAL))){
        std::string xml=m_cdxml?m_cdxml():std::string(),bytes,why;
        if(xml.empty()||!cdxbinary::fromXml(xml,bytes,why))return DV_E_FORMATETC;
        ILockBytes* lb=nullptr;IStorage* storage=nullptr;
        HRESULT hr=CreateILockBytesOnHGlobal(nullptr,TRUE,&lb);
        if(FAILED(hr))return hr;
        hr=StgCreateDocfileOnILockBytes(lb,STGM_CREATE|STGM_READWRITE|STGM_SHARE_EXCLUSIVE,0,&storage);
        if(FAILED(hr)){lb->Release();return hr;}
        // Native ChemDraw storage needs its class/format metadata as well as
        // CONTENTS; without CompObj ChemDraw falls back to the vector picture.
        const CLSID chemClass={0x41ba6d21,0xa02e,0x11ce,{0x8f,0xd9,0x00,0x20,0xaf,0xd1,0xf2,0x0c}};
        hr=WriteClassStg(storage,chemClass);
        if(SUCCEEDED(hr))hr=WriteFmtUserTypeStg(storage,CLIPFORMAT(cdxbinary::format()),const_cast<LPOLESTR>(L"CS ChemDraw 64-bit Drawing"));
        if(FAILED(hr)){storage->Release();lb->Release();return hr;}
        if(!writeContents(storage,bytes)||FAILED(storage->Commit(STGC_DEFAULT))){storage->Release();lb->Release();return STG_E_WRITEFAULT;}
        if(fmt->tymed&TYMED_ISTORAGE){medium->tymed=TYMED_ISTORAGE;medium->pstg=storage;lb->Release();return S_OK;}
        STATSTG stat={};hr=lb->Stat(&stat,STATFLAG_NONAME);
        if(SUCCEEDED(hr)&&stat.cbSize.QuadPart<=128*1024*1024){
            HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,SIZE_T(stat.cbSize.QuadPart));
            void* p=h?GlobalLock(h):nullptr;ULONG read=0;ULARGE_INTEGER offset={};
            hr=p?lb->ReadAt(offset,p,ULONG(stat.cbSize.QuadPart),&read):E_OUTOFMEMORY;
            if(p)GlobalUnlock(h);
            if(SUCCEEDED(hr)&&read==stat.cbSize.QuadPart){medium->tymed=TYMED_HGLOBAL;medium->hGlobal=h;}
            else {if(h)GlobalFree(h);hr=STG_E_READFAULT;}
        }else if(SUCCEEDED(hr))hr=STG_E_MEDIUMFULL;
        storage->Release();lb->Release();return hr;
    }
    bool embedSource=!icon&&(cf==fmtEmbedSource()||cf==fmtEmbeddedObject());
    if(embedSource&&!(m_mask&(1|128)))return DV_E_FORMATETC;
    if(cf==fmtObjectDescriptor()&&!(m_mask&2))return DV_E_FORMATETC;
    if(embedSource&&(fmt->tymed&TYMED_ISTORAGE)){
        IStorage* stg=createMemoryStorage();if(!stg)return STG_E_MEDIUMFULL;
        // Mirror CwOleDocument::WriteTo(IStorage*): class + CompObj + CONTENTS.
        HRESULT hr=WriteClassStg(stg,kDocClsid);
        if(SUCCEEDED(hr))hr=writeContainerStorage(stg,false);
        if(SUCCEEDED(hr))hr=stg->Commit(STGC_DEFAULT);
        if(FAILED(hr)){stg->Release();logf("GetData EmbedSource failed 0x%08lx",(unsigned long)hr);return hr;}
        medium->tymed=TYMED_ISTORAGE;medium->pstg=stg;
        logf("GetData EmbedSource ok");
        return S_OK;
    }
    SIZE extent=m_extent;if(extent.cx<=0||extent.cy<=0){extent.cx=5080;extent.cy=2540;}
    if(cf==CF_ENHMETAFILE&&(fmt->tymed&TYMED_ENHMF)){
        HENHMETAFILE emf=icon?iconFor():containerPreviewFor(extent);
        logf("GetData ENHMF emf=%p",(void*)emf);
        if(!emf)return STG_E_MEDIUMFULL;
        medium->tymed=TYMED_ENHMF;medium->hEnhMetaFile=emf;return S_OK;
    }
    if(cf==CF_METAFILEPICT&&(fmt->tymed&TYMED_MFPICT)){
        HMETAFILE wmf=nullptr;
        if(icon){HENHMETAFILE emf=iconFor();if(emf){wmf=emfToWmf(emf);DeleteEnhMetaFile(emf);}}
        else if(ensurePresentation(extent))wmf=SetMetaFileBitsEx(UINT(m_presData.size()),m_presData.data());
        logf("GetData MFPICT wmf=%p",(void*)wmf);
        if(!wmf)return STG_E_MEDIUMFULL;
        HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(METAFILEPICT));
        if(!h){DeleteMetaFile(wmf);return STG_E_MEDIUMFULL;}
        METAFILEPICT* m=(METAFILEPICT*)GlobalLock(h);
        if(!m){GlobalFree(h);DeleteMetaFile(wmf);return STG_E_MEDIUMFULL;}
        m->mm=MM_ANISOTROPIC;m->xExt=extent.cx;m->yExt=extent.cy;m->hMF=wmf;
        GlobalUnlock(h);
        medium->tymed=TYMED_MFPICT;medium->hMetaFilePict=h;return S_OK;
    }
    if(!icon&&(fmt->tymed&TYMED_HGLOBAL)){
        if(cf==fmtObjectDescriptor()){
            if(!m_writeOd)return DV_E_FORMATETC;
            SIZE extent=m_extent;if(extent.cx<=0||extent.cy<=0){extent.cx=5080;extent.cy=2540;}
            medium->tymed=TYMED_HGLOBAL;medium->hGlobal=objectDescriptorMedium(kDocClsid,extent,m_userType,L"Molecule Studio");return S_OK;
        }
        if(m_cfNative&&cf==m_cfNative){
            medium->tymed=TYMED_HGLOBAL;medium->hGlobal=hglobalString(m_contents);return S_OK;
        }
        if(cf==cdxbinary::format()){
            std::string xml=m_cdxml?m_cdxml():std::string(),bytes,why;
            if(xml.empty()||!cdxbinary::fromXml(xml,bytes,why))return DV_E_FORMATETC;
            medium->tymed=TYMED_HGLOBAL;medium->hGlobal=hglobalString(bytes);return medium->hGlobal?S_OK:STG_E_MEDIUMFULL;
        }
        if(cf==fmtCdxml()){
            std::string xml=m_cdxml?m_cdxml():std::string();
            if(xml.empty())return DV_E_FORMATETC;
            medium->tymed=TYMED_HGLOBAL;medium->hGlobal=hglobalString(xml);return S_OK;
        }
        if(cf==CF_UNICODETEXT){
            if(!m_text)return DV_E_FORMATETC;
            std::wstring text=m_text?utf8ToWide(m_text()):L"";
            if(text.empty())return DV_E_FORMATETC;
            medium->tymed=TYMED_HGLOBAL;medium->hGlobal=hglobalWide(text);return S_OK;
        }
    }
    return DV_E_FORMATETC;
}
inline STDMETHODIMP DocumentObject::GetDataHere(FORMATETC* fmt,STGMEDIUM* medium){
    if(!fmt||!medium)return E_INVALIDARG;
    if((fmt->cfFormat==fmtEmbedSource()||fmt->cfFormat==fmtEmbeddedObject())&&(fmt->tymed&TYMED_ISTORAGE)&&medium->pstg){
        collectFromHost();
        HRESULT hr=WriteClassStg(medium->pstg,kDocClsid);
        if(SUCCEEDED(hr))hr=writeContainerStorage(medium->pstg,false);
        if(SUCCEEDED(hr))hr=medium->pstg->Commit(STGC_DEFAULT);
        logf("GetDataHere EmbedSource storage hr=0x%08lx",(unsigned long)hr);
        return hr;
    }
    if(!(fmt->tymed&TYMED_HGLOBAL)||!medium->hGlobal)return DV_E_FORMATETC;
    STGMEDIUM tmp={};HRESULT hr=GetData(fmt,&tmp);
    if(FAILED(hr))return hr;
    size_t size=GlobalSize(tmp.hGlobal);
    if(GlobalSize(medium->hGlobal)<size){ReleaseStgMedium(&tmp);return STG_E_MEDIUMFULL;}
    void* dst=GlobalLock(medium->hGlobal);void* src=GlobalLock(tmp.hGlobal);
    if(dst&&src)memcpy(dst,src,size);
    if(src)GlobalUnlock(tmp.hGlobal);if(dst)GlobalUnlock(medium->hGlobal);
    ReleaseStgMedium(&tmp);return (dst&&src)?S_OK:E_FAIL;
}
inline STDMETHODIMP DocumentObject::QueryGetData(FORMATETC* fmt){
    if(!fmt)return E_INVALIDARG;
    logf("QueryGetData cf=%u tymed=0x%lx",(unsigned)fmt->cfFormat,(unsigned long)fmt->tymed);
    const bool icon=fmt->dwAspect==DVASPECT_ICON;
    if(fmt->dwAspect!=DVASPECT_CONTENT&&!icon)return DV_E_DVASPECT;
    UINT cf=fmt->cfFormat;
    if(!icon&&(cf==fmtEmbedSource()||cf==fmtEmbeddedObject()))return !(m_mask&(1|128))?DV_E_FORMATETC:(fmt->tymed&TYMED_ISTORAGE)?S_OK:DV_E_TYMED;
    if(cf==CF_ENHMETAFILE)return (fmt->tymed&TYMED_ENHMF)?S_OK:DV_E_TYMED;
    if(cf==CF_METAFILEPICT)return (fmt->tymed&TYMED_MFPICT)?S_OK:DV_E_TYMED;
    if(m_cfNative&&cf==m_cfNative)return (fmt->tymed&TYMED_HGLOBAL)?S_OK:DV_E_TYMED;
    if(cf==fmtObjectDescriptor())return (m_mask&2)&&m_writeOd&&(fmt->tymed&TYMED_HGLOBAL)?S_OK:DV_E_FORMATETC;
    if(cf==fmtNativeStorage())return (m_cdxml&&(fmt->tymed&(TYMED_ISTORAGE|TYMED_HGLOBAL)))?S_OK:DV_E_FORMATETC;
    if(cf==fmtCdxml()||cf==cdxbinary::format())return (m_cdxml&&(fmt->tymed&TYMED_HGLOBAL))?S_OK:DV_E_FORMATETC;
    if(cf==CF_UNICODETEXT)return (m_text&&(fmt->tymed&TYMED_HGLOBAL))?S_OK:DV_E_FORMATETC;
    return DV_E_FORMATETC;
}
inline STDMETHODIMP DocumentObject::EnumFormatEtc(DWORD dir,IEnumFORMATETC** out){
    if(!out)return E_POINTER;*out=nullptr;
    logf("EnumFormatEtc dir=%lu",(unsigned long)dir);
    if(dir!=DATADIR_GET)return E_NOTIMPL;
    unsigned available=m_mask;
    if(!m_cdxml)available&=~32u;
    if(!m_text)available&=~64u;
    *out=new FormatEnum(dataFormats(m_cfNative,available));return S_OK;
}
inline STDMETHODIMP DocumentObject::EnumVerbs(IEnumOLEVERB** out){
    if(!out)return E_POINTER;*out=new VerbEnum();return S_OK;
}
inline STDMETHODIMP DocumentObject::Draw(DWORD aspect,LONG,void*,DVTARGETDEVICE*,HDC,HDC hdc,LPCRECTL bounds,LPCRECTL,BOOL (STDMETHODCALLTYPE*)(ULONG_PTR),ULONG_PTR){
    if(!hdc)return E_INVALIDARG;
    if(aspect!=DVASPECT_CONTENT)return DV_E_DVASPECT;
    collectFromHost();
    SIZE extent=m_extent;if(extent.cx<=0||extent.cy<=0){extent.cx=5080;extent.cy=2540;}
    RECTL rc=bounds?*bounds:RECTL{0,0,extent.cx,extent.cy};
    if(rc.right<=rc.left||rc.bottom<=rc.top)rc=RECTL{0,0,extent.cx,extent.cy};
    // Bounds are in the caller DC's logical coordinate space, not HIMETRIC.
    HENHMETAFILE emf=previewFor(extent);
    if(!emf)return E_FAIL;
    int saved=SaveDC(hdc);
    RECT dst={rc.left,rc.top,rc.right,rc.bottom};
    BOOL ok=PlayEnhMetaFile(hdc,emf,&dst);
    RestoreDC(hdc,saved);
    DeleteEnhMetaFile(emf);
    logf("Draw rc=%ld,%ld,%ld,%ld ok=%d",(long)rc.left,(long)rc.top,(long)rc.right,(long)rc.bottom,ok?1:0);
    return ok?S_OK:E_FAIL;
}

// ------------------------------------------------------------ class factory
class ClassFactory : public IClassFactory {
    LONG m_ref=1;
public:
    STDMETHODIMP QueryInterface(REFIID riid,void** ppv)override{
        if(!ppv)return E_POINTER;*ppv=nullptr;
        if(IsEqualIID(riid,IID_IUnknown)||IsEqualIID(riid,IID_IClassFactory))*ppv=static_cast<IClassFactory*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef()override{return ULONG(InterlockedIncrement(&m_ref));}
    STDMETHODIMP_(ULONG) Release()override{LONG r=InterlockedDecrement(&m_ref);if(!r){delete this;return 0;}return ULONG(r);}
    STDMETHODIMP CreateInstance(IUnknown* outer,REFIID riid,void** ppv)override{
        logf("ClassFactory::CreateInstance outer=%p riid=%ls",(void*)outer,guidText(riid).c_str());
        if(!ppv)return E_POINTER;*ppv=nullptr;
        if(outer)return CLASS_E_NOAGGREGATION;
        DocumentObject* obj=new DocumentObject();
        HRESULT hr=obj->QueryInterface(riid,ppv);
        obj->Release();
        logf("ClassFactory::CreateInstance hr=0x%08lx",(unsigned long)hr);
        return hr;
    }
    STDMETHODIMP LockServer(WINBOOL lock)override{logf("ClassFactory::LockServer %d",lock?1:0);return S_OK;}
};

inline HRESULT registerClassObject(DWORD* cookie){
    ClassFactory* factory=new ClassFactory();
    HRESULT hr=CoRegisterClassObject(kDocClsid,factory,CLSCTX_LOCAL_SERVER,REGCLS_SINGLEUSE,cookie);
    factory->Release();
    logf("CoRegisterClassObject clsid=%ls hr=0x%08lx cookie=%lu",guidText(kDocClsid).c_str(),(unsigned long)hr,(unsigned long)(cookie?*cookie:0));
    return hr;
}

} // namespace ole
#endif
