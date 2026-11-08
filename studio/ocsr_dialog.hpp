#pragma once
// Literature structure recognition (OCSR) dialog.
//
// The dialog is deliberately a thin client: the isolated Python worker does
// PDF rasterisation and MolScribe inference; this window only shows the source
// crop, the overlay and the model result, and passes a CDXML file to the
// editor after recording exactly which validation layers were executed.
// Nothing is uploaded and no layout is recomputed here.
// Standard library headers are provided by studio.cpp before its anonymous
// namespace opens; including them here (inside that namespace) would create
// {anonymous}::std and break libstdc++.

namespace ocsr {

static const UINT kTaskMessage = WM_APP + 90;
static const wchar_t* kWindowClass = L"MoleculeStudioOcsrDialog";

inline std::string xmlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (char c : value) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\r': break;
        case '\n': out += "&#10;"; break;
        default: out += c; break;
        }
    }
    return out;
}

struct TaskResult {
    enum Kind { Service = 1, Render = 2, Recognize = 3, Confirm = 4, Export = 5, Detect = 6 } kind = Service;
    bool ok = false;
    std::string xml;
    std::wstring error;
    std::wstring label;
};

class ServiceClient {
public:
    ~ServiceClient() { stop(); }

    bool start(const std::wstring& python, const std::wstring& script,
               const std::wstring& models, const std::wstring& cache,
               const std::wstring& records, const std::wstring& sitePackages,
               std::wstring& error) {
        stop();
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE childIn = nullptr, parentOut = nullptr;
        HANDLE childOut = nullptr, parentIn = nullptr;
        if (!CreatePipe(&childIn, &parentIn, &sa, 0) ||
            !CreatePipe(&parentOut, &childOut, &sa, 0)) {
            error = L"无法创建识别服务的管道";
            return false;
        }
        SetHandleInformation(parentIn, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(parentOut, HANDLE_FLAG_INHERIT, 0);
        // A freshly copied folder has no cache directory yet; create it before
        // opening the log so the worker's stderr is always captured instead of
        // being merged into the response pipe (a stray Python warning there
        // would corrupt the XML handshake on machines where it is emitted).
        CreateDirectoryW(cache.c_str(), nullptr);
        CreateDirectoryW(records.c_str(), nullptr);
        logPath = cache + L"\\service-error.log";
        HANDLE errorLog = CreateFileW(logPath.c_str(), GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (errorLog == INVALID_HANDLE_VALUE)
            errorLog = CreateFileW(L"NUL", GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (errorLog == INVALID_HANDLE_VALUE) errorLog = nullptr;
        if (errorLog) {
            // Record what was launched: on a fresh machine this is the first
            // thing needed to tell a blocked python.exe from a broken runtime.
            std::string header = "[ocsr] python=" + U(python) + "\r\n[ocsr] cache=" + U(cache) +
                                 "\r\n[ocsr] records=" + U(records) + "\r\n";
            DWORD ignored = 0;
            WriteFile(errorLog, header.data(), DWORD(header.size()), &ignored, nullptr);
        }

        std::wstring command = L"\"" + python + L"\" \"" + script + L"\" --serve --models \"" +
                               models + L"\" --cache \"" + cache + L"\" --records \"" + records + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = childIn;
        si.hStdOutput = childOut;
        si.hStdError = errorLog ? errorLog : childOut;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(0);
        // The uv/venv launcher depends on an absolute pyvenv.cfg that a copied
        // folder cannot always repair. Launch the bundled base interpreter and
        // point it at the venv site-packages via the child environment instead.
        std::vector<wchar_t> environment;
        if (!sitePackages.empty()) {
            wchar_t* current = GetEnvironmentStringsW();
            if (current) {
                for (wchar_t* item = current; *item; item += wcslen(item) + 1) {
                    if (_wcsnicmp(item, L"PYTHONPATH=", 11) == 0) continue;
                    if (_wcsnicmp(item, L"PYTHONHOME=", 11) == 0) continue;
                    environment.insert(environment.end(), item, item + wcslen(item) + 1);
                }
                FreeEnvironmentStringsW(current);
            }
            std::wstring pythonPath = L"PYTHONPATH=" + sitePackages;
            environment.insert(environment.end(), pythonPath.begin(), pythonPath.end());
            environment.push_back(0);
        }
        BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                      environment.empty() ? nullptr : environment.data(), nullptr, &si, &pi);
        CloseHandle(childIn);
        CloseHandle(childOut);
        if (errorLog) CloseHandle(errorLog);
        if (!created) {
            DWORD code = GetLastError();
            CloseHandle(parentIn);
            CloseHandle(parentOut);
            HANDLE tail = CreateFileW(logPath.c_str(), FILE_APPEND_DATA,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (tail != INVALID_HANDLE_VALUE) {
                std::string line = "[ocsr] CreateProcess failed, GetLastError=" +
                                   std::to_string(code) + "\r\n";
                DWORD ignored = 0;
                WriteFile(tail, line.data(), DWORD(line.size()), &ignored, nullptr);
                CloseHandle(tail);
            }
            error = L"无法启动本地识别服务（错误码 " + std::to_wstring(code) +
                    L"）。请确认 ocsr_runtime 完整，或杀毒软件没有拦截 python.exe。诊断日志：" + logPath;
            return false;
        }
        CloseHandle(pi.hThread);
        process = pi.hProcess;
        // Recognition saturates every core; keep the editor's own message loop ahead of it so
        // dragging and repainting the window stays smooth while a page is being processed.
        SetPriorityClass(process, BELOW_NORMAL_PRIORITY_CLASS);
        writeHandle = parentIn;
        readHandle = parentOut;
        return true;
    }

    std::string request(const std::string& xml, DWORD timeoutMs = 600000) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!process || !writeHandle || !readHandle) throw std::runtime_error("识别服务未启动");
        std::string line = xml;
        line.push_back('\n');
        DWORD written = 0;
        if (!WriteFile(writeHandle, line.data(), DWORD(line.size()), &written, nullptr) ||
            written != line.size()) {
            throw std::runtime_error("无法向识别服务发送请求；诊断日志：" + U(logPath));
        }
        std::string response;
        response.reserve(4096);
        ULONGLONG deadline = GetTickCount64() + timeoutMs;
        for (;;) {
            DWORD available = 0;
            if (!PeekNamedPipe(readHandle, nullptr, 0, nullptr, &available, nullptr)) {
                throw std::runtime_error("识别服务连接已断开；诊断日志：" + U(logPath));
            }
            if (available == 0) {
                if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
                    throw std::runtime_error("识别服务已退出；诊断日志：" + U(logPath));
                if (GetTickCount64() > deadline)
                    throw std::runtime_error("识别超时；可缩小选区或检查本地运行时。诊断日志：" + U(logPath));
                Sleep(15);
                continue;
            }
            char buffer[4096];
            DWORD got = 0;
            DWORD want = std::min<DWORD>(available, DWORD(sizeof(buffer)));
            if (!ReadFile(readHandle, buffer, want, &got, nullptr) || got == 0)
                throw std::runtime_error("无法读取识别服务响应");
            bool lineComplete = false;
            for (DWORD i = 0; i < got; ++i) {
                if (buffer[i] == '\n') { lineComplete = true; break; }
                response.push_back(buffer[i]);
                if (response.size() > 4 * 1024 * 1024)
                    throw std::runtime_error("识别服务响应过大");
            }
            if (lineComplete) {
                // Only '<'-prefixed lines are protocol responses; ignore any
                // stray stderr text instead of failing the request.
                std::string line = response;
                size_t start = line.find_first_not_of(" \t\r");
                if (start != std::string::npos && line[start] == '<')
                    return line.substr(start);
                response.clear();
            }
        }
    }

    void stop() {
        // Terminate before taking the lock: an in-flight request holds the
        // mutex while blocked in PeekNamedPipe/ReadFile.  Killing the child
        // makes that read fail and releases the mutex promptly, so closing the
        // dialog cannot hang until the 10-minute recognition timeout.
        if (process) TerminateProcess(process, 0);
        std::lock_guard<std::mutex> lock(mutex);
        if (process) {
            CloseHandle(process);
            process = nullptr;
        }
        if (writeHandle) { CloseHandle(writeHandle); writeHandle = nullptr; }
        if (readHandle) { CloseHandle(readHandle); readHandle = nullptr; }
    }

    bool alive() const { return process != nullptr; }

private:
    HANDLE process = nullptr;
    HANDLE writeHandle = nullptr;
    HANDLE readHandle = nullptr;
    std::wstring logPath;
    mutable std::mutex mutex;
};


// Independent editor session: recognition records and the main document stay
// untouched until the user explicitly copies or inserts the reviewed structure.
class ReviewCanvas {
public:
    std::unique_ptr<App> app;
    Session& session;
    HWND hwnd=nullptr;
    bool loaded=false;
    std::string baseline;
    std::function<void(Session&)> prepare;
    std::function<void()> changed,copy;
    ReviewCanvas():app(new App()),session(app->s){app->reviewEditor=true;}
    ~ReviewCanvas(){app->reviewChanged={};if(hwnd&&IsWindow(hwnd))DestroyWindow(hwnd);}
    void create(HWND parent){
        initializeReviewEditor(*app,parent);hwnd=app->h;session.whiteCanvas=true;
        app->reviewChanged=[this]{if(loaded&&changed)changed();};
    }
    std::string xml() const {std::string x;ocsr_edit::exportReview(session.doc,&x);return x;}
    bool modified() const {
        if(!loaded)return false;
        if(app->textEditor.active){std::string initial;for(const auto& run:app->textEditor.initial)initial+=run.utf8;
            if(app->textEditor.value()!=W(initial))return true;}
        return xml()!=baseline;
    }
    bool load(const std::string& path){
        if(app->textEditor.active)app->finishText(false);
        loaded=session.open(path).ok();
        if(loaded&&prepare){prepare(session);session.refresh();}
        session.whiteCanvas=true;baseline=xml();session.activateCommand("chooseSelectionTool_Marquee");
        app->textEditor.nextFamily=session.doc.defaultFont();app->textEditor.nextSize=session.doc.defaultTextSize();
        fit();ShowWindow(hwnd,loaded?SW_SHOW:SW_HIDE);return loaded;
    }
    void fit(){if(!hwnd)return;app->layout();if(loaded)session.fit();app->invalidate();}
    void notify(){if(hwnd)app->invalidate();else if(changed)changed();}
    void finishLabel(){if(app->textEditor.active)app->finishText(true);}
    void undo(bool redo=false){app->execute(Button("review.undo","","",redo?"@redo":"@undo"));}
    std::vector<ObjectId> bonds(){std::vector<ObjectId> ids;for(auto ref:session.selected)if(ref.kind==ObjectKind::Bond)ids.push_back(ref.id);return ids;}
    void tool(const std::string& command){
        finishLabel();auto ids=bonds();
        if(!ids.empty()&&(command.find("chooseBondTool_")==0||command.find("chooseMultipleBondsTool_")==0))
            session.submit(bonds::applyBondPresetCommand(ids,command),"review bond type");
        app->execute(Button("review.tool","","",command));notify();
    }
};

class Dialog {
public:
    using PrepareFn = std::function<void(Session&)>;
    using InsertFn = std::function<bool(const std::string& cdxml, const std::string& record,
                                        std::wstring& error)>;
    // 复制结构 hands the recognised CDXML back to the editor, which publishes it
    // with the very same formats as its own copy command (CDXML text plus the
    // EMF/metafile OLE payload); this window never touches the clipboard itself.
    using CopyFn = std::function<bool(const std::string& cdxml, std::wstring& error)>;

