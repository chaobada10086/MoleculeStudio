#ifndef R62_T19_RIBBON_UI_HPP
#define R62_T19_RIBBON_UI_HPP
// Custom-drawn Word/Excel-style ribbon. Win32 + GDI only (no MFC/COM ribbon
// framework), so it builds with the same MinGW toolchain as the rest of T19.
// Every click is forwarded to one sink; the assembly maps it to Session /
// CommandRouter, so ribbon, menu and hotkeys share a single command path.
#define WIN32_LEAN_AND_MEAN
#include "ribbon_model.hpp"
#include <windows.h>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace r62 { namespace integration { namespace ribbon {

struct State {
    std::string activeCommand;                  // currently active source tool
    std::string status;                         // status / last refusal
    std::string selectionSummary;               // e.g. "已选 6 原子 / 6 键"
    std::set<std::string> unavailable;          // source commands with a limitation
    std::set<std::string> disabled;             // item ids to draw disabled
    std::map<std::string,std::string> reason;   // item id or command -> why
    std::set<std::string> checked;              // item ids drawn as selected
    std::map<std::string,std::string> value;    // item id -> current value badge
    bool propsVisible;                          // property panel open
    int dpi;
    State():propsVisible(true),dpi(96){}
};

struct Event {
    enum class Kind { None, Command, Action, FileOp, PropertyOp };
    Kind kind;
    std::string command;   // SourceCommand command, or PropertyOp key
    int action;            // HotkeyAction for Kind::Action
    int fileOp;            // 1 new, 2 open, 3 save, 4 export
    int value;             // PropertyOp parameter
    std::string text;      // refusal text for "__unsupported__"/"__refused__"
    Event():kind(Kind::None),action(0),fileOp(0),value(0){}
};

class RibbonBar {
public:
    RibbonBar();
    ~RibbonBar();
    bool create(HWND parent,HINSTANCE inst,int dpi);
    HWND hwnd() const { return h_; }
    void setSink(const std::function<void(const Event&)>& sink) { sink_=sink; }
    void setState(const State& s) { state_=s; invalidate(); }
    const State& state() const { return state_; }
    int  heightPx() const;
    void relayout(int dpi);
    void invalidate();
    void paintInto(HDC dc,int width,int height);
    void closePopups();
    // Programmatic activation used by tests and by the host menu.
    void activateItemId(const std::string& id);
private:
    struct Hit { RECT r; const Item* item; };
    friend struct PopupData;
    void layout();
    void paint();
    void emit(const Item& it);
    void emitSource(const std::string& command);
    void openPopup(const Item& owner,RECT anchor);
    int  itemWidth(HDC dc,const Item& it) const;
    void drawItem(HDC dc,const Hit& hit,bool focus);
    void scrollBy(int dx);
    int  scale(int v) const;
    bool isEnabled(const Item& it) const;
    std::string reasonFor(const Item& it) const;
    void showTip(const Item& it,POINT screen);
    void hideTip();
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK popupProc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK tipProc(HWND,UINT,WPARAM,LPARAM);
    HWND h_,popup_,tip_;
    HWND parent_;
    HINSTANCE inst_;
    HFONT font_,fontBold_,fontSmall_,fontTab_;
    int dpi_;
    int tabH_,contentH_;
    int curTab_;
    int scrollX_;
    int focusIndex_;
    const Item* hover_;
    const Item* press_;
    int hoverTab_;
    std::vector<RECT> tabRects_;
    std::vector<Hit> hits_;
    std::vector<RECT> groupRects_;
    int contentWidth_;
    int focusTab_;
    State state_;
    std::function<void(const Event&)> sink_;
    struct PopupData* popupData_;
    std::wstring tipText_;
    bool tipVisible_;
    int hotIndex_;
    POINT lastMouse_;
    std::string lastHoverId_;
};

}}}
#endif
