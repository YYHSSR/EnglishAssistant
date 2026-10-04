#include "candidates.hpp"
#include "dictionary.hpp"
#include "options.hpp"
#include "layout.hpp"
#include "offline.hpp"
#include "startup.hpp"
#include "translation_box.hpp"
#include "documents.hpp"
#include <shellapi.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <memory>
#include <optional>
#include <thread>

namespace {
using namespace ea;
constexpr UINT UPDATE=WM_APP+1,CHOOSE=WM_APP+2,RESULT=WM_APP+3,INVALIDATE=WM_APP+4,TRAY=WM_APP+5,CONFIRM=WM_APP+6,NAVIGATE=WM_APP+7,DOCUMENT=WM_APP+8;
constexpr ULONG_PTR OWN_INPUT=0x45415353495354ULL;
enum Result {Success=0,Changed=1,CancelFailed=2,OutputFailed=3,ReaderFailed=4,Reloaded=5};
HINSTANCE instance;
HWND main_window=nullptr,popup=nullptr;
HHOOK key_hook=nullptr;
HHOOK mouse_hook=nullptr;
HWINEVENTHOOK foreground_hook=nullptr,visibility_hook=nullptr;
UINT taskbar_created=0;
HFONT normal_font=nullptr,small_font=nullptr;
HICON tray_icon=nullptr;
std::wstring folder,config_path,executable_path;
std::atomic<uint64_t> epoch{1},wake_version{0};
std::atomic<bool> paused{false},stopping{false};
std::atomic<bool> reload_personal{false};
bool committing=false;
bool swallowed_mouse=false;
bool ctrl_held=false,swallowed[256]{};
int scale_dpi=96,hover=-1,page=0,page_capacity=page_size,target_window_width=0;
struct Hit { RECT rect;int number,sense; };
std::vector<Hit> hits;
Snapshot shown;
std::vector<EnglishOption> displayed_options;
FlowLayout displayed_flow;
std::shared_ptr<OfflineTranslator> dictionary;
std::thread worker;
std::mutex mutex;
std::condition_variable wake;
Snapshot latest;
struct Choice { Snapshot snapshot;int number,sense; };
std::optional<Choice> choice;
std::optional<Choice> pending_selection;
std::ofstream diagnostics;

std::shared_ptr<OfflineTranslator> current_dictionary(){std::lock_guard<std::mutex>lock(mutex);return dictionary;}
void notify_worker(){wake_version.fetch_add(1,std::memory_order_relaxed);wake.notify_one();}
int px(int v){return MulDiv(v,scale_dpi,96);}
void balloon(const wchar_t* message){
    NOTIFYICONDATAW n{};n.cbSize=sizeof(n);n.hWnd=main_window;n.uID=1;n.uFlags=NIF_INFO;
    wcscpy_s(n.szInfoTitle,L"EnglishAssistant");wcsncpy_s(n.szInfo,message,_TRUNCATE);n.dwInfoFlags=NIIF_INFO;Shell_NotifyIconW(NIM_MODIFY,&n);
}
void tray(bool add){
    NOTIFYICONDATAW n{};n.cbSize=sizeof(n);n.hWnd=main_window;n.uID=1;n.uFlags=NIF_ICON|NIF_TIP|NIF_MESSAGE;n.hIcon=tray_icon;n.uCallbackMessage=TRAY;
    swprintf_s(n.szTip,L"EnglishAssistant · %s · %zu 条译词",paused?L"已暂停":L"运行中",current_dictionary()->english_size());
    Shell_NotifyIconW(add?NIM_ADD:NIM_MODIFY,&n);
}
bool modifiers_up(){return !(GetAsyncKeyState(VK_CONTROL)&0x8000)&&!(GetAsyncKeyState(VK_SHIFT)&0x8000)&&!(GetAsyncKeyState(VK_MENU)&0x8000)&&!(GetAsyncKeyState(VK_LWIN)&0x8000)&&!(GetAsyncKeyState(VK_RWIN)&0x8000);}
bool still_valid(const Snapshot& s){return !paused && !stopping && epoch.load()==s.epoch && same_target(s);}
const Candidate* find_candidate(const Snapshot& s,int number){for(const auto& c:s.candidates)if(c.number==number)return &c;return nullptr;}
bool send_escape(){
    INPUT in[2]{};in[0].type=in[1].type=INPUT_KEYBOARD;in[0].ki.wVk=in[1].ki.wVk=VK_ESCAPE;in[1].ki.dwFlags=KEYEVENTF_KEYUP;
    in[0].ki.dwExtraInfo=in[1].ki.dwExtraInfo=OWN_INPUT;return SendInput(2,in,sizeof(INPUT))==2;
}
bool send_unicode(const std::wstring& text){
    std::vector<INPUT> in;in.reserve(text.size()*2);
    for(wchar_t c:text){INPUT d{};d.type=INPUT_KEYBOARD;d.ki.wScan=c;d.ki.dwFlags=KEYEVENTF_UNICODE;d.ki.dwExtraInfo=OWN_INPUT;in.push_back(d);d.ki.dwFlags|=KEYEVENTF_KEYUP;in.push_back(d);}
    return !in.empty() && SendInput((UINT)in.size(),in.data(),sizeof(INPUT))==in.size();
}
Result commit(CandidateReader& reader,const Choice& request){
    const auto& old=request.snapshot;
    ULONGLONG start=GetTickCount64();
    // Let physical modifiers be released instead of fabricating key releases.
    while(!modifiers_up()){
        if(!still_valid(old)||GetTickCount64()-start>1200)return Changed;
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
    if(!still_valid(old))return Changed;
    auto fresh=reader.read(old.epoch);
    if(!same_candidates(old,fresh)||!still_valid(old)||!reader.focus_matches(old))return Changed;
    auto c=find_candidate(old,request.number);
    if(!c||request.sense<0||request.sense>=(int)c->senses.size())return Changed;
    if(!send_escape())return OutputFailed;
    start=GetTickCount64();int confirmed=0;
    while(GetTickCount64()-start<1000){
        if(!still_valid(old)||!modifiers_up())return Changed;
        if(reader.candidate_gone(fresh.host))++confirmed;else confirmed=0;
        if(confirmed>=2){
            if(!still_valid(old)||!modifiers_up()||!reader.focus_matches(old))return Changed;
            return send_unicode(c->senses[request.sense])?Success:OutputFailed;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(18));
    }
    return CancelFailed;
}
void publish(Snapshot s){
    {std::lock_guard<std::mutex> lock(mutex);latest=std::move(s);}
    PostMessageW(main_window,UPDATE,0,0);
}
void work(){
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(hr)){PostMessageW(main_window,RESULT,ReaderFailed,0);return;}
    {
        CandidateReader reader;
        if(!reader.available()){PostMessageW(main_window,RESULT,ReaderFailed,0);}
        bool active=false;uint64_t seen=0;
        while(reader.available() && !stopping){
            std::optional<Choice> request;
            {std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock,std::chrono::milliseconds(active?130:1800),[&]{return stopping || choice.has_value() || wake_version.load()!=seen;});
                seen=wake_version.load();if(stopping)break;
                request=std::move(choice);choice.reset();
            }
            if(request){
                auto result=commit(reader,*request);publish({});PostMessageW(main_window,RESULT,result,0);active=false;continue;
            }
            if(reload_personal.exchange(false)){
                auto replacement=std::make_shared<OfflineTranslator>();
                if(replacement->open(folder)){std::lock_guard<std::mutex>lock(mutex);dictionary=std::move(replacement);PostMessageW(main_window,RESULT,Reloaded,0);}
            }
            if(paused){if(active)publish({});active=false;continue;}
            uint64_t version=epoch.load();auto s=reader.read(version);
            if(version!=epoch.load()||paused){publish({});active=false;continue;}
            auto local=current_dictionary();for(auto& c:s.candidates){auto reply=local->translate_to_english(c.word);c.senses=std::move(reply.senses);c.reference=reply.reference;}
            s.time=GetTickCount64();active=s.valid();publish(std::move(s));
        }
    }
    CoUninitialize();
}
void invalidate(){epoch.fetch_add(1);PostMessageW(main_window,INVALIDATE,0,0);notify_worker();}
LRESULT CALLBACK keyboard(int code,WPARAM w,LPARAM l){
    if(code<0)return CallNextHookEx(key_hook,code,w,l);
    auto* key=(KBDLLHOOKSTRUCT*)l;
    if(key->dwExtraInfo==OWN_INPUT)return CallNextHookEx(key_hook,code,w,l);
    DWORD vk=key->vkCode;
    bool up=w==WM_KEYUP||w==WM_SYSKEYUP;
    bool arrow=vk==VK_UP||vk==VK_DOWN||vk==VK_LEFT||vk==VK_RIGHT;
    if(vk<256&&swallowed[vk]){if(up){swallowed[vk]=false;return 1;}if(!arrow)return 1;}
    if(vk==VK_CONTROL||vk==VK_LCONTROL||vk==VK_RCONTROL){
        if(up){ctrl_held=(GetAsyncKeyState(vk==VK_RCONTROL?VK_LCONTROL:VK_RCONTROL)&0x8000)!=0;if(!ctrl_held)PostMessageW(main_window,CONFIRM,0,0);}else ctrl_held=true;
    }
    if(up||paused)return CallNextHookEx(key_hook,code,w,l);
    bool control=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
    bool alt=(GetAsyncKeyState(VK_MENU)&0x8000)!=0;
    bool win=(GetAsyncKeyState(VK_LWIN)&0x8000)||(GetAsyncKeyState(VK_RWIN)&0x8000);
    int number=vk>='1'&&vk<='9'?(int)(vk-'0'):(vk>=VK_NUMPAD1&&vk<=VK_NUMPAD9?(int)(vk-VK_NUMPAD0):0);
    bool shift=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
    if(control && !alt && !shift && !win && !committing && IsWindowVisible(popup) && shown.valid() && shown.epoch==epoch.load() && GetTickCount64()-shown.time<700 && same_target(shown)){
        int index=page*page_capacity+number-1;
        if(number && index<(int)displayed_options.size()){
            swallowed[vk]=true;PostMessageW(main_window,CHOOSE,number,-1);return 1;
        }
        if(arrow&&!displayed_options.empty()){swallowed[vk]=true;PostMessageW(main_window,NAVIGATE,(vk==VK_DOWN||vk==VK_RIGHT)?1:0,0);return 1;}
    }
    if(vk<256&&swallowed[vk])return 1;
    if(vk!=VK_CONTROL&&vk!=VK_LCONTROL&&vk!=VK_RCONTROL&&vk!=VK_SHIFT&&vk!=VK_LSHIFT&&vk!=VK_RSHIFT&&vk!=VK_MENU&&vk!=VK_LMENU&&vk!=VK_RMENU&&vk!=VK_LWIN&&vk!=VK_RWIN)invalidate();
    return CallNextHookEx(key_hook,code,w,l);
}
LRESULT CALLBACK mouse(int code,WPARAM w,LPARAM l){
    if(code<0)return CallNextHookEx(mouse_hook,code,w,l);
    if(w==WM_LBUTTONUP && swallowed_mouse){swallowed_mouse=false;return 1;}
    if(paused)return CallNextHookEx(mouse_hook,code,w,l);
    if(w!=WM_LBUTTONDOWN && w!=WM_RBUTTONDOWN && w!=WM_MBUTTONDOWN)return CallNextHookEx(mouse_hook,code,w,l);
    if(w!=WM_LBUTTONDOWN || committing || !shown.valid() || shown.epoch!=epoch.load() || !same_target(shown)){invalidate();return CallNextHookEx(mouse_hook,code,w,l);}
    auto* event=(MSLLHOOKSTRUCT*)l;
    if(WindowFromPoint(event->pt)!=popup){invalidate();return CallNextHookEx(mouse_hook,code,w,l);}
    POINT point=event->pt;ScreenToClient(popup,&point);
    for(const auto&hit:hits)if(PtInRect(&hit.rect,point)){
        // Consume only clicks on our English cells. The IME never receives an
        // outside click that could dismiss/commit its Chinese composition.
        swallowed_mouse=true;PostMessageW(main_window,CHOOSE,hit.number,hit.sense);return 1;
    }
    // Empty space and Chinese group labels belong to our panel too. Consume
    // their clicks without selecting, so the IME cannot commit Chinese text.
    swallowed_mouse=true;return 1;
}
void CALLBACK foreground_event(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){invalidate();}
void CALLBACK visibility_event(HWINEVENTHOOK,DWORD,HWND h,LONG,LONG,DWORD,DWORD){
    wchar_t cls[80]{};GetClassNameW(h,cls,80);
    if(wcscmp(cls,L"Windows.UI.Core.CoreWindow")==0||wcscmp(cls,L"ApplicationFrameWindow")==0)notify_worker();
}
void fonts(){
    if(normal_font)DeleteObject(normal_font);
    if(small_font)DeleteObject(small_font);
    normal_font=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    small_font=CreateFontW(-px(11),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
}
int page_count(){return std::max(1,((int)displayed_options.size()+page_capacity-1)/page_capacity);}
std::vector<int> measure_options(){
    HDC dc=GetDC(popup);auto old=SelectObject(dc,normal_font);std::vector<int> widths;
    for(const auto&o:displayed_options){SIZE size{};GetTextExtentPoint32W(dc,o.text.c_str(),(int)o.text.size(),&size);widths.push_back(size.cx);}
    SelectObject(dc,old);ReleaseDC(popup,dc);return widths;
}
std::wstring footer(){
    int missing=0;for(const auto&c:shown.candidates)if(c.senses.empty())++missing;
    std::wstring text;
    if(page_count()>1)text=std::to_wstring(page+1)+L" / "+std::to_wstring(page_count())+L"  ·  方向键跨页选词";
    if(missing){if(!text.empty())text+=L"    ";text+=L"部分短句未收录 · 可添加到个人词表";}
    return text;
}
void position_popup(){
    if(!shown.valid()||shown.epoch!=epoch.load()||!same_target(shown)||paused){ShowWindow(popup,SW_HIDE);hits.clear();return;}
    auto monitor=MonitorFromRect(&shown.bounds,MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(monitor,&mi);
    UINT dpi=96,dpi_y=96;GetDpiForMonitor(monitor,MDT_EFFECTIVE_DPI,&dpi,&dpi_y);
    if(scale_dpi!=(int)dpi){scale_dpi=(int)dpi;fonts();}
    bool missing=false;for(const auto&c:shown.candidates)if(c.senses.empty())missing=true;
    RECT target_bounds{};GetWindowRect(shown.foreground,&target_bounds);
    target_window_width=(int)(target_bounds.right-target_bounds.left);
    auto layout=popup_layout(shown.bounds,mi.rcWork,scale_dpi,displayed_options,measure_options(),page,missing,target_window_width);
    if(!layout.capacity){ShowWindow(popup,SW_HIDE);hits.clear();return;}
    page_capacity=layout.capacity;page=layout.page;
    displayed_flow=std::move(layout.flow);
    const auto&r=layout.bounds;
    SetWindowPos(popup,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    InvalidateRect(popup,nullptr,FALSE);
}
void draw_text(HDC dc,const std::wstring& text,RECT r,COLORREF color,HFONT font){
    SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);
    DrawTextW(dc,text.c_str(),(int)text.size(),&r,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
}
void rounded(HDC dc,RECT r,COLORREF fill){
    auto brush=CreateSolidBrush(fill);auto oldBrush=SelectObject(dc,brush);auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
    RoundRect(dc,r.left,r.top,r.right,r.bottom,px(10),px(10));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);
}
void render(HDC dc,RECT client){
    HDC mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,client.right,client.bottom);auto old=SelectObject(mem,bmp);
    HBRUSH bg=CreateSolidBrush(RGB(249,250,254));FillRect(mem,&client,bg);DeleteObject(bg);
    DrawIconEx(mem,px(14),px(12),tray_icon,px(20),px(20),0,nullptr,DI_NORMAL);
    RECT title{px(42),px(8),client.right-px(12),px(35)};
    draw_text(mem,L"英文  ·  Ctrl + 数字 / 方向键，松开输出",title,RGB(85,92,112),small_font);
    hits.clear();
    for(const auto&group:displayed_flow.groups){
        rounded(mem,group.bounds,RGB(255,255,255));
        RECT chinese{group.bounds.left+px(10),group.bounds.top+px(2),group.bounds.right-px(10),group.bounds.top+px(22)};
        draw_text(mem,group.word,chinese,RGB(139,145,162),small_font);
    }
    for(size_t cell=0;cell<displayed_flow.cells.size();++cell){
        const auto&placement=displayed_flow.cells[cell];const auto&o=displayed_options[placement.index];
        const auto&card=placement.bounds;
        bool selected=pending_selection&&pending_selection->number==o.candidate&&pending_selection->sense==o.sense;
        rounded(mem,card,selected?RGB(221,227,254):(hover==(int)cell?RGB(237,240,255):RGB(255,255,255)));
        hits.push_back({card,o.candidate,o.sense});
        RECT badge{card.left+px(3),card.top+px(4),card.left+px(25),card.bottom-px(4)};rounded(mem,badge,selected?RGB(77,87,210):RGB(237,239,252));
        RECT num=badge;num.left+=px(6);draw_text(mem,std::to_wstring(placement.index-page*page_capacity+1),num,selected?RGB(255,255,255):RGB(82,89,186),small_font);
        RECT english{card.left+px(31),card.top,card.right-px(6),card.bottom};
        draw_text(mem,o.text,english,RGB(46,55,123),normal_font);
    }
    RECT foot{px(16),client.bottom-px(26),client.right-px(12),client.bottom-px(4)};
    draw_text(mem,footer(),foot,RGB(121,130,151),small_font);
    HBRUSH border=CreateSolidBrush(RGB(218,222,237));FrameRect(mem,&client,border);DeleteObject(border);
    BitBlt(dc,0,0,client.right,client.bottom,mem,0,0,SRCCOPY);
    SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);
}
void paint(HWND h){PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT client;GetClientRect(h,&client);render(dc,client);EndPaint(h,&ps);}
bool render_preview(const std::wstring& path,int width){
    // Render sample content into a bitmap without opening a window or installing hooks.
    auto local=current_dictionary();
    shown.candidates={{1,L"托盘中增加一个选项",true,local->to_english(L"托盘中增加一个选项")},{2,L"托盘中",false,local->to_english(L"托盘中")},{3,L"托盘",false,local->to_english(L"托盘")},{4,L"托",false,local->to_english(L"托")}};
    displayed_options=english_options(shown);pending_selection=Choice{shown,1,0};
    displayed_flow=grouped_flow(displayed_options,measure_options(),0,page_capacity,width,scale_dpi);
    RECT client{0,0,width,displayed_flow.height+8};
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=client.right;info.bmiHeader.biHeight=client.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap){DeleteDC(dc);ReleaseDC(nullptr,screen);return false;}
    auto old=SelectObject(dc,bitmap);render(dc,client);GdiFlush();
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+client.right*client.bottom*4;
    std::ofstream file{std::filesystem::path(path),std::ios::binary};file.write((const char*)&header,sizeof(header));file.write((const char*)&info.bmiHeader,sizeof(BITMAPINFOHEADER));file.write((const char*)bits,client.right*client.bottom*4);bool ok=(bool)file;
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);return ok;
}
LRESULT CALLBACK popup_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_PAINT){paint(h);return 0;}
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_MOUSEMOVE){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};int now=-1;for(size_t i=0;i<hits.size();i++)if(PtInRect(&hits[i].rect,p)){now=(int)i;break;}if(now!=hover){hover=now;InvalidateRect(h,nullptr,FALSE);}TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);SetCursor(LoadCursor(nullptr,now>=0?IDC_HAND:IDC_ARROW));return 0;}
    if(m==WM_MOUSELEAVE){hover=-1;InvalidateRect(h,nullptr,FALSE);return 0;}
    if(m==WM_LBUTTONUP){POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};for(const auto& hit:hits)if(PtInRect(&hit.rect,p)){PostMessageW(main_window,CHOOSE,hit.number,hit.sense);break;}return 0;}
    return DefWindowProcW(h,m,w,l);
}
HICON make_icon(){return (HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,LR_DEFAULTCOLOR);}
void document(bool editable){
    show_document(instance,main_window,folder+(editable?L"\\personal.tsv":L"\\使用说明.md"),editable?L"EnglishAssistant — 个人词表":L"EnglishAssistant — 使用说明",editable,[]{reload_personal=true;notify_worker();});
}
void menu(){
    epoch.fetch_add(1);shown={};pending_selection.reset();ShowWindow(popup,SW_HIDE);
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,paused?L"恢复英文候选":L"暂停英文候选");
    AppendMenuW(menu,MF_STRING|(startup_enabled(executable_path)?MF_CHECKED:0),10,L"开机自启动");
    AppendMenuW(menu,MF_STRING,11,L"英文 → 中文翻译框");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,4,L"编辑个人词表");AppendMenuW(menu,MF_STRING,5,L"重新加载个人词表");
    AppendMenuW(menu,MF_STRING,7,L"使用说明");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,8,L"退出");
    POINT p;GetCursorPos(&p);SetForegroundWindow(main_window);int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,p.x,p.y,0,main_window,nullptr);DestroyMenu(menu);PostMessageW(main_window,WM_NULL,0,0);
    if(cmd==1){paused=!paused;tray(false);notify_worker();}
    if(cmd==10){bool enable=!startup_enabled(executable_path);if(set_startup(executable_path,enable))WritePrivateProfileStringW(L"startup",L"enabled",enable?L"1":L"0",config_path.c_str());else balloon(L"无法修改当前用户的开机自启动设置。");}
    if(cmd==11)show_translation_box(instance,main_window,current_dictionary);
    if(cmd==4)document(true);
    if(cmd==5){reload_personal=true;notify_worker();}
    if(cmd==7)document(false);
    if(cmd==8)DestroyWindow(main_window);
}
bool visual_equal(const Snapshot&a,const Snapshot&b){
    if(!a.valid()&&!b.valid())return true;
    if(!same_candidates(a,b)||memcmp(&a.bounds,&b.bounds,sizeof(RECT))!=0)return false;
    for(size_t i=0;i<a.candidates.size();i++)if(a.candidates[i].selected!=b.candidates[i].selected||a.candidates[i].senses!=b.candidates[i].senses||a.candidates[i].reference!=b.candidates[i].reference)return false;
    return true;
}
LRESULT CALLBACK main_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==DOCUMENT){if(w==2)show_translation_box(instance,main_window,current_dictionary);else document(w!=0);return 0;}
    if(m==taskbar_created && taskbar_created){tray(true);return 0;}
    if(m==UPDATE){Snapshot s;{std::lock_guard<std::mutex> lock(mutex);s=latest;}
        if(s.valid() && (s.epoch!=epoch.load()||paused||committing||!same_target(s)))s={};
        // Keep the displayed numbering stable while the user holds Ctrl.
        if(ctrl_held&&shown.valid()&&s.valid()&&same_candidates(shown,s)){shown.time=s.time;return 0;}
        if(!same_candidates(shown,s)){page=0;pending_selection.reset();}
        bool changed=!visual_equal(shown,s);shown=std::move(s);
        if(changed){displayed_options=english_options(shown);hover=-1;}
        if(diagnostics && changed){diagnostics<<"snapshot epoch="<<shown.epoch<<" foreground="<<shown.foreground<<" bounds="<<shown.bounds.left<<","<<shown.bounds.top<<","<<shown.bounds.right<<","<<shown.bounds.bottom<<"\n";for(const auto&c:shown.candidates){diagnostics<<c.number<<"\t"<<utf8(c.word);for(const auto&v:c.senses)diagnostics<<"\t"<<utf8(v);diagnostics<<"\n";}diagnostics.flush();}
        RECT target_bounds{};if(shown.valid())GetWindowRect(shown.foreground,&target_bounds);
        bool resized=(int)(target_bounds.right-target_bounds.left)!=target_window_width;
        if(changed||!shown.valid()||resized)position_popup();else InvalidateRect(popup,nullptr,FALSE);
        return 0;
    }
    if(m==INVALIDATE){shown={};pending_selection.reset();page=0;ShowWindow(popup,SW_HIDE);hits.clear();return 0;}
    if(m==NAVIGATE){
        if(!shown.valid()||!still_valid(shown)||committing||displayed_options.empty())return 0;
        int current=-1;
        if(pending_selection)for(size_t i=0;i<displayed_options.size();++i)
            if(displayed_options[i].candidate==pending_selection->number&&displayed_options[i].sense==pending_selection->sense){current=(int)i;break;}
        int index=move_selection(current,w?1:-1,(int)displayed_options.size(),page*page_capacity);
        const auto&o=displayed_options[index];pending_selection=Choice{shown,o.candidate,o.sense};
        page=index/page_capacity;hover=-1;position_popup();
        if(!ctrl_held)PostMessageW(main_window,CONFIRM,0,0);
        return 0;
    }
    if(m==CHOOSE){
        if(!shown.valid()||shown.epoch!=epoch.load()||!same_target(shown)||paused||committing)return 0;
        if(l==-1){int index=page*page_capacity+(int)w-1;if(index<0||index>=(int)displayed_options.size())return 0;w=displayed_options[index].candidate;l=displayed_options[index].sense;}
        auto c=find_candidate(shown,(int)w);if(!c||l<0||l>=(LPARAM)c->senses.size())return 0;
        pending_selection=Choice{shown,(int)w,(int)l};InvalidateRect(popup,nullptr,FALSE);
        if(!ctrl_held)PostMessageW(main_window,CONFIRM,0,0);
        return 0;
    }
    if(m==CONFIRM){
        if(ctrl_held)return 0;
        if(!pending_selection){notify_worker();return 0;}
        if(committing||!still_valid(pending_selection->snapshot)){pending_selection.reset();return 0;}
        {std::lock_guard<std::mutex> lock(mutex);if(!choice)choice=std::move(pending_selection);}
        pending_selection.reset();
        committing=true;shown={};ShowWindow(popup,SW_HIDE);hits.clear();notify_worker();return 0;
    }
    if(m==RESULT){
        if(w!=Reloaded)committing=false;
        if(diagnostics){diagnostics<<"commit-result="<<w<<"\n";diagnostics.flush();}
        if(w==Changed)balloon(L"输入状态或焦点已经变化，本次未输出英文。请重新选择。");
        if(w==CancelFailed)balloon(L"未能确认拼音已取消，本次未输出英文。");
        if(w==OutputFailed)balloon(L"应用未接受输入。请检查目标应用是否以管理员身份运行。");
        if(w==ReaderFailed)balloon(L"无法初始化候选读取接口，请退出后重试。");
        if(w==Reloaded)balloon(L"个人词表已重新加载。");
        return 0;
    }
    if(m==TRAY){if(l==WM_RBUTTONUP||l==WM_CONTEXTMENU||l==WM_LBUTTONUP)menu();return 0;}
    if(m==WM_QUERYENDSESSION)return TRUE;
    if(m==WM_ENDSESSION&&w){DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){
        close_translation_box();
        stopping=true;wake.notify_one();
        if(key_hook)UnhookWindowsHookEx(key_hook);
        if(mouse_hook)UnhookWindowsHookEx(mouse_hook);
        if(foreground_hook)UnhookWinEvent(foreground_hook);
        if(visibility_hook)UnhookWinEvent(visibility_hook);
        NOTIFYICONDATAW n{};n.cbSize=sizeof(n);n.hWnd=h;n.uID=1;Shell_NotifyIconW(NIM_DELETE,&n);ShowWindow(popup,SW_HIDE);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE i,HINSTANCE,LPWSTR,int){
    instance=i;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
    for(int a=1;a<argc;a++)if(wcscmp(args[a],L"--quit")==0){HWND existing=FindWindowW(L"EnglishAssistant.Tray",nullptr);if(existing)PostMessageW(existing,WM_CLOSE,0,0);LocalFree(args);return 0;}
    int requested_document=0;for(int a=1;a<argc;a++){if(wcscmp(args[a],L"--help")==0)requested_document=1;if(wcscmp(args[a],L"--edit-personal")==0)requested_document=2;if(wcscmp(args[a],L"--translate")==0)requested_document=3;}
    HANDLE singleton=CreateMutexW(nullptr,FALSE,L"Local\\EnglishAssistant.2026.v1");
    if(!singleton){LocalFree(args);return 1;}
    if(GetLastError()==ERROR_ALREADY_EXISTS){if(requested_document){HWND existing=FindWindowW(L"EnglishAssistant.Tray",nullptr);if(existing)PostMessageW(existing,DOCUMENT,requested_document-1,0);}else MessageBoxW(nullptr,L"EnglishAssistant 已经运行。请在任务栏托盘找到沃雅妮莎头像。",L"EnglishAssistant",MB_OK|MB_ICONINFORMATION);LocalFree(args);CloseHandle(singleton);return 0;}
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);executable_path=path;folder=std::filesystem::path(path).parent_path().wstring();config_path=folder+L"\\settings.ini";
    std::wstring preview_path;
    int preview_width=560;
    for(int a=1;a+1<argc;a++)if(wcscmp(args[a],L"--render-preview")==0)preview_path=args[a+1];
    for(int a=1;a+1<argc;a++)if(wcscmp(args[a],L"--preview-width")==0)preview_width=std::clamp(_wtoi(args[a+1]),240,560);
    for(int a=1;a+1<argc;a++)if(wcscmp(args[a],L"--diagnostic")==0)diagnostics.open(std::filesystem::path(args[a+1]),std::ios::trunc);
    LocalFree(args);
    CopyFileW((folder+L"\\personal.example.tsv").c_str(),(folder+L"\\personal.tsv").c_str(),TRUE);
    dictionary=std::make_shared<OfflineTranslator>();
    if(!dictionary->open(folder)){MessageBoxW(nullptr,L"无法读取 data 中的中英、英中词库。请保留完整项目目录后运行。",L"EnglishAssistant",MB_OK|MB_ICONERROR);CloseHandle(singleton);return 1;}
    taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");fonts();tray_icon=make_icon();
    if(!preview_path.empty()){bool ok=render_preview(preview_path,preview_width);DeleteObject(normal_font);DeleteObject(small_font);DestroyIcon(tray_icon);CloseHandle(singleton);return ok?0:1;}
    if(GetPrivateProfileIntW(L"startup",L"enabled",0,config_path.c_str())!=0)set_startup(executable_path,true);
    WritePrivateProfileStringW(L"network",nullptr,nullptr,config_path.c_str());
    WNDCLASSW cls{};cls.lpfnWndProc=main_proc;cls.hInstance=i;cls.hIcon=tray_icon;cls.lpszClassName=L"EnglishAssistant.Tray";RegisterClassW(&cls);
    main_window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"EnglishAssistant",WS_POPUP,0,0,0,0,nullptr,nullptr,i,nullptr);
    cls.lpfnWndProc=popup_proc;cls.lpszClassName=L"EnglishAssistant.Popup";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&cls);
    popup=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"英文候选",WS_POPUP,0,0,0,0,main_window,nullptr,i,nullptr);
    if(!main_window||!popup){CloseHandle(singleton);return 1;}
    tray(true);
    if(requested_document)PostMessageW(main_window,DOCUMENT,requested_document-1,0);
    key_hook=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard,i,0);
    mouse_hook=SetWindowsHookExW(WH_MOUSE_LL,mouse,i,0);
    foreground_hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,foreground_event,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    visibility_hook=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_HIDE,nullptr,visibility_event,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    if(!key_hook||!mouse_hook){balloon(L"输入监听启动失败。请退出后重试。");DestroyWindow(main_window);}else worker=std::thread(work);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    stopping=true;wake.notify_one();if(worker.joinable())worker.join();DestroyWindow(popup);DeleteObject(normal_font);DeleteObject(small_font);DestroyIcon(tray_icon);CloseHandle(singleton);return 0;
}