    static void show(HWND owner, InsertFn insert, CopyFn copy = CopyFn(), PrepareFn prepare = PrepareFn()) {
        auto* dialog = new Dialog(owner, std::move(insert), std::move(copy));
        dialog->editor.prepare=std::move(prepare);
        if (!dialog->create()) {
            delete dialog;
            studio_i18n::messageBox(owner, L"无法创建文献结构识别窗口。",
                        L"MoleculeStudio OCSR", MB_OK | MB_ICONERROR);
        }
    }

    // 图片转结构 entry point: open one raster image, treat the whole image as the
    // crop and recognise it immediately - no page turning, no dragging a box.
    static void showImage(HWND owner, InsertFn insert, CopyFn copy, const std::wstring& imagePath, PrepareFn prepare = PrepareFn()) {
        auto* dialog = new Dialog(owner, std::move(insert), std::move(copy));
        dialog->editor.prepare=std::move(prepare);
        dialog->imageMode = true;
        dialog->sourcePath = imagePath;
        dialog->sourceKind = L"image";
        dialog->quickPending = true;
        if (!dialog->create()) {
            delete dialog;
            studio_i18n::messageBox(owner, L"无法创建图片转结构窗口。",
                        L"MoleculeStudio 图片转结构", MB_OK | MB_ICONERROR);
        }
    }

    // Acceptance-test entry point: load a known PDF page and crop, then run
    // recognition on the real UI.  It is only reachable through the
    // MOLECULE_STUDIO_OCSR_DEMO environment variable.
    static void showDemo(HWND owner, InsertFn insert, const std::wstring& pdf, int page,
                         int x, int y, int w, int h, bool doInsert, PrepareFn prepare = PrepareFn()) {
        auto* dialog = new Dialog(owner, std::move(insert), CopyFn());
        dialog->editor.prepare=std::move(prepare);
        dialog->demoPdf = pdf;
        dialog->demoPage = page;
        dialog->demoX = x;
        dialog->demoY = y;
        dialog->demoW = w;
        dialog->demoH = h;
        dialog->demoInsert = doInsert;
        dialog->demoPending = true;
        if (!dialog->create()) {
            delete dialog;
            studio_i18n::messageBox(owner, L"无法创建文献结构识别窗口。",
                        L"MoleculeStudio OCSR", MB_OK | MB_ICONERROR);
        }
    }

private:
    enum ControlId {
        ID_FLIP=342, ID_ICON_FIRST=320, ID_SWAP_SOLID=340, ID_SWAP_HASH=341, ID_SOURCE=306, ID_TOOL=300, ID_CYCLE=301, ID_REVERSE=302, ID_UNDO=303, ID_REDO=304, ID_DETAILS=305,
        ID_PASTE = 106, ID_OPEN = 100, ID_PREV = 101, ID_NEXT = 102, ID_PAGE = 103, ID_GO = 104, ID_DPI = 105,
        ID_VECTOR = 107, ID_EXPORT = 108, ID_DETECT = 109,
        ID_RECOGNIZE = 200, ID_INSERT = 201, ID_CLOSE = 202, ID_COPY = 203,
        ID_MODE_SIDE = 210, ID_MODE_OVERLAY = 211, ID_SHOW_OVERLAY = 212,
        ID_OPACITY = 213, ID_ZOOM_OUT = 214, ID_ZOOM_IN = 215, ID_ZOOM_FIT = 216,
        ID_INFO = 220, ID_STATUS = 221, ID_CONF = 222, ID_PAGE_TOTAL = 223,
    };

    HWND owner = nullptr;
    HWND hwnd = nullptr;
    InsertFn insert;
    CopyFn copy;
    float scale = 1.0f;
    HFONT font = nullptr;
    HBRUSH whiteBrush = nullptr;
    std::atomic<bool> closing{false};
    bool busy = false;
    bool serviceReady = false;
    bool modelAvailable = false;
    bool pageLoaded = false;
    bool selecting = false;
    bool hasResult = false;
    bool insertable = false;
    bool overlayVisible = true;
    bool sideBySide = true;

    ReviewCanvas editor;
    bool details=false;
    HWND tooltips=nullptr;
    ServiceClient client;
    std::thread worker;
    HDC memoryDc = nullptr;
    HBITMAP memoryBitmap = nullptr;
    HGDIOBJ memoryPrevious = nullptr;
    int bufferWidth = 0, bufferHeight = 0;

    std::wstring appDir;
    std::wstring runtimeDir;
    std::wstring pythonPath;
    std::wstring sitePackages;
    std::wstring scriptPath;
    std::wstring modelsDir;
    std::wstring cacheDir;
    std::wstring recordsDir;

    std::wstring sourcePath;
    std::wstring sourceKind = L"pdf";
    std::wstring pageImagePath;
    std::wstring pendingExportPng;
    std::wstring pendingExportPdf;
    double vectorDpi = 0.0;
    int page = 1;
    int pageCount = 0;
    int pageWidth = 0;
    int pageHeight = 0;
    int dpi = 300;
    double pageScale = 1.0;
    RectF pageRect;

    std::unique_ptr<Bitmap> pageBitmap;
    std::unique_ptr<Bitmap> cropBitmap;
    std::unique_ptr<Bitmap> overlayBitmap;
    std::unique_ptr<Bitmap> blendBitmap;
    std::unique_ptr<Bitmap> resultBitmap;

    RectF pagePanel;
    RectF previewPanel;
    RectF previewRect;
    RectF infoRect;
    RectF statusRect;
    int opacity = 65;
    double previewZoom = 1.0;

    bool selectingActive = false;
    Point selectStart;
    Point selectCurrent;
    struct PixelRect { int left, top, right, bottom; };
    PixelRect selectionPx{0, 0, 0, 0};
    struct DetectedRegion { PixelRect rect; double score; };
    std::vector<DetectedRegion> detectedRegions;
    int pressedDetected = -1;

    std::string cdxmlPath;
    std::string recordPath;
    std::wstring warningsText;
    std::wstring statusText = L"正在启动本地识别服务…";
    std::wstring confidenceText = L"尚未识别";
    std::wstring stereoStatus;
    double confidence = -1.0;
    int atomCount = 0;
    int bondCount = 0;
    int fragmentCount = 0;
    bool imageMode = false;
    std::wstring quickImagePath;
    bool quickPending = false;
    std::wstring demoPdf;
    int demoPage = 1, demoX = 0, demoY = 0, demoW = 0, demoH = 0;
    bool demoInsert = false, demoPending = false;

    Dialog(HWND ownerWindow, InsertFn callback, CopyFn copier)
        : owner(ownerWindow), insert(std::move(callback)), copy(std::move(copier)) {}

    int S(int value) const { return int(std::lround(value * scale)); }
    float SF(float value) const { return value * scale; }

    bool create() {
        scale = owner ? GetDpiForWindow(owner) / 96.0f : 1.0f;
        if (scale < 0.5f) scale = 1.0f;
        font = CreateFontW(-MulDiv(9, int(scale * 96), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                           DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
        discoverPaths();

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Dialog::windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        RECT ownerRect{};
        if (owner) GetWindowRect(owner, &ownerRect);
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        int maxWidth = std::max(640, int(work.right - work.left) - 24);
        int maxHeight = std::max(480, int(work.bottom - work.top) - 24);
        // SPI_GETWORKAREA can be virtualised on high-DPI monitors; the screen
        // metrics keep the dialog on the physical desktop in that case.
        maxWidth = std::min(maxWidth, GetSystemMetrics(SM_CXSCREEN) - 24);
        maxHeight = std::min(maxHeight, GetSystemMetrics(SM_CYSCREEN) - 24);
        int width = std::min(S(1100), maxWidth);
        int height = std::min(S(760), maxHeight);
        int x = work.left + ((work.right - work.left) - width) / 2;
        int y = work.top + ((work.bottom - work.top) - height) / 2;
        hwnd = studio_i18n::createWindowEx(0, kWindowClass,
                               imageMode ? L"图片转结构 — 本地识别，不上传"
                                         : L"文献化学结构识别（OCSR） — 本地识别，不上传文献",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height,
                               owner, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd) return false;
        // Modeless: the canvas remains available while reviewing recognition.
        createControls();
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        layout();
        // Show the chosen picture immediately: the MolScribe weights can take
        // tens of seconds to load, and the user must never stare at an empty
        // panel while that happens.
        if (imageMode) loadQuickImage();
        startService();
        return true;
    }

    void discoverPaths() {
        wchar_t exe[32768]{};
        GetModuleFileNameW(nullptr, exe, 32768);
        appDir = exe;
        size_t slash = appDir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) appDir = appDir.substr(0, slash);
        wchar_t overrideDir[32768]{};
        DWORD length = GetEnvironmentVariableW(L"MOLECULE_STUDIO_OCSR_RUNTIME", overrideDir, 32768);
        runtimeDir = (length && length < 32768) ? std::wstring(overrideDir, length)
                                                                : appDir + L"\\ocsr_runtime";
        pythonPath = runtimeDir + L"\\venv\\Scripts\\python.exe";
        std::wstring basePython = runtimeDir + L"\\python\\cpython-3.10-windows-x86_64-none\\python.exe";
        if (GetFileAttributesW(basePython.c_str()) != INVALID_FILE_ATTRIBUTES) pythonPath = basePython;
        sitePackages = runtimeDir + L"\\venv\\Lib\\site-packages";
        scriptPath = appDir + L"\\ocsr_service.py";
        modelsDir = runtimeDir + L"\\models";
        // OpenCV (inside MolScribe) cannot read image files under non-ASCII paths on
        // Windows, so model input must live in an ASCII temp directory. Keep records
        // next to the application so the user can find review artifacts.
        wchar_t temporary[32768]{};
        DWORD temporaryLength = GetTempPathW(32768, temporary);
        cacheDir = (temporaryLength && temporaryLength < 32768)
                       ? std::wstring(temporary, temporaryLength) + L"MoleculeStudio-OCSR-cache"
                       : appDir + L"\\OCSRCache";
        recordsDir = appDir + L"\\recognition_records";
    }

    HWND createControl(const wchar_t* className, const wchar_t* text, DWORD style, int id) {
        HWND control = studio_i18n::createWindowEx(0, className, text, WS_CHILD | WS_VISIBLE | style,
                                       0, 0, 10, 10, hwnd, HMENU(UINT_PTR(id)),
                                       GetModuleHandleW(nullptr), nullptr);
        if (control && font) studio_i18n::sendMessage(control, WM_SETFONT, WPARAM(font), TRUE);
        if (control) SetWindowSubclass(control, &Dialog::controlProc, 1, DWORD_PTR(this));
        return control;
    }

    void createControls() {
        createControl(L"BUTTON", L"打开 PDF / 图片…", BS_PUSHBUTTON, ID_OPEN);
        createControl(L"BUTTON", L"粘贴剪贴板", BS_PUSHBUTTON, ID_PASTE);
        createControl(L"BUTTON", L"上一页", BS_PUSHBUTTON, ID_PREV);
        createControl(L"EDIT", L"1", ES_NUMBER | ES_CENTER | WS_BORDER, ID_PAGE);
        createControl(L"STATIC", L"/ 0 页", SS_CENTER, ID_PAGE_TOTAL);
        createControl(L"BUTTON", L"下一页", BS_PUSHBUTTON, ID_NEXT);
        createControl(L"BUTTON", L"转到", BS_PUSHBUTTON, ID_GO);
        HWND combo = createControl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, ID_DPI);
        for (const wchar_t* item : {L"150 DPI", L"200 DPI", L"300 DPI", L"400 DPI"})
            studio_i18n::sendMessage(combo, CB_ADDSTRING, 0, LPARAM(item));
        studio_i18n::sendMessage(combo, CB_SETCURSEL, 2, 0);
        createControl(L"BUTTON", L"矢量高清", BS_AUTOCHECKBOX | WS_GROUP, ID_VECTOR);
        studio_i18n::sendMessage(GetDlgItem(hwnd, ID_VECTOR), BM_SETCHECK, BST_CHECKED, 0);
        createControl(L"BUTTON", L"导出矢量裁剪…", BS_PUSHBUTTON, ID_EXPORT);

        createControl(L"BUTTON", L"自动检测", BS_PUSHBUTTON, ID_DETECT);
        createControl(L"BUTTON", L"识别选区", BS_PUSHBUTTON, ID_RECOGNIZE);
        createControl(L"BUTTON", L"插入画布", BS_PUSHBUTTON, ID_INSERT);
        createControl(L"BUTTON", L"复制结构", BS_PUSHBUTTON, ID_COPY);
        createControl(L"BUTTON", L"关闭", BS_PUSHBUTTON, ID_CLOSE);
        createControl(L"BUTTON", L"并排", BS_AUTORADIOBUTTON | WS_GROUP, ID_MODE_SIDE);
        createControl(L"BUTTON", L"叠加", BS_AUTORADIOBUTTON, ID_MODE_OVERLAY);
        createControl(L"BUTTON", L"显示叠加", BS_AUTOCHECKBOX, ID_SHOW_OVERLAY);
        studio_i18n::sendMessage(GetDlgItem(hwnd, ID_MODE_SIDE), BM_SETCHECK, BST_CHECKED, 0);
        studio_i18n::sendMessage(GetDlgItem(hwnd, ID_SHOW_OVERLAY), BM_SETCHECK, BST_CHECKED, 0);
        createControl(L"BUTTON", L"−", BS_PUSHBUTTON, ID_ZOOM_OUT);
        createControl(L"BUTTON", L"+", BS_PUSHBUTTON, ID_ZOOM_IN);
        createControl(L"BUTTON", L"适合", BS_PUSHBUTTON, ID_ZOOM_FIT);
        HWND track = createControl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_TOOLTIPS, ID_OPACITY);
        studio_i18n::sendMessage(track, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        studio_i18n::sendMessage(track, TBM_SETPOS, TRUE, opacity);
        // The review pane uses a flattened crop+overlay image so GDI+ and
        // transparent-PNG alpha cannot disagree.  Show/hide is the supported
        // opacity control; the old alpha slider is no longer exposed.
        ShowWindow(track, SW_HIDE);
        createControl(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL |
                                  WS_BORDER, ID_INFO);
        createControl(L"STATIC", L"", SS_LEFT, ID_CONF);
        createControl(L"STATIC", L"", SS_LEFT, ID_STATUS);
        createControl(L"BUTTON",L"重新框选",BS_PUSHBUTTON,ID_SOURCE);
        editor.create(hwnd);
        editor.changed=[this]{
            std::wstring text=confidenceText;
            if(editor.modified()){
                auto counts=editor.session.doc.stats();
                text=L"已修改 · 原子 "+std::to_wstring(counts.aliveAtoms)+L" · 键 "+std::to_wstring(counts.aliveBonds)+
                    L"\r\n以上校验与置信度仅针对原始识别结果；编辑后尚未重新校验。";
            }
            for(int i=0;i<9;++i)InvalidateRect(GetDlgItem(hwnd,ID_ICON_FIRST+i),nullptr,FALSE);
            studio_i18n::setWindowText(GetDlgItem(hwnd,ID_CONF),text.c_str());
            InvalidateRect(hwnd,nullptr,FALSE);
        };
        editor.copy=[this]{copyResult();};
        createControl(L"BUTTON",L"反向",BS_PUSHBUTTON,ID_REVERSE);
        createControl(L"BUTTON",L"撤销",BS_PUSHBUTTON,ID_UNDO);
        createControl(L"BUTTON",L"重做",BS_PUSHBUTTON,ID_REDO);
        createControl(L"BUTTON",L"详情",BS_PUSHBUTTON,ID_DETAILS);
        createControl(L"BUTTON",L"实楔 ↔ 粗实线（全部）",BS_PUSHBUTTON,ID_SWAP_SOLID);
        createControl(L"BUTTON",L"虚楔 ↔ 粗虚线（全部）",BS_PUSHBUTTON,ID_SWAP_HASH);
        createControl(L"BUTTON",L"构型翻转",BS_PUSHBUTTON,ID_FLIP);
        tooltips=studio_i18n::createWindowEx(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,
            CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
        const wchar_t* names[]={L"选择 / 拖动",L"基础单键",L"实楔",L"虚楔",L"粗实线",L"粗虚线（等宽短线）",L"双键",L"橡皮",L"文字"};
        for(int i=0;i<9;++i){HWND b=createControl(L"BUTTON",names[i],BS_OWNERDRAW,ID_ICON_FIRST+i);
            TOOLINFOW tip{};tip.cbSize=sizeof(tip);tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
            tip.hwnd=hwnd;tip.uId=UINT_PTR(b);tip.lpszText=const_cast<wchar_t*>(names[i]);studio_i18n::sendMessage(tooltips,TTM_ADDTOOLW,0,LPARAM(&tip));}
        refreshEnabled();
    }

    void layout() {
        if (!hwnd) return;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        int margin=S(10), bottom=S(106), width=rc.right-margin*2;
        previewPanel=RectF(float(margin),float(margin),float(width),float(std::max(S(180),int(rc.bottom)-bottom-margin)));
        auto place=[&](int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(hwnd,id),x,y,w,h,TRUE);};
        for(int id:{ID_MODE_SIDE,ID_MODE_OVERLAY,ID_SHOW_OVERLAY,ID_OPACITY})ShowWindow(GetDlgItem(hwnd,id),SW_HIDE);
        sideBySide=true;
        int top=margin+S(34), x=margin+width/2+S(8);
        for(int i=0;i<9;++i){place(ID_ICON_FIRST+i,x,top,S(30),S(30));x+=S(33);}
        for(auto item:{std::make_pair(ID_REVERSE,40),std::make_pair(ID_UNDO,40),std::make_pair(ID_REDO,40)}){
            place(item.first,x,top,S(item.second),S(30));x+=S(item.second+3);}
        x=margin+width/2+S(8);
        place(ID_SWAP_SOLID,x,top+S(34),S(155),S(26));
        place(ID_SWAP_HASH,x+S(162),top+S(34),S(155),S(26));
        place(ID_FLIP,x+S(324),top+S(34),S(96),S(26));
        place(ID_ZOOM_OUT,margin+S(12),top,S(26),S(24));
        place(ID_ZOOM_IN,margin+S(42),top,S(26),S(24));
        place(ID_ZOOM_FIT,margin+S(72),top,S(44),S(24));
        place(ID_DETAILS,margin+S(124),top,S(44),S(24));
        place(ID_SOURCE,margin+S(174),top,S(76),S(24));
        place(ID_VECTOR,margin+S(254),top,S(76),S(24));
        place(ID_EXPORT,margin+S(334),top,S(112),S(24));
        int infoHeight=details?S(120):0;
        previewRect=RectF(float(margin+S(8)),float(top+S(66)),float(width-S(16)),float(std::max(S(90),int(previewPanel.GetBottom())-top-S(102)-infoHeight)));
        float gap=SF(10), half=(previewRect.Width-gap)/2;
        pagePanel=RectF(previewRect.X,previewRect.Y,half,previewRect.Height);
        pageRect=RectF(pagePanel.X+S(6),pagePanel.Y+S(30),pagePanel.Width-S(12),pagePanel.Height-S(36));
        computePageScale();
        MoveWindow(editor.hwnd,int(previewRect.X+half+gap+S(6)),int(previewRect.Y+S(30)),int(half-S(12)),int(previewRect.Height-S(36)),TRUE);
        ShowWindow(editor.hwnd,hasResult&&editor.loaded?SW_SHOW:SW_HIDE);
        place(ID_CONF,margin+S(8),int(previewRect.GetBottom())+S(4),width-S(16),S(34));
        place(ID_INFO,margin+S(8),int(previewRect.GetBottom())+S(40),width-S(16),infoHeight);
        ShowWindow(GetDlgItem(hwnd,ID_INFO),details?SW_SHOW:SW_HIDE);
        int y=rc.bottom-bottom+S(6);
        place(ID_STATUS,margin,y,width,S(24));y+=S(28);
        place(ID_OPEN,margin,y,S(120),S(26));place(ID_PASTE,margin+S(126),y,S(94),S(26));
        place(ID_DETECT,margin+S(226),y,S(88),S(26));
        place(ID_RECOGNIZE,margin+S(320),y,S(88),S(26));place(ID_INSERT,margin+S(414),y,S(88),S(26));
        place(ID_COPY,margin+S(508),y,S(88),S(26));place(ID_CLOSE,rc.right-margin-S(60),y,S(60),S(26));y+=S(32);
        place(ID_PREV,margin,y,S(56),S(24));place(ID_PAGE,margin+S(62),y,S(36),S(24));
        place(ID_PAGE_TOTAL,margin+S(102),y,S(55),S(24));place(ID_NEXT,margin+S(162),y,S(56),S(24));
        place(ID_GO,margin+S(224),y,S(42),S(24));place(ID_DPI,margin+S(272),y,S(90),S(200));
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void computePageScale() {
        if (!pageWidth || !pageHeight || pageRect.Width <= 1 || pageRect.Height <= 1) {
            pageScale = 1.0;
            return;
        }
        pageScale = std::min(pageRect.Width / pageWidth, pageRect.Height / pageHeight);
        if (pageScale < 0.01) pageScale = 0.01;
    }

    Point pageOrigin() const {
        double w = pageWidth * pageScale;
        double h = pageHeight * pageScale;
        return Point(int(pageRect.X + (pageRect.Width - w) / 2), int(pageRect.Y + (pageRect.Height - h) / 2));
    }

    Point imageToDevice(double x, double y) const {
        Point origin = pageOrigin();
        return Point(int(origin.X + x * pageScale), int(origin.Y + y * pageScale));
    }

    void deviceToImage(Point point, int* x, int* y) const {
        Point origin = pageOrigin();
        *x = int((point.X - origin.X) / std::max(0.01, pageScale));
        *y = int((point.Y - origin.Y) / std::max(0.01, pageScale));
        *x = std::max(0, std::min(pageWidth - 1, *x));
        *y = std::max(0, std::min(pageHeight - 1, *y));
    }

    void repairVenvConfig() {
        // The uv-created venv stores an absolute "home = ..." path.  A
        // portable folder can be moved, so re-point it at this copy before
        // launching.  This is a small text file, not a runtime download.
        std::wstring cfgPath = runtimeDir + L"\\venv\\pyvenv.cfg";
        std::wstring base = runtimeDir + L"\\python\\cpython-3.10-windows-x86_64-none";
        FILE* file = _wfopen(cfgPath.c_str(), L"rb");
        if (!file) return;
        std::string text;
        char buffer[4096];
        size_t got = 0;
        while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, got);
        fclose(file);
        size_t start = text.find("home =");
        if (start == std::string::npos) return;
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = "home = " + U(base);
        if (text.compare(start, end - start, line) == 0) return;
        text.replace(start, end - start, line);
        file = _wfopen(cfgPath.c_str(), L"wb");
        if (!file) return;
        fwrite(text.data(), 1, text.size(), file);
        fclose(file);
    }

    void startService() {
        repairVenvConfig();
        // Wide-API existence checks: the UTF-8 narrow std::ifstream path cannot
        // open files under non-ASCII directories (e.g. a Chinese install path).
        if (GetFileAttributesW(pythonPath.c_str()) == INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(scriptPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            serviceReady = false;
            statusText = L"本地 OCSR 运行时缺失：请安装 ocsr_runtime（含 Python、MolScribe 权重）";
            warningsText = L"未找到本地识别运行时。\r\n"
                           L"预期路径：" + pythonPath + L"\r\n" + scriptPath + L"\r\n"
                           L"界面仍可打开，但无法渲染 PDF 或识别结构。";
            setInfoText(warningsText);
            refreshEnabled();
            return;
        }
        runTask(TaskResult::Service, L"正在启动本地识别服务…", [this]() {
            std::wstring error;
            if (!client.start(pythonPath, scriptPath, modelsDir, cacheDir, recordsDir, sitePackages, error))
                throw std::runtime_error(U(error));
            return client.request("<request id=\"1\" op=\"status\"/>", 180000);
        });
    }

    void startRender() {
        if (sourcePath.empty()) return;
        std::wstring path = sourcePath;
        int wantedPage = page;
        int wantedDpi = dpi;
        runTask(TaskResult::Render, L"正在本地渲染页面…", [this, path, wantedPage, wantedDpi]() {
            std::ostringstream xml;
            xml << "<request id=\"2\" op=\"render\" file=\"" << xmlEscape(U(path)) << "\" page=\""
                << wantedPage << "\" dpi=\"" << wantedDpi << "\"/>";
            return client.request(xml.str());
        });
    }

    void startRecognition() {
        if (!pageLoaded || selectionPx.right <= selectionPx.left || selectionPx.bottom <= selectionPx.top) {
            setStatus(L"请先在页面上拖拽框选一个完整的化学结构");
            return;
        }
        std::string pageImage = U(pageImagePath);
        std::string source = U(sourcePath);
        std::string kind = U(sourceKind);
        int x = selectionPx.left;
        int y = selectionPx.top;
        int w = selectionPx.right - selectionPx.left;
        int h = selectionPx.bottom - selectionPx.top;
        int currentPage = page, currentDpi = dpi;
        bool useVector = sourceKind == L"pdf" &&
                         studio_i18n::sendMessage(GetDlgItem(hwnd, ID_VECTOR), BM_GETCHECK, 0, 0) == BST_CHECKED;
        runTask(TaskResult::Recognize, useVector ? L"正在按 PDF 矢量内容重建高清选区并识别（MolScribe，CPU）…"
                                                 : L"正在本地识别（MolScribe，CPU）…",
                [this, pageImage, source, kind, x, y, w, h, currentPage, currentDpi, useVector]() {
            std::ostringstream xml;
            xml << "<request id=\"3\" op=\"recognize\" page_image=\"" << xmlEscape(pageImage)
                << "\" crop_x=\"" << x << "\" crop_y=\"" << y << "\" crop_w=\"" << w
                << "\" crop_h=\"" << h << "\" dpi=\"" << currentDpi
                << "\" source_kind=\"" << xmlEscape(kind) << "\" source_path=\""
                << xmlEscape(source) << "\" page=\"" << currentPage << "\""
                << " vector=\"" << (useVector ? "1" : "0") << "\" target_dpi=\"600\"/>";
            return client.request(xml.str(), 600000);
        });
    }

    void startDetect() {
        if (!pageLoaded || sourcePath.empty()) return;
        if (hasResult) clearResult();
        selectionPx = PixelRect{0, 0, 0, 0};
        detectedRegions.clear();
        pressedDetected = -1;
        std::string source = U(sourcePath);
        int currentPage = page, currentDpi = dpi;
        runTask(TaskResult::Detect, L"正在检测页面上的候选结构…",
                [this, source, currentPage, currentDpi]() {
            std::ostringstream xml;
            xml << "<request id=\"6\" op=\"detect\" file=\"" << xmlEscape(source)
                << "\" page=\"" << currentPage << "\" dpi=\"" << currentDpi
                << "\" min_score=\"0.35\" max_regions=\"120\"/>";
            return client.request(xml.str(), 120000);
        });
    }

    void applyDetect(tinyxml2::XMLElement* root) {
        detectedRegions.clear();
        for (auto* region = root->FirstChildElement("region"); region;
             region = region->NextSiblingElement("region")) {
            int x = 0, y = 0, w = 0, h = 0;
            const char* pixels = region->Attribute("pixels");
            if (!pixels || sscanf(pixels, "%d,%d,%d,%d", &x, &y, &w, &h) != 4) continue;
            if (w < 8 || h < 8) continue;
            double score = region->Attribute("score")
                               ? std::strtod(region->Attribute("score"), nullptr) : 0.0;
            detectedRegions.push_back(DetectedRegion{PixelRect{x, y, x + w, y + h}, score});
        }
        if (detectedRegions.empty()) {
            setStatus(L"没有检测到候选结构；请手动框选，或降低检测阈值后重试");
        } else {
            setStatus(L"检测到 " + std::to_wstring(detectedRegions.size()) +
                      L" 个候选结构 · 点击绿色框选中，或直接拖拽框选");
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    static std::wstring replaceExtension(const std::wstring& path, const wchar_t* extension) {
        size_t slash = path.find_last_of(L"\\/");
        size_t dot = path.find_last_of(L'.');
        std::wstring stem = (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
                                ? path.substr(0, dot) : path;
        return stem + extension;
    }

    void exportCrop() {
        if (!pageLoaded || selectionPx.right <= selectionPx.left || selectionPx.bottom <= selectionPx.top) {
            setStatus(L"请先在页面上拖拽框选一个完整的化学结构");
            return;
        }
        int x = selectionPx.left, y = selectionPx.top;
        int w = selectionPx.right - selectionPx.left, h = selectionPx.bottom - selectionPx.top;
        wchar_t file[32768]{};
        swprintf(file, 32768, L"矢量裁剪-第%d页-%dx%d.png", page, w, h);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"高清 PNG (*.png)\0*.png\0矢量 PDF (*.pdf)\0*.pdf\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFile = file;
        ofn.nMaxFile = 32768;
        ofn.lpstrDefExt = L"png";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!studio_i18n::saveFileName(&ofn)) return;

        std::wstring chosen = file;
        std::wstring lower = chosen;
        for (auto& c : lower) c = wchar_t(towlower(c));
        std::wstring pngPath, pdfPath;
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".pdf") {
            pdfPath = chosen;
            pngPath = replaceExtension(chosen, L".png");
        } else {
            pngPath = (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".png")
                          ? chosen : replaceExtension(chosen, L".png");
            pdfPath = sourceKind == L"pdf" ? replaceExtension(pngPath, L".pdf") : L"";
        }
        pendingExportPng = pngPath;
        pendingExportPdf = pdfPath;
        int currentPage = page, currentDpi = dpi;
        std::wstring source = sourcePath;
        runTask(TaskResult::Export, L"正在从 PDF 矢量内容重建高清选区（1200 DPI）…",
                [this, source, x, y, w, h, currentPage, currentDpi]() {
            std::ostringstream xml;
            xml << "<request id=\"5\" op=\"export_crop\" file=\"" << xmlEscape(U(source))
                << "\" page=\"" << currentPage << "\" dpi=\"" << currentDpi
                << "\" crop_x=\"" << x << "\" crop_y=\"" << y << "\" crop_w=\"" << w
                << "\" crop_h=\"" << h << "\" target_dpi=\"1200\" formats=\"png+pdf\"/>";
            return client.request(xml.str(), 120000);
        });
    }

    void applyExport(tinyxml2::XMLElement* root) {
        std::wstring image = W(root->Attribute("image") ? root->Attribute("image") : "");
        std::wstring pdf = W(root->Attribute("pdf") ? root->Attribute("pdf") : "");
        int width = root->IntAttribute("width", 0);
        int height = root->IntAttribute("height", 0);
        double effectiveDpi = root->Attribute("dpi") ? std::strtod(root->Attribute("dpi"), nullptr) : 0.0;
        bool pngOk = false, pdfOk = false;
        DWORD pngError = 0, pdfError = 0;
        if (!pendingExportPng.empty() && !image.empty()) {
            pngOk = CopyFileW(image.c_str(), pendingExportPng.c_str(), FALSE) != 0;
            if (!pngOk) pngError = GetLastError();
        }
        if (!pendingExportPdf.empty() && !pdf.empty()) {
            pdfOk = CopyFileW(pdf.c_str(), pendingExportPdf.c_str(), FALSE) != 0;
            if (!pdfOk) pdfError = GetLastError();
        }
        std::wostringstream info;
        info << L"矢量裁剪导出完成。\r\n";
        if (pngOk) {
            info << L"高清 PNG：" << pendingExportPng << L"\r\n  " << width << L" × " << height
                 << L" px";
            if (effectiveDpi > 0) {
                wchar_t buffer[32]{}; swprintf(buffer, 32, L" · %.0f DPI", effectiveDpi);
                info << buffer;
            }
            info << L"（从 PDF 矢量内容重建，不是页面截图放大）\r\n";
        } else {
            info << L"PNG 导出失败（错误 " << pngError << L"）：" << pendingExportPng << L"\r\n";
        }
        if (pdfOk) {
            info << L"矢量 PDF：" << pendingExportPdf << L"\r\n  CropBox 即当前选区，可无限缩放或再次导入。\r\n";
        } else if (!pendingExportPdf.empty()) {
            info << L"矢量 PDF 导出失败（错误 " << pdfError << L"）：" << pendingExportPdf << L"\r\n";
        }
        warningsText = info.str();
        setInfoText(warningsText);
        setStatus(pngOk ? L"高清矢量裁剪已导出 · 可直接送入本地识别或存档" : L"导出未完成，请检查路径与权限");
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    template <typename F>
    void runTask(TaskResult::Kind kind, const std::wstring& label, F work) {
        if (busy) return;
        if (worker.joinable()) worker.join();
        busy = true;
        setStatus(label);
        refreshEnabled();
        worker = std::thread([this, kind, label, work]() {
            auto* result = new TaskResult();
            result->kind = kind;
            result->label = label;
            try {
                result->xml = work();
                result->ok = true;
            } catch (const std::exception& error) {
                result->error = W(error.what());
            } catch (...) {
                result->error = L"未知错误";
            }
            if (closing) {
                delete result;
                return;
            }
            PostMessageW(hwnd, kTaskMessage, 0, LPARAM(result));
        });
    }

    static tinyxml2::XMLElement* parseResponse(const std::string& xml, std::wstring& error) {
        static thread_local tinyxml2::XMLDocument document;
        document.Clear();
        if (document.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) {
            error = L"识别服务返回了无效响应";
            return nullptr;
        }
        auto* root = document.RootElement();
        if (!root || std::string(root->Name()) != "response") {
            error = L"识别服务响应格式不正确";
            return nullptr;
        }
        if (!root->BoolAttribute("ok")) {
            const char* message = root->Attribute("error");
            error = message ? W(message) : L"识别服务返回失败";
            return nullptr;
        }
        return root;
    }

    void onTask(TaskResult* result) {
        if (!result) return;
        busy = false;
        if (!result->ok) {
            setStatus(L"操作已结束：" + result->error);
            warningsText = L"操作失败：\r\n" + result->error;
            setInfoText(warningsText);
            refreshEnabled();
            delete result;
            return;
        }
        std::wstring error;
        auto* root = parseResponse(result->xml, error);
        if (!root) {
            setStatus(error);
            warningsText = error;
            setInfoText(warningsText);
            refreshEnabled();
            delete result;
            return;
        }
        switch (result->kind) {
        case TaskResult::Service:
            applyStatus(root);
            break;
        case TaskResult::Render:
            applyRender(root);
            break;
        case TaskResult::Recognize:
            applyRecognition(root);
            break;
        case TaskResult::Export:
            applyExport(root);
            break;
        case TaskResult::Detect:
            applyDetect(root);
            break;
        case TaskResult::Confirm:
            break;
        }
        refreshEnabled();
        delete result;
    }

    // Local preview of the picked image (no model, no cache copy). The service
    // render that recognition needs still runs later, once the worker answers.
    void loadQuickImage() {
        if (sourcePath.empty()) return;
        pageBitmap = loadBitmap(sourcePath);
        if (!pageBitmap) {
            setStatus(L"无法读取图片：" + sourcePath);
            return;
        }
        pageWidth = int(pageBitmap->GetWidth());
        pageHeight = int(pageBitmap->GetHeight());
        pageCount = 1;
        page = 1;
        pageLoaded = true;
        detectedRegions.clear();
        pressedDetected = -1;
        selectionPx = PixelRect{0, 0, pageWidth, pageHeight};
        computePageScale();
        studio_i18n::setWindowText(GetDlgItem(hwnd, ID_PAGE), L"1");
        studio_i18n::setWindowText(GetDlgItem(hwnd, ID_PAGE_TOTAL), L"/ 1 页");
        setStatus(L"图片已载入 " + std::to_wstring(pageWidth) + L" × " +
                  std::to_wstring(pageHeight) + L" px · 正在启动本地识别服务…");
        refreshEnabled();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void applyStatus(tinyxml2::XMLElement* root) {
        serviceReady = true;
        const char* model = root->Attribute("model_path");
        modelAvailable = model && *model;
        std::wstring torch = root->Attribute("torch") ? W(root->Attribute("torch")) : L"";
        if (modelAvailable) {
            std::wstring name = W(model);
            size_t slash = name.find_last_of(L"\\/");
            if (slash != std::wstring::npos) name = name.substr(slash + 1);
            warningsText = L"本地识别服务就绪（不上传文献）。\r\n模型：" + name +
                           L"\r\n运行时：Python " + (torch.empty() ? L"未知" : torch) +
                           L"\r\n设备：CPU（无 GPU 时使用 CPU）\r\n"
                           L"先在左侧页面拖拽框选一个结构，再点“识别选区”。";
            setStatus(L"本地识别服务就绪 · " + name + L" · 请打开 PDF 或图片");
        } else {
            warningsText = L"识别服务已启动，但未找到 MolScribe 权重。\r\n"
                           L"请把 swin_base_char_aux_1m.pth（约 1.06 GiB）放到：\r\n"
                           + modelsDir + L"\r\n模型缺失时仍可浏览页面，但不能识别。";
            setStatus(L"模型缺失：请在 ocsr_runtime\\models 放入 MolScribe 权重");
        }
        setInfoText(warningsText);
        if (imageMode && pageLoaded) startRender();
        else if (!demoPdf.empty() && sourcePath.empty()) beginDemo();
        else if (!sourcePath.empty()) startRender();
        else if (!modelAvailable) {
            // Keep the dialog usable for page browsing once a runtime exists.
        }
    }

    void beginDemo() {
        if (demoPdf.empty()) return;
        sourcePath = demoPdf;
        sourceKind = L"pdf";
        page = std::max(1, demoPage);
        startRender();
    }

    void clearResult() {
        editor.loaded=false; ShowWindow(editor.hwnd,SW_HIDE);
        hasResult = false;
        insertable = false;
        cropBitmap.reset();
        overlayBitmap.reset();
        blendBitmap.reset();
        resultBitmap.reset();
        cdxmlPath.clear();
        recordPath.clear();
        confidence = -1.0;
        vectorDpi = 0.0;
        stereoStatus.clear();
        atomCount = bondCount = fragmentCount = 0;
        confidenceText = L"尚未识别";
        studio_i18n::setWindowText(GetDlgItem(hwnd, ID_CONF), confidenceText.c_str());
        warningsText.clear();
        setInfoText(warningsText);
        refreshEnabled();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void applyRender(tinyxml2::XMLElement* root) {
        pageImagePath = W(root->Attribute("image") ? root->Attribute("image") : "");
        pageCount = root->IntAttribute("pages", 1);
        pageWidth = root->IntAttribute("width", 0);
        pageHeight = root->IntAttribute("height", 0);
        page = root->IntAttribute("page", 1);
        pageBitmap = loadBitmap(pageImagePath);
        pageLoaded = pageBitmap != nullptr;
        detectedRegions.clear();
        pressedDetected = -1;
        selectionPx = PixelRect{0, 0, 0, 0};
        selectingActive = false;
        clearResult();
        computePageScale();
        std::wstring pageText = std::to_wstring(page);
        studio_i18n::setWindowText(GetDlgItem(hwnd, ID_PAGE), pageText.c_str());
        std::wstring total = L"/ " + std::to_wstring(pageCount) + L" 页";
        studio_i18n::setWindowText(GetDlgItem(hwnd, ID_PAGE_TOTAL), total.c_str());
        setStatus(pageLoaded ? L"第 " + pageText + L" / " + std::to_wstring(pageCount) +
                                   L" 页 · 拖拽框选一个完整结构"
                             : L"页面渲染成功，但图片加载失败");
        if (pageLoaded && quickPending) {
            // The whole image is the crop: the user picked a picture of one
            // structure, so recognise it without asking for a selection.
            quickPending = false;
            selectionPx = PixelRect{0, 0, std::max(1, pageWidth), std::max(1, pageHeight)};
            startRecognition();
        }
        if (pageLoaded && demoPending) {
            demoPending = false;
            int left = std::max(0, std::min(pageWidth - 2, demoX));
            int top = std::max(0, std::min(pageHeight - 2, demoY));
            int right = std::max(left + 1, std::min(pageWidth, demoX + demoW));
            int bottom = std::max(top + 1, std::min(pageHeight, demoY + demoH));
            selectionPx = PixelRect{left, top, right, bottom};
            startRecognition();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void applyRecognition(tinyxml2::XMLElement* root) {
        std::wstring crop = W(root->Attribute("crop") ? root->Attribute("crop") : "");
        std::wstring overlay = W(root->Attribute("overlay") ? root->Attribute("overlay") : "");
        std::wstring blend = W(root->Attribute("blend") ? root->Attribute("blend") : "");
        std::wstring result = W(root->Attribute("result") ? root->Attribute("result") : "");
        cdxmlPath = root->Attribute("cdxml") ? root->Attribute("cdxml") : "";
        recordPath = root->Attribute("record") ? root->Attribute("record") : "";
        cropBitmap = loadBitmap(crop);
        overlayBitmap = loadBitmap(overlay);
        blendBitmap = loadBitmap(blend);
        resultBitmap = loadBitmap(result);
        hasResult = cropBitmap && resultBitmap && (blendBitmap || overlayBitmap);
        insertable = hasResult && root->BoolAttribute("insertable");
        confidence = root->Attribute("confidence") && *root->Attribute("confidence")
                         ? std::strtod(root->Attribute("confidence"), nullptr)
                         : -1.0;
        atomCount = root->IntAttribute("atoms", 0);
        bondCount = root->IntAttribute("bonds", 0);
        fragmentCount = root->IntAttribute("fragments", 0);
        insertable = hasResult && atomCount > 0 && !cdxmlPath.empty() && GetFileAttributesW(W(cdxmlPath).c_str()) != INVALID_FILE_ATTRIBUTES;
        if(insertable && !editor.load(cdxmlPath)){insertable=false; setStatus(L"结构编辑器载入失败");}
        layout();
        stereoStatus = root->Attribute("stereo_status") ? W(root->Attribute("stereo_status")) : L"";
        vectorDpi = root->Attribute("vector_dpi") && *root->Attribute("vector_dpi")
                        ? std::strtod(root->Attribute("vector_dpi"), nullptr) : 0.0;
        warningsText.clear();
        if (vectorDpi > 0) {
            wchar_t buffer[96]{};
            swprintf(buffer, 96, L"• 模型输入由 PDF 矢量内容按 %.0f DPI 重建（页面预览 %d DPI），不是截图放大。\r\n",
                     vectorDpi, dpi);
            warningsText = buffer;
        }
        for (auto* warning = root->FirstChildElement("warning"); warning; warning = warning->NextSiblingElement("warning")) {
            if (warning->GetText()) {
                if (!warningsText.empty()) warningsText += L"\r\n";
                warningsText += L"• " + W(warning->GetText());
            }
        }
        if (root->Attribute("stereo_detail") && *root->Attribute("stereo_detail")) {
            if (!warningsText.empty()) warningsText += L"\r\n";
            warningsText += L"• 立体保持校验详情（CDXML→RDKit 回读）：" + W(root->Attribute("stereo_detail"));
        }
        if (warningsText.empty()) warningsText = L"没有额外警告。";
        setInfoText(warningsText);
        std::wostringstream summary;
        summary << L"原子 " << atomCount << L" · 键 " << bondCount << L" · 片段 " << fragmentCount;
        if (vectorDpi > 0) {
            wchar_t buffer[96]{};
            swprintf(buffer, 96, L"\r\n矢量重建输入 %.0f DPI（页面预览 %d DPI）", vectorDpi, dpi);
            summary << buffer;
        }
        if (!stereoStatus.empty()) {
            std::wstring label = stereoStatus;
            if (stereoStatus == L"passed") label = L"通过（RDKit 回读）";
            else if (stereoStatus == L"failed") label = L"失败";
            else if (stereoStatus == L"not_verified") label = L"无法验证（已结束）";
            else if (stereoStatus == L"not_specified") label = L"E/Z 未指定（已保留）";
            else if (stereoStatus == L"not_applicable") label = L"不存在可定义 E/Z";
            summary << L" · 立体保持 " << label;
        }
        if (confidence >= 0) {
            wchar_t buffer[128]{};
            swprintf(buffer, 128, L"%.3f", confidence);
            summary << L"\r\n模型置信度 " << buffer << L"（非正确率）";
        }
        confidenceText = summary.str();
        studio_i18n::setWindowText(GetDlgItem(hwnd, ID_CONF), confidenceText.c_str());
        if (stereoStatus == L"not_verified") {
            warningsText = L"校验已结束，无法自动确认立体保持性；等待不会改变结果。请查看下方原因，请核对原图；结果仍可复制或插入画布后编辑。\r\n\r\n" + warningsText;
            setInfoText(warningsText);
        }
        setStatus(insertable ? L"识别完成 · 请核对原图，再复制或插入画布"
                  : stereoStatus == L"not_verified" ? L"校验已结束：无法验证立体中心。原因见右侧；请调整选区后重试。"
                  : stereoStatus == L"failed" ? L"校验失败：立体信息发生变化，已禁止插入和复制。"
                  : L"质量校验未通过，已禁止插入和复制；原因见右侧。");
        { wchar_t overlayFlag[8]={}; if(GetEnvironmentVariableW(L"MOLECULE_STUDIO_OCSR_DEMO_OVERLAY",overlayFlag,8)){
            sideBySide=false; CheckRadioButton(hwnd,ID_MODE_SIDE,ID_MODE_OVERLAY,ID_MODE_OVERLAY);
        } }
        if (demoInsert && insertable) SetTimer(hwnd, 9, 900, nullptr);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    std::unique_ptr<Bitmap> loadBitmap(const std::wstring& path) {
        if (path.empty()) return nullptr;
        Bitmap* raw = Bitmap::FromFile(path.c_str(), FALSE);
        if (!raw || raw->GetLastStatus() != Ok) {
            delete raw;
            return nullptr;
        }
        Bitmap* copy = raw->Clone(0, 0, raw->GetWidth(), raw->GetHeight(), PixelFormat32bppPARGB);
        delete raw;
        if (!copy || copy->GetLastStatus() != Ok) {
            delete copy;
            return nullptr;
        }
        return std::unique_ptr<Bitmap>(copy);
    }

    void setInfoText(const std::wstring& text) {
        std::wstring normalized = text;
        for (size_t i = 0; i < normalized.size(); ++i)
            if (normalized[i] == L'\n' && (i == 0 || normalized[i - 1] != L'\r')) normalized.insert(i++, 1, L'\r');
        studio_i18n::setWindowText(GetDlgItem(hwnd, ID_INFO), normalized.c_str());
    }

    void setStatus(const std::wstring& text) {
        statusText = text;
        studio_i18n::setWindowText(GetDlgItem(hwnd, ID_STATUS), statusText.c_str());
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void refreshEnabled() {
        for(int id=ID_ICON_FIRST;id<ID_ICON_FIRST+9;++id)EnableWindow(GetDlgItem(hwnd,id),hasResult&&editor.loaded&&!busy);
        for(int id:{ID_FLIP,ID_SWAP_SOLID,ID_SWAP_HASH,ID_REVERSE,ID_UNDO,ID_REDO})EnableWindow(GetDlgItem(hwnd,id),hasResult&&editor.loaded&&!busy);
        bool canBrowse = serviceReady && !busy;
        bool canRecognize = canBrowse && modelAvailable && pageLoaded && !selectingActive &&
                            selectionPx.right > selectionPx.left && selectionPx.bottom > selectionPx.top;
        bool canInsert = canBrowse && hasResult && insertable;
        auto enable = [&](int id, bool value) {
            if (HWND control = GetDlgItem(hwnd, id)) EnableWindow(control, value ? TRUE : FALSE);
        };
        enable(ID_OPEN, !busy);
        enable(ID_PASTE, !busy);
        enable(ID_PREV, canBrowse && pageLoaded && page > 1);
        enable(ID_NEXT, canBrowse && pageLoaded && page < pageCount);
        enable(ID_PAGE, canBrowse && pageLoaded);
        enable(ID_GO, canBrowse && pageLoaded);
        enable(ID_DPI, canBrowse);
        enable(ID_VECTOR, canBrowse && pageLoaded && sourceKind == L"pdf");
        enable(ID_EXPORT, canBrowse && pageLoaded &&
                         selectionPx.right > selectionPx.left && selectionPx.bottom > selectionPx.top);
        enable(ID_DETECT, canBrowse && pageLoaded);
        enable(ID_RECOGNIZE, canRecognize);
        enable(ID_INSERT, canInsert);
        enable(ID_COPY, canInsert);
        enable(ID_MODE_SIDE, canBrowse);
        enable(ID_MODE_OVERLAY, canBrowse);
        enable(ID_SHOW_OVERLAY, canBrowse);
        enable(ID_OPACITY, canBrowse);
        enable(ID_ZOOM_OUT, canBrowse && hasResult);
        enable(ID_ZOOM_IN, canBrowse && hasResult);
        enable(ID_ZOOM_FIT, canBrowse && hasResult);
    }

    void openFile() {
        wchar_t file[32768]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"PDF 或图片 (*.pdf;*.png;*.jpg;*.jpeg)\0*.pdf;*.png;*.jpg;*.jpeg\0PDF (*.pdf)\0*.pdf\0图片 (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0所有文件 (*.*)\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = 32768;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!studio_i18n::openFileName(&ofn)) return;
        sourcePath = file;
        std::wstring lower = sourcePath;
        for (auto& c : lower) c = wchar_t(towlower(c));
        sourceKind = (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".pdf") ? L"pdf" : L"image";
        page = 1;
        pageCount = 0;
        pageLoaded = false;
        pageBitmap.reset();
        detectedRegions.clear();
        pressedDetected = -1;
        clearResult();
        quickPending = sourceKind == L"image";
        if (serviceReady) startRender(); else { loadQuickImage(); setStatus(L"图片已载入 · 等待本地服务就绪"); }
    }

    static LRESULT CALLBACK controlProc(HWND control, UINT message, WPARAM wp, LPARAM lp,
                                         UINT_PTR id, DWORD_PTR data) {
        auto* dialog = reinterpret_cast<Dialog*>(data);
        if (message == WM_NCDESTROY) RemoveWindowSubclass(control, controlProc, id);
        if (message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && (wp == 'V' || wp == 'C')) {
            // Preserve ordinary copying of selected diagnostics/page text.
            DWORD begin = 0, end = 0;
            studio_i18n::sendMessage(control, EM_GETSEL, WPARAM(&begin), LPARAM(&end));
            if (wp == 'V' || begin == end) { if(wp=='C')dialog->copyResult();else dialog->pasteClipboard(); return 0; }
        }
        return DefSubclassProc(control, message, wp, lp);
    }

    void pasteClipboard() {
        if (busy) { setStatus(L"当前任务进行中，请完成后再粘贴图片"); return; }
        if (!OpenClipboard(hwnd)) { setStatus(L"剪贴板暂时被占用，请重试"); return; }
        std::unique_ptr<Bitmap> picture;
        UINT png = RegisterClipboardFormatW(L"PNG");
        if (HANDLE handle = GetClipboardData(png)) {
            SIZE_T bytes = GlobalSize(handle);
            if (bytes && bytes < 256u * 1024u * 1024u) {
                HGLOBAL clone = GlobalAlloc(GMEM_MOVEABLE, bytes);
                void* src = GlobalLock(handle); void* dst = GlobalLock(clone);
                if (src && dst) memcpy(dst, src, bytes);
                if (src) GlobalUnlock(handle); if (dst) GlobalUnlock(clone);
                IStream* stream = nullptr;
                if (src && dst && SUCCEEDED(CreateStreamOnHGlobal(clone, TRUE, &stream))) {
                    Bitmap image(stream);
                    if (image.GetLastStatus() == Ok) picture.reset(image.Clone(0, 0, image.GetWidth(), image.GetHeight(), PixelFormat32bppPARGB));
                    stream->Release();
                } else if (clone) GlobalFree(clone);
            }
        }
        if (!picture) {
            HBITMAP bitmap = reinterpret_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
            if (bitmap) picture.reset(Bitmap::FromHBITMAP(bitmap, nullptr));
        }
        // Windows synthesizes CF_BITMAP from CF_DIB/CF_DIBV5 (screenshots).
        CloseClipboard();
        if (!picture || picture->GetLastStatus() != Ok) {
            setStatus(L"剪贴板没有可读取的图片。请先复制图片或截图，再按 Ctrl+V（也支持 Ctrl+C）。"); return;
        }
        CreateDirectoryW(cacheDir.c_str(), nullptr);
        std::wstring path = cacheDir + L"\\clipboard-" + std::to_wstring(GetTickCount64()) + L".png";
        CLSID encoder = {0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        // Flatten transparent clipboard content against white for the recognizer.
        Bitmap flat(picture->GetWidth(), picture->GetHeight(), PixelFormat32bppRGB);
        { Graphics g(&flat); g.Clear(Color(255,255,255)); g.DrawImage(picture.get(), 0, 0, picture->GetWidth(), picture->GetHeight()); }
        if (flat.Save(path.c_str(), &encoder, nullptr) != Ok) { setStatus(L"无法写入剪贴板图片缓存"); return; }
        sourcePath = path; sourceKind = L"image"; page = 1; quickPending = true;
        clearResult(); loadQuickImage();
        if (serviceReady) startRender();
    }

    void gotoPage(int wanted) {
        if (!pageLoaded || pageCount <= 0) return;
        wanted = std::max(1, std::min(pageCount, wanted));
        if (wanted == page) return;
        page = wanted;
        startRender();
    }

    void confirmRecord() {
        if (recordPath.empty()) return;
        std::ostringstream xml;
        xml << "<request id=\"4\" op=\"confirm\" record=\"" << xmlEscape(recordPath)
            << "\" document=\"" << xmlEscape(U(sourcePath)) << "\"/>";
        try {
            client.request(xml.str(), 15000);
        } catch (...) {
            // Insertion already succeeded; the record remains pending rather
            // than claiming a review that could not be saved.
        }
    }

    std::string reviewPath() const {return cdxmlPath+".review.cdxml";}
    bool saveReview(){if(!editor.loaded)return false;if(editor.session.all().empty()){setStatus(L"预览中没有结构，请撤销删除或重新识别");return false;}
        std::string x;auto e=ocsr_edit::exportReview(editor.session.doc,&x);
        if(e.ok())e=persistence::writeFileAtomic(reviewPath(),x,persistence::AtomicWriteOptions());
        if(!e.ok()){setStatus(L"保存预览修改失败，请重试");return false;}return true;}

    void copyResult() {
        if (!hasResult || !insertable) {
            setStatus(L"结果未通过质量校验，已禁止复制");
            return;
        }
        if (!copy) {
            setStatus(L"当前窗口不支持复制结构");
            return;
        }
        editor.finishLabel();
        std::wstring error;
        if (!saveReview())return;
        if (!copy(reviewPath(), error)) {
            setStatus(L"复制结构失败：" + (error.empty() ? L"未知错误" : error));
            studio_i18n::messageBox(hwnd, error.empty() ? L"复制结构失败。" : error.c_str(),
                        L"复制结构", MB_OK | MB_ICONWARNING);
            return;
        }
        setStatus(L"已复制结构 · 可直接粘贴到 ChemDraw / Word / PowerPoint");
    }

    void insertResult() {
        if (!hasResult || !insertable || !insert) return;
        std::wstring error;
        editor.finishLabel();
        if (!saveReview())return;
        if (!insert(reviewPath(), editor.modified()?std::string():recordPath, error)) {
            studio_i18n::messageBox(hwnd, error.empty() ? L"插入失败，文档未改变。" : error.c_str(),
                        L"插入文献结构", MB_OK | MB_ICONWARNING);
            return;
        }
        if(!editor.modified())confirmRecord();
        DestroyWindow(hwnd);
    }

    void paint(Graphics& graphics) {
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        SolidBrush background(Color(255, 243, 246, 250));
        graphics.FillRectangle(&background, RectF(0.0f, 0.0f, (float)bufferWidth, (float)bufferHeight));

        drawPanel(graphics, previewPanel, L"识别审核（原图对照）");
        if(!hasResult)drawPage(graphics);
        drawPreview(graphics);
        if(editor.modified()){Font f(L"Microsoft YaHei UI",SF(12),FontStyleBold,UnitPixel);SolidBrush b(Color(255,202,111,20));studio_i18n::drawString(graphics,L"已修改",-1,&f,PointF(previewPanel.GetRight()-S(80),previewPanel.Y+S(10)),&b);}
    }

    void drawPanel(Graphics& graphics, const RectF& rect, const std::wstring& title) {
        SolidBrush white(Color(255, 255, 255, 255));
        Pen border(Color(255, 224, 230, 227), 1.0f);
        graphics.FillRectangle(&white, rect);
        graphics.DrawRectangle(&border, rect);
        Font titleFont(L"Microsoft YaHei UI", SF(13.0f), FontStyleBold, UnitPixel);
        SolidBrush titleBrush(Color(255, 23, 117, 109));
        studio_i18n::drawString(graphics,title.c_str(), -1, &titleFont, PointF(rect.X + SF(12.0f), rect.Y + SF(10.0f)), &titleBrush);
    }

    void drawPage(Graphics& graphics) {
        if (!pageBitmap || !pageLoaded) {
            Font font(L"Microsoft YaHei UI", SF(11.0f), FontStyleRegular, UnitPixel);
            SolidBrush brush(Color(255, 117, 129, 129));
            RectF area = pageRect;
            area.Y += area.Height * 0.42f;
            studio_i18n::drawString(graphics,L"打开 PDF 或图片，然后在页面上拖拽框选一个完整化学结构", -1,
                                &font, area, nullptr, &brush);
            return;
        }
        Point origin = pageOrigin();
        RectF destination((float)origin.X, (float)origin.Y,
                          (float)(pageWidth * pageScale), (float)(pageHeight * pageScale));
        graphics.DrawImage(pageBitmap.get(), destination);
        Pen frame(Color(255, 210, 218, 214), 1.0f);
        graphics.DrawRectangle(&frame, destination);

        if (!detectedRegions.empty() && !hasResult) {
            Pen candidate(Color(255, 0, 155, 85), 1.75f);
            Font labelFont(L"Microsoft YaHei UI", SF(9.0f), FontStyleRegular, UnitPixel);
            SolidBrush labelBrush(Color(255, 0, 120, 70));
            for (size_t i = 0; i < detectedRegions.size(); ++i) {
                const PixelRect& rect = detectedRegions[i].rect;
                Point topLeft = imageToDevice(rect.left, rect.top);
                Point bottomRight = imageToDevice(rect.right, rect.bottom);
                RectF box((float)topLeft.X, (float)topLeft.Y,
                          (float)(bottomRight.X - topLeft.X),
                          (float)(bottomRight.Y - topLeft.Y));
                graphics.DrawRectangle(&candidate, box);
                wchar_t number[16]{};
                swprintf(number, 16, L"%d", int(i + 1));
                studio_i18n::drawString(graphics,number, -1, &labelFont,
                                    PointF(box.X + 2.0f, std::max(destination.Y + 1.0f, box.Y - 14.0f)),
                                    &labelBrush);
            }
        }

        if (selectionPx.right > selectionPx.left && selectionPx.bottom > selectionPx.top) {
            Point topLeft = imageToDevice(selectionPx.left, selectionPx.top);
            Point bottomRight = imageToDevice(selectionPx.right, selectionPx.bottom);
            RectF selected((float)topLeft.X, (float)topLeft.Y,
                           (float)(bottomRight.X - topLeft.X), (float)(bottomRight.Y - topLeft.Y));
            if (!selectingActive) {
                SolidBrush shade(Color(36, 20, 40, 52));
                graphics.FillRectangle(&shade, RectF(destination.X, destination.Y, destination.Width,
                                                     selected.Y - destination.Y));
                graphics.FillRectangle(&shade, RectF(destination.X, selected.GetBottom(), destination.Width,
                                                     destination.GetBottom() - selected.GetBottom()));
                graphics.FillRectangle(&shade, RectF(destination.X, selected.Y,
                                                     selected.X - destination.X, selected.Height));
                graphics.FillRectangle(&shade, RectF(selected.GetRight(), selected.Y,
                                                     destination.GetRight() - selected.GetRight(), selected.Height));
            }
            Pen outline(Color(255, 0, 120, 215), 2.0f);
            graphics.DrawRectangle(&outline, selected);
            SolidBrush handle(Color(255, 255, 255, 255));
            Pen handlePen(Color(255, 0, 120, 215), 1.0f);
            for (PointF corner : {PointF(selected.X, selected.Y),
                                  PointF(selected.GetRight(), selected.Y),
                                  PointF(selected.X, selected.GetBottom()),
                                  PointF(selected.GetRight(), selected.GetBottom())}) {
                graphics.FillRectangle(&handle, corner.X - 3.0f, corner.Y - 3.0f, 6.0f, 6.0f);
                graphics.DrawRectangle(&handlePen, corner.X - 3.0f, corner.Y - 3.0f, 6.0f, 6.0f);
            }
            Font font(L"Microsoft YaHei UI", SF(9.0f), FontStyleRegular, UnitPixel);
            SolidBrush brush(Color(255, 23, 117, 109));
            wchar_t label[128]{};
            swprintf(label, 128, L"选区 %d × %d px", selectionPx.right - selectionPx.left,
                     selectionPx.bottom - selectionPx.top);
            studio_i18n::drawString(graphics,label, -1, &font, PointF(selected.X, std::max(destination.Y + 2.0f, selected.Y - 20.0f)), &brush);
        }
    }

    void drawPreview(Graphics& graphics) {
        if (!hasResult || !cropBitmap) { return;
            Font font(L"Microsoft YaHei UI", SF(11.0f), FontStyleRegular, UnitPixel);
            SolidBrush brush(Color(255, 117, 129, 129));
            RectF area = previewRect;
            area.Y += area.Height * 0.40f;
            studio_i18n::drawString(graphics,L"打开或粘贴图片后，点击识别。\n在这里对照原图、检查立体信息。", -1,
                                &font, area, nullptr, &brush);
            return;
        }
        if (sideBySide) {
            float gap = SF(10.0f);
            RectF left(previewRect.X, previewRect.Y, (previewRect.Width - gap) / 2.0f, previewRect.Height);
            RectF right(left.GetRight() + gap, previewRect.Y, left.Width, previewRect.Height);
            drawImagePane(graphics, cropBitmap.get(), left, L"原图（裁剪）");
            drawPanel(graphics,right,L"编辑画布（与主画布相同）");
        } else {
            if (overlayVisible && blendBitmap)
                drawImagePane(graphics, blendBitmap.get(), previewRect, L"原图 + 识别叠加（合成图）");
            else
                drawImagePane(graphics, cropBitmap.get(), previewRect, L"原图（识别叠加已隐藏）");
        }
        wchar_t zoom[64]{};
        swprintf(zoom, 64, L"预览缩放 %.0f%%", previewZoom * 100.0);
        Font font(L"Microsoft YaHei UI", SF(9.0f), FontStyleRegular, UnitPixel);
        SolidBrush brush(Color(255, 117, 129, 129));
        studio_i18n::drawString(graphics,zoom, -1, &font, PointF(previewRect.GetRight() - 120.0f,
                                                    previewRect.GetBottom() - 20.0f), &brush);
    }

    RectF imageDestination(Bitmap* bitmap, const RectF& area) const {
        float fit = std::min(area.Width / bitmap->GetWidth(),
                             area.Height / bitmap->GetHeight()) * (float)previewZoom;
        float width = bitmap->GetWidth() * fit;
        float height = bitmap->GetHeight() * fit;
        return RectF(area.X + (area.Width - width) / 2.0f,
                     area.Y + (area.Height - height) / 2.0f, width, height);
    }

    void drawImagePane(Graphics& graphics, Bitmap* bitmap, const RectF& rect, const std::wstring& title) {
        SolidBrush background(Color(255, 255, 255, 255));
        graphics.FillRectangle(&background, rect);
        Pen border(Color(255, 224, 230, 227), 1.0f);
        graphics.DrawRectangle(&border, rect);
        Font font(L"Microsoft YaHei UI", SF(10.0f), FontStyleBold, UnitPixel);
        SolidBrush titleBrush(Color(255, 40, 52, 53));
        studio_i18n::drawString(graphics,title.c_str(), -1, &font, PointF(rect.X + SF(8.0f), rect.Y + SF(6.0f)), &titleBrush);
        RectF imageArea(rect.X + SF(6.0f), rect.Y + SF(30.0f), rect.Width - SF(12.0f), rect.Height - SF(36.0f));
        if (bitmap->GetWidth() <= 0 || bitmap->GetHeight() <= 0) return;
        RectF destination = imageDestination(bitmap, imageArea);
        graphics.SetClip(imageArea);
        graphics.DrawImage(bitmap, destination);
        graphics.ResetClip();
        Pen frame(Color(255, 210, 218, 214), 1.0f);
        graphics.DrawRectangle(&frame, imageArea);
    }

    void onPaint() {
        PAINTSTRUCT paintStruct{};
        HDC dc = BeginPaint(hwnd, &paintStruct);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        int width = std::max(1, int(rc.right - rc.left));
        int height = std::max(1, int(rc.bottom - rc.top));
        if (!memoryDc || bufferWidth != width || bufferHeight != height) {
            if (memoryBitmap) {
                if (memoryPrevious) SelectObject(memoryDc, memoryPrevious);
                DeleteObject(memoryBitmap);
            }
            if (memoryDc) DeleteDC(memoryDc);
            memoryDc = CreateCompatibleDC(dc);
            memoryBitmap = CreateCompatibleBitmap(dc, width, height);
            memoryPrevious = SelectObject(memoryDc, memoryBitmap);
            bufferWidth = width;
            bufferHeight = height;
        }
        {
            Graphics graphics(memoryDc);
            graphics.SetPageUnit(UnitPixel);
            paint(graphics);
        }
        BitBlt(dc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &paintStruct);
    }

    void onMouseDown(Point point, WPARAM) {
        if (hasResult || busy || !pageLoaded || !pageBitmap) return;
        if (point.X < pageRect.X || point.X > pageRect.GetRight() ||
            point.Y < pageRect.Y || point.Y > pageRect.GetBottom()) return;
        int x = 0, y = 0;
        deviceToImage(point, &x, &y);
        pressedDetected = -1;
        for (size_t i = 0; i < detectedRegions.size(); ++i) {
            const PixelRect& rect = detectedRegions[i].rect;
            if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom) {
                pressedDetected = int(i);
                break;
            }
        }
        selectingActive = true;
        selectStart = Point(x, y);
        selectCurrent = selectStart;
        selectionPx = PixelRect{x, y, x, y};
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void onMouseMove(Point point) {
        if (!selectingActive) return;
        int x = 0, y = 0;
        deviceToImage(point, &x, &y);
        selectCurrent = Point(x, y);
        selectionPx.left = std::min(selectStart.X, selectCurrent.X);
        selectionPx.top = std::min(selectStart.Y, selectCurrent.Y);
        selectionPx.right = std::max(selectStart.X, selectCurrent.X);
        selectionPx.bottom = std::max(selectStart.Y, selectCurrent.Y);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void onMouseUp() {
        if (!selectingActive) return;
        selectingActive = false;
        ReleaseCapture();
        if (selectionPx.right - selectionPx.left < 12 || selectionPx.bottom - selectionPx.top < 12) {
            if (pressedDetected >= 0 && pressedDetected < int(detectedRegions.size())) {
                selectionPx = detectedRegions[pressedDetected].rect;
                clearResult();
                setStatus(L"已选中候选结构 #" + std::to_wstring(pressedDetected + 1) +
                          L" · 点“识别选区”");
            } else {
                selectionPx = PixelRect{0, 0, 0, 0};
                setStatus(L"选区太小，请拖拽框选一个完整结构");
            }
        } else {
            clearResult();
            setStatus(L"已框选 " + std::to_wstring(selectionPx.right - selectionPx.left) + L" × " +
                      std::to_wstring(selectionPx.bottom - selectionPx.top) + L" px · 点“识别选区”");
        }
        refreshEnabled();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    static const char* toolCommand(int i){
        static const char* commands[]={"chooseSelectionTool_Marquee","chooseBondTool_Solid","chooseBondTool_Wedged",
            "chooseBondTool_Hashed_Wedged","chooseBondTool_Bold","chooseBondTool_Hashed","chooseMultipleBondsTool_DoubleBond","chooseEdittingTool_Eraser","chooseEdittingTool_Text"};
        return i>=0&&i<9?commands[i]:commands[0];
    }
    bool drawTool(DRAWITEMSTRUCT* item){
        int i=int(item->CtlID)-ID_ICON_FIRST;if(i<0||i>=9)return false;
        Graphics g(item->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);
        bool active=editor.loaded&&editor.session.activeIndex<editor.session.entries.size()&&
            editor.session.entries[editor.session.activeIndex].command==toolCommand(i);
        bool enabled=IsWindowEnabled(item->hwndItem)!=FALSE;
        RectF r(float(item->rcItem.left),float(item->rcItem.top),float(item->rcItem.right-item->rcItem.left),float(item->rcItem.bottom-item->rcItem.top));
        SolidBrush bg(active?Color(255,221,243,238):Color(255,255,255,255));g.FillRectangle(&bg,r);
        Pen border(active?Color(255,23,117,109):Color(255,224,230,227));g.DrawRectangle(&border,r.X,r.Y,r.Width-1,r.Height-1);
        static const char* glyphs[]={"select","bond","wedge","hashed","bold","hash","double","erase","text"};
        icon(g,glyphs[i],RectF(r.X+SF(3),r.Y+SF(2),r.Width-SF(6),r.Height-SF(6)),enabled?ink:muted);
        if(active){Pen mark(teal,SF(2));g.DrawLine(&mark,r.X+SF(6),r.GetBottom()-SF(3),r.GetRight()-SF(6),r.GetBottom()-SF(3));}
        return true;
    }
    void swapAll(bool hashed){
        if(!editor.loaded||busy)return;
        editor.finishLabel();editor.session.cancel();
        auto e=editor.session.submit(ocsr_edit::swapBondTypes(hashed),"OCSR batch swap");
        if(!e.ok()){setStatus(L"批量转换失败："+W(e.message));return;}
        editor.notify();SetFocus(editor.hwnd);
        setStatus(hashed?L"已整图互换：虚楔 ↔ 粗虚线 · Ctrl+Z 撤销":L"已整图互换：实楔 ↔ 粗实线 · Ctrl+Z 撤销");
    }

    void onCommand(WPARAM wparam, LPARAM lparam) {
        int id = LOWORD(wparam);
        int code = HIWORD(wparam);
        switch (id) {
        case ID_ICON_FIRST:case ID_ICON_FIRST+1:case ID_ICON_FIRST+2:case ID_ICON_FIRST+3:
        case ID_ICON_FIRST+4:case ID_ICON_FIRST+5:case ID_ICON_FIRST+6:case ID_ICON_FIRST+7:case ID_ICON_FIRST+8:
            if(editor.loaded&&!busy)editor.tool(toolCommand(id-ID_ICON_FIRST));
            for(int i=0;i<9;++i)InvalidateRect(GetDlgItem(hwnd,ID_ICON_FIRST+i),nullptr,FALSE);break;
        case ID_FLIP:if(editor.loaded&&!busy){editor.finishLabel();editor.session.cancel();editor.session.submit(ocsr_edit::flipConfiguration(),"review flip");editor.notify();setStatus(L"已整体翻转：实楔 ↔ 虚楔、粗实线 ↔ 粗虚线 · Ctrl+Z 撤销");}break;
        case ID_SWAP_SOLID:swapAll(false);break;
        case ID_SWAP_HASH:swapAll(true);break;
        case ID_REVERSE:editor.session.submit(bonds::reverseBondDirectionCommand(editor.bonds()),"review reverse");editor.notify();break;
        case ID_UNDO:editor.undo();break;
        case ID_REDO:editor.undo(true);break;
        case ID_SOURCE:if(!busy){selectionPx=PixelRect{0,0,0,0};pressedDetected=-1;clearResult();layout();setStatus(L"在左侧原图拖拽框选，然后识别");}break;
        case ID_DETAILS:details=!details;layout();break;
        case ID_OPEN: openFile(); break;
        case ID_PASTE: pasteClipboard(); break;
        case ID_PREV: gotoPage(page - 1); break;
        case ID_NEXT: gotoPage(page + 1); break;
        case ID_GO: {
            wchar_t text[32]{};
            studio_i18n::getWindowText(GetDlgItem(hwnd, ID_PAGE), text, 32);
            gotoPage(_wtoi(text));
            break;
        }
        case ID_DPI:
            if (code == CBN_SELCHANGE) {
                int index = int(studio_i18n::sendMessage(GetDlgItem(hwnd, ID_DPI), CB_GETCURSEL, 0, 0));
                static const int values[] = {150, 200, 300, 400};
                if (index >= 0 && index < 4) {
                    dpi = values[index];
                    if (pageLoaded) startRender();
                }
            }
            break;
        case ID_DETECT: startDetect(); break;
        case ID_RECOGNIZE: startRecognition(); break;
        case ID_EXPORT: exportCrop(); break;
        case ID_INSERT: insertResult(); break;
        case ID_COPY: copyResult(); break;
        case ID_CLOSE: DestroyWindow(hwnd); break;
        case ID_MODE_SIDE:
            sideBySide = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case ID_MODE_OVERLAY:
            sideBySide = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case ID_SHOW_OVERLAY:
            overlayVisible = studio_i18n::sendMessage(GetDlgItem(hwnd, ID_SHOW_OVERLAY), BM_GETCHECK, 0, 0) == BST_CHECKED;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case ID_ZOOM_OUT:
            {auto v=editor.session.view();v.scale/=1.25;editor.session.setView(v);editor.notify();}
            previewZoom = std::max(0.25, previewZoom / 1.25);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case ID_ZOOM_IN:
            {auto v=editor.session.view();v.scale*=1.25;editor.session.setView(v);editor.notify();}
            previewZoom = std::min(8.0, previewZoom * 1.25);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case ID_ZOOM_FIT:
            editor.fit();
            previewZoom = 1.0;
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case ID_PAGE:
            if (code == EN_CHANGE) refreshEnabled();
            break;
        }
    }

    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            return 0;
        case WM_KEYDOWN:
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (wparam == 'V' || wparam == 'C')) { if(wparam=='C')copyResult();else pasteClipboard(); return 0; }
            break;
        case WM_SIZE:
            layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = S(1000);
            info->ptMinTrackSize.y = std::min(S(620), S(480));
            return 0;
        }
        case WM_PAINT:
            onPaint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DRAWITEM: return drawTool(reinterpret_cast<DRAWITEMSTRUCT*>(lparam))?TRUE:FALSE;
        case WM_COMMAND:
            onCommand(wparam, lparam);
            return 0;
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(lparam) == GetDlgItem(hwnd, ID_OPACITY)) {
                opacity = int(studio_i18n::sendMessage(GetDlgItem(hwnd, ID_OPACITY), TBM_GETPOS, 0, 0));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
            onMouseDown(Point(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)), wparam);
            return 0;
        case WM_MOUSEMOVE:
            onMouseMove(Point(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)));
            return 0;
        case WM_LBUTTONUP:
            onMouseUp();
            return 0;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &point);
            if (hasResult && point.x >= previewRect.X && point.x <= previewRect.GetRight() &&
                point.y >= previewRect.Y && point.y <= previewRect.GetBottom()) {
                int delta = GET_WHEEL_DELTA_WPARAM(wparam);
                previewZoom = std::max(0.25, std::min(8.0, previewZoom * std::pow(1.15, delta / 120.0)));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(40, 52, 53));
            return reinterpret_cast<LRESULT>(whiteBrush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetBkColor(dc, RGB(255, 255, 255));
            SetTextColor(dc, RGB(40, 52, 53));
            return reinterpret_cast<LRESULT>(whiteBrush);
        }
        case kTaskMessage:
            onTask(reinterpret_cast<TaskResult*>(lparam));
            return 0;
        case WM_TIMER:
            if (wparam == 9) {
                KillTimer(hwnd, 9);
                insertResult();
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            closing = true;
            client.stop();
            if (worker.joinable()) worker.join();
            if (owner && IsWindow(owner) && GetForegroundWindow() == hwnd) SetForegroundWindow(owner);
            return 0;
        case WM_NCDESTROY: {
            if (memoryBitmap) {
                if (memoryPrevious) SelectObject(memoryDc, memoryPrevious);
                DeleteObject(memoryBitmap);
                memoryBitmap = nullptr;
            }
            if (memoryDc) { DeleteDC(memoryDc); memoryDc = nullptr; }
            if (font) { DeleteObject(font); font = nullptr; }
            if (whiteBrush) { DeleteObject(whiteBrush); whiteBrush = nullptr; }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete this;
            return 0;
        }
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        Dialog* dialog = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            dialog = reinterpret_cast<Dialog*>(create->lpCreateParams);
            dialog->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
        }
        if (!dialog) return DefWindowProcW(hwnd, message, wparam, lparam);
        return dialog->handleMessage(message, wparam, lparam);
    }
};

} // namespace ocsr
