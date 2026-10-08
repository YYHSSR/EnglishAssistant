#include "translation_box.hpp"
#include "neural.hpp"
#include "background.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <mutex>
#include <thread>
#include <array>
#include <algorithm>
#include <cwctype>
namespace ea {
namespace {
constexpr UINT translated_message=WM_APP+40;
constexpr UINT_PTR background_timer=1,translate_timer=2,idle_timer=3;
std::atomic<ULONG_PTR> tickets{0};
HWND translation_window=nullptr;
struct State {std::mutex mutex;HWND window=nullptr;std::wstring text,status;uint64_t revision=0;std::atomic<bool> cancelled{false};};
struct Box {
    HWND window=nullptr,input=nullptr,output=nullptr,button=nullptr,direction=nullptr,clear=nullptr,tooltip=nullptr;
    std::array<HWND,3> caption{};
    HICON icon=nullptr;
    std::wstring status;int input_wheel=0,output_wheel=0;
    HFONT font=nullptr;int dpi=96;DictionaryProvider provider;std::wstring root,appearance_root;Background background;HBRUSH paper=nullptr;
    std::shared_ptr<State> state=std::make_shared<State>();std::thread worker;
    std::unique_ptr<NeuralTranslator> engine;uint64_t revision=0;ULONG_PTR ticket=0;bool busy=false,pending=false,scheduled=false;
    int direction_mode=0;bool internal_output=false;
    struct Panel {HWND control=nullptr;HBRUSH brush=nullptr;};std::array<Panel,2> panels{};
    HDC surface=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;int width=0,height=0,opacity=40;bool dirty=true;
    ~Box(){for(auto& p:panels)if(p.brush)DeleteObject(p.brush);if(surface){SelectObject(surface,previous);DeleteObject(bitmap);DeleteDC(surface);}}
    int px(int value)const{return MulDiv(value,dpi,96);}
    int title_height()const{return px(40);}
    void reload(bool media=true){if(media){background.load(background_path(appearance_root,BackgroundKind::Translation),background_sound(appearance_root,BackgroundKind::Translation));background.visible(IsWindowVisible(window));KillTimer(window,1);if(IsWindowVisible(window)&&background.animated())SetTimer(window,1,67,nullptr);}opacity=background_opacity(appearance_root,BackgroundKind::Translation);dirty=true;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}
};
RECT panel_rect(Box& b,HWND control){RECT r{};GetClientRect(control,&r);MapWindowPoints(control,b.window,(POINT*)&r,2);return r;}
void set_status(Box& b,const std::wstring& text){
    b.status=text;if(!b.tooltip)return;
    TOOLINFOW info{};info.cbSize=sizeof(info);info.hwnd=b.window;info.uId=(UINT_PTR)b.button;info.lpszText=b.status.data();
    SendMessageW(b.tooltip,TTM_UPDATETIPTEXTW,0,(LPARAM)&info);
}
void prepare_surface(Box& b){
    if(!b.dirty)return;
    RECT r{};GetClientRect(b.window,&r);if(r.right<=0||r.bottom<=0)return;
    if(!b.surface||b.width!=r.right||b.height!=r.bottom){
        if(b.surface){SelectObject(b.surface,b.previous);DeleteObject(b.bitmap);DeleteDC(b.surface);}
        auto dc=GetDC(b.window);b.surface=CreateCompatibleDC(dc);b.bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);ReleaseDC(b.window,dc);b.previous=SelectObject(b.surface,b.bitmap);b.width=r.right;b.height=r.bottom;
    }
    FillRect(b.surface,&r,b.paper);auto scene=r;scene.top=b.title_height();b.background.paint(b.surface,scene);
    if(b.icon)DrawIconEx(b.surface,b.px(8),b.px(10),b.icon,b.px(20),b.px(20),0,nullptr,DI_NORMAL);
    auto old_font=SelectObject(b.surface,b.font);SetBkMode(b.surface,TRANSPARENT);SetTextColor(b.surface,RGB(65,94,110));RECT title{b.px(34),0,b.px(88),b.title_height()};DrawTextW(b.surface,L"翻译框",-1,&title,DT_LEFT|DT_VCENTER|DT_SINGLELINE);SelectObject(b.surface,old_font);
    for(auto& p:b.panels){auto area=panel_rect(b,p.control);paint_glass(b.surface,area,b.opacity,b.px(10));}
    // Native EDIT controls use the same composited pixels as the parent.
    // Pattern brushes preserve normal hit testing, IME, selection and clipboard behavior.
    for(auto& p:b.panels){
        auto area=panel_rect(b,p.control);int width=area.right-area.left,height=area.bottom-area.top;if(width<=0||height<=0)continue;
        auto dc=CreateCompatibleDC(b.surface);auto bmp=CreateCompatibleBitmap(b.surface,width,height);auto old=SelectObject(dc,bmp);BitBlt(dc,0,0,width,height,b.surface,area.left,area.top,SRCCOPY);
        auto brush=CreatePatternBrush(bmp);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);if(p.brush)DeleteObject(p.brush);p.brush=brush;
    }
    b.dirty=false;
}
void layout(Box& b){
    RECT r{};GetClientRect(b.window,&r);int margin=b.px(16),top=b.title_height(),half=(r.bottom-top-b.px(48))/2;int content=(r.right-2*margin)*76/100;
    MoveWindow(b.clear,b.px(94),b.px(6),b.px(80),b.px(28),FALSE);
    MoveWindow(b.direction,b.px(182),b.px(6),b.px(148),b.px(28),FALSE);
    MoveWindow(b.button,b.px(338),b.px(6),b.px(92),b.px(28),FALSE);
    for(int i=0;i<3;++i)MoveWindow(b.caption[i],r.right-b.px(132-i*44),0,b.px(44),top,FALSE);
    MoveWindow(b.input,margin,top+margin,content,half,FALSE);
    MoveWindow(b.output,margin,top+b.px(32)+half,content,half,FALSE);
    for(auto control:{b.input,b.output}){RECT area{};GetClientRect(control,&area);InflateRect(&area,-b.px(10),-b.px(8));SendMessageW(control,EM_SETRECTNP,0,(LPARAM)&area);}
    b.dirty=true;RedrawWindow(b.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
std::wstring input_text(Box& b){int length=GetWindowTextLengthW(b.input);std::wstring text(length+1,L'\0');GetWindowTextW(b.input,text.data(),length+1);text.resize(length);return text;}
void set_output(Box& b,const wchar_t* text){b.internal_output=true;SetWindowTextW(b.output,text);b.internal_output=false;}
TranslationDirection direction_for(Box& b,const std::wstring& text){
    if(b.direction_mode==1)return TranslationDirection::EnglishToChinese;
    if(b.direction_mode==2)return TranslationDirection::ChineseToEnglish;
    size_t chinese=0,latin=0;for(auto c:text){if((c>=0x3400&&c<=0x9fff)||(c>=0xf900&&c<=0xfaff))++chinese;else if((c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z'))++latin;}
    return chinese&&chinese*2>=latin?TranslationDirection::ChineseToEnglish:TranslationDirection::EnglishToChinese;
}
void schedule(Box& b){
    ++b.revision;b.state->cancelled=true;b.pending=false;b.scheduled=false;KillTimer(b.window,translate_timer);KillTimer(b.window,idle_timer);
    set_output(b,L"");SetWindowTextW(b.button,L"复制译文");EnableWindow(b.button,FALSE);auto text=input_text(b);
    if(text.empty()||std::all_of(text.begin(),text.end(),iswspace)){set_status(b,L"输入或粘贴文本后自动翻译");SetTimer(b.window,idle_timer,60000,nullptr);return;}
    b.scheduled=true;SetTimer(b.window,translate_timer,450,nullptr);set_status(b,L"等待输入结束…");
}
void translate(Box& b){
    KillTimer(b.window,translate_timer);b.scheduled=false;
    if(b.busy){b.pending=true;b.state->cancelled=true;return;}
    auto text=input_text(b);if(text.empty()||std::all_of(text.begin(),text.end(),iswspace))return;
    if(b.worker.joinable())b.worker.join();
    b.state->cancelled=false;b.busy=true;b.pending=false;b.ticket=++tickets;auto direction=direction_for(b,text);auto dictionary=b.provider();
    EnableWindow(b.button,FALSE);SetWindowTextW(b.button,L"正在翻译…");set_status(b,direction==TranslationDirection::ChineseToEnglish?L"中文 → 英文 · 正在本机翻译…":L"英文 → 中文 · 正在本机翻译…");set_output(b,L"");
    b.worker=std::thread([state=b.state,dictionary,engine=b.engine.get(),direction,revision=b.revision,ticket=b.ticket,text=std::move(text)]{
        std::wstring result,status;
        try{
            if(direction==TranslationDirection::EnglishToChinese){auto exact=dictionary->to_chinese(text);if(exact.exact)result=std::move(exact.text);}
            else{auto exact=dictionary->to_english(text);if(!exact.empty()&&valid_english_output(exact[0]))result=exact[0];}
            if(!result.empty())status=L"已匹配本地词条 · 可点击复制译文";
            else{auto reply=engine->translate(text,state->cancelled,direction);result=std::move(reply.text);status=reply.error.empty()?L"本地模型译文 · 请核对专名及专业术语":L"翻译失败："+reply.error;}
        }
        catch(const std::exception&){status=L"翻译失败，请检查本地模型文件。";}
        if(direction==TranslationDirection::ChineseToEnglish&&!result.empty()&&!valid_english_output(result)){result.clear();status=L"未生成完整英文，请补全中文句子后重试。";}
        std::wstring windows_text;for(size_t i=0;i<result.size();++i){if(result[i]==L'\n'&&(i==0||result[i-1]!=L'\r'))windows_text+=L'\r';windows_text+=result[i];}
        std::lock_guard<std::mutex>lock(state->mutex);state->text=std::move(windows_text);state->status=std::move(status);state->revision=revision;if(state->window)PostMessageW(state->window,translated_message,ticket,0);
    });
}
void copy_result(Box& b){
    int length=GetWindowTextLengthW(b.output);if(!length||!IsWindowEnabled(b.button))return;
    auto memory=GlobalAlloc(GMEM_MOVEABLE,(length+1)*sizeof(wchar_t));if(!memory)return;auto* text=(wchar_t*)GlobalLock(memory);if(!text){GlobalFree(memory);return;}GetWindowTextW(b.output,text,length+1);GlobalUnlock(memory);
    if(!OpenClipboard(b.window)){GlobalFree(memory);SetWindowTextW(b.button,L"重试复制");set_status(b,L"剪贴板暂时被占用，请再点击一次复制。");return;}
    bool copied=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory);CloseClipboard();if(!copied)GlobalFree(memory);SetWindowTextW(b.button,copied?L"已复制":L"重试复制");set_status(b,copied?L"已复制译文":L"复制失败，请重试。");
}
void set_direction(Box& b,int mode){b.direction_mode=mode;SetWindowTextW(b.direction,mode==1?L"英文 → 中文 ▾":mode==2?L"中文 → 英文 ▾":L"自动识别方向 ▾");schedule(b);}
void direction_menu(Box& b){
    auto menu=CreatePopupMenu();for(int mode=0;mode<3;++mode)AppendMenuW(menu,MF_STRING|(b.direction_mode==mode?MF_CHECKED:0),211+mode,mode==1?L"英文 → 中文":mode==2?L"中文 → 英文":L"自动识别方向");
    RECT area{};GetWindowRect(b.direction,&area);int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,area.left,area.bottom,0,b.window,nullptr);DestroyMenu(menu);if(chosen>=211&&chosen<=213)set_direction(b,chosen-211);
}
void clear_text(Box& b){
    // WM_SETTEXT schedules a new revision, clears the output and cancels any
    // pending/running translation, so a late reply cannot restore old text.
    SetWindowTextW(b.input,L"");b.input_wheel=b.output_wheel=0;SendMessageW(b.input,EM_SETSEL,0,0);SetFocus(b.input);
}
void output_changed(Box& b){
    if(b.internal_output)return;
    // Editing a result takes ownership of it; a late model reply must not replace it.
    ++b.revision;b.state->cancelled=true;b.pending=b.scheduled=false;
    KillTimer(b.window,translate_timer);KillTimer(b.window,idle_timer);SetTimer(b.window,idle_timer,60000,nullptr);
    if(!GetWindowTextLengthW(b.output)){SetWindowTextW(b.input,L"");b.input_wheel=b.output_wheel=0;}
    SetWindowTextW(b.button,L"复制译文");EnableWindow(b.button,GetWindowTextLengthW(b.output)>0);
}
void extend_frame(Box& b){
    // Keep GDI controls on an opaque client surface. Extending glass over the
    // toolbar makes non-alpha-aware GDI text disappear in the DWM compositor.
    MARGINS margins{1,1,1,1};DwmExtendFrameIntoClientArea(b.window,&margins);
}
LRESULT frame_hit(Box& b,LPARAM position){
    POINT p{GET_X_LPARAM(position),GET_Y_LPARAM(position)};ScreenToClient(b.window,&p);RECT area{};GetClientRect(b.window,&area);
    if(!IsZoomed(b.window)){
        int border=GetSystemMetricsForDpi(SM_CXSIZEFRAME,b.dpi)+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,b.dpi);
        bool left=p.x<border,right=p.x>=area.right-border,top=p.y<border,bottom=p.y>=area.bottom-border;
        if(top)return left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP;
        if(bottom)return left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM;
        if(left)return HTLEFT;
        if(right)return HTRIGHT;
    }
    if(p.y<b.title_height()){
        for(auto control:{b.clear,b.direction,b.button,b.caption[0],b.caption[1],b.caption[2]}){auto bounds=panel_rect(b,control);if(PtInRect(&bounds,p))return HTCLIENT;}
        return p.x<b.px(32)?HTSYSMENU:HTCAPTION;
    }
    return HTCLIENT;
}
LRESULT CALLBACK input_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    if(m==WM_PAINT){
        auto& b=*(Box*)data;prepare_surface(b);PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT r{};GetClientRect(h,&r);
        auto memory=CreateCompatibleDC(dc);auto bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(memory,bitmap);
        auto area=panel_rect(b,h);BitBlt(memory,0,0,r.right,r.bottom,b.surface,area.left,area.top,SRCCOPY);
        // Render the complete current text over fresh background pixels, rather
        // than retaining EDIT's incremental transparent glyph updates.
        DefSubclassProc(h,WM_PRINT,(WPARAM)memory,PRF_CLIENT|PRF_ERASEBKGND);
        BitBlt(dc,0,0,r.right,r.bottom,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);EndPaint(h,&paint);return 0;
    }
    if(m==WM_ERASEBKGND){auto& b=*(Box*)data;prepare_surface(b);auto area=panel_rect(b,h);BitBlt((HDC)w,0,0,area.right-area.left,area.bottom-area.top,b.surface,area.left,area.top,SRCCOPY);return 1;}
    if(m==WM_MOUSEWHEEL){
        auto& b=*(Box*)data;auto& remainder=GetDlgCtrlID(h)==201?b.input_wheel:b.output_wheel;
        remainder+=GET_WHEEL_DELTA_WPARAM(w);int steps=remainder/WHEEL_DELTA;remainder%=WHEEL_DELTA;
        UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
        if(lines==WHEEL_PAGESCROLL){RECT area{};SendMessageW(h,EM_GETRECT,0,(LPARAM)&area);auto dc=GetDC(h);auto old=SelectObject(dc,b.font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(h,dc);lines=std::max(1L,(area.bottom-area.top)/std::max(1L,metrics.tmHeight));}
        if(steps&&lines){SendMessageW(h,EM_LINESCROLL,0,-steps*(int)lines);InvalidateRect(h,nullptr,TRUE);}return 0;
    }
    if(GetDlgCtrlID(h)==201&&m==WM_KEYDOWN&&w==VK_RETURN&&(GetKeyState(VK_CONTROL)&0x8000)){translate(*(Box*)data);return 0;}
    if(m==WM_KEYDOWN&&w=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(h,EM_SETSEL,0,-1);return 0;}
    if(m==WM_CHAR&&w==10)return 0;
    auto* b=(Box*)data;auto revision=b->revision;auto result=DefSubclassProc(h,m,w,l);
    // Multiline EDIT does not notify EN_CHANGE for programmatic WM_SETTEXT.
    if(GetDlgCtrlID(h)==201&&m==WM_SETTEXT&&b->revision==revision)schedule(*b);
    if(GetDlgCtrlID(h)==202&&m==WM_SETTEXT&&b->revision==revision)output_changed(*b);
    if(m==WM_SETTEXT||m==EM_REPLACESEL||m==WM_CHAR||m==WM_CUT||m==WM_CLEAR||m==WM_PASTE||m==WM_UNDO||m==EM_UNDO||m==WM_KEYDOWN)InvalidateRect(h,nullptr,TRUE);
    if(m==WM_VSCROLL||m==WM_HSCROLL||m==WM_MOUSEWHEEL||(m==WM_KEYDOWN&&(w==VK_UP||w==VK_DOWN||w==VK_PRIOR||w==VK_NEXT||w==VK_HOME||w==VK_END)))InvalidateRect(h,nullptr,TRUE);
    return result;
}
LRESULT CALLBACK box_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto*b=(Box*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_NCCREATE){b=(Box*)((CREATESTRUCTW*)l)->lpCreateParams;b->window=h;b->state->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)b);}
    if(!b)return DefWindowProcW(h,m,w,l);
    if(m==WM_NCCALCSIZE&&w){
        // Use the caption space for one opaque toolbar row; retain the normal
        // system menu, resize and minimize/maximize window styles.
        if(IsZoomed(h)){auto* area=&((NCCALCSIZE_PARAMS*)l)->rgrc[0];int border=GetSystemMetricsForDpi(SM_CXSIZEFRAME,b->dpi)+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,b->dpi);InflateRect(area,-border,-border);}return 0;
    }
    if(m==WM_NCHITTEST)return frame_hit(*b,l);
    if(m==WM_NCACTIVATE)return DefWindowProcW(h,m,w,-1);
    if(m==WM_ACTIVATE){extend_frame(*b);b->dirty=true;RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}
    if(m==WM_CREATE){
        auto instance=(HINSTANCE)GetWindowLongPtrW(h,GWLP_HINSTANCE);b->dpi=(int)GetDpiForWindow(h);b->paper=CreateSolidBrush(RGB(248,252,254));
        b->font=CreateFontW(-b->px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        b->icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,LR_SHARED);
        b->engine=std::make_unique<NeuralTranslator>(b->root);
        DWORD style=WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL;
        b->input=CreateWindowExW(0,L"EDIT",L"",style,0,0,0,0,h,(HMENU)201,instance,nullptr);
        b->output=CreateWindowExW(0,L"EDIT",L"",style,0,0,0,0,h,(HMENU)202,instance,nullptr);
        b->button=CreateWindowW(L"BUTTON",L"复制译文",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,h,(HMENU)203,instance,nullptr);EnableWindow(b->button,FALSE);
        b->direction=CreateWindowW(L"BUTTON",L"自动识别方向 ▾",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,h,(HMENU)204,instance,nullptr);
        b->clear=CreateWindowW(L"BUTTON",L"一键清除",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,h,(HMENU)206,instance,nullptr);
        for(int i=0;i<3;++i)b->caption[i]=CreateWindowW(L"BUTTON",i==0?L"最小化":i==1?L"最大化 / 还原":L"关闭",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,h,(HMENU)(INT_PTR)(207+i),instance,nullptr);
        b->tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,h,nullptr,instance,nullptr);
        TOOLINFOW tip{};tip.cbSize=sizeof(tip);tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=h;tip.uId=(UINT_PTR)b->button;tip.lpszText=(LPWSTR)L"复制译文";SendMessageW(b->tooltip,TTM_ADDTOOLW,0,(LPARAM)&tip);SendMessageW(b->tooltip,TTM_SETMAXTIPWIDTH,0,b->px(360));set_status(*b,L"输入或粘贴文本后自动翻译");
        for(HWND control:{b->input,b->output,b->button,b->direction,b->clear})SendMessageW(control,WM_SETFONT,(WPARAM)b->font,TRUE);
        b->panels={Box::Panel{b->input},Box::Panel{b->output}};
        SendMessageW(b->input,EM_SETLIMITTEXT,8000,0);SendMessageW(b->output,EM_SETLIMITTEXT,128000,0);SetWindowSubclass(b->input,input_proc,1,(DWORD_PTR)b);SetWindowSubclass(b->output,input_proc,1,(DWORD_PTR)b);layout(*b);b->reload();extend_frame(*b);SetWindowPos(h,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);return 0;
    }
    if(m==WM_SIZE){layout(*b);bool visible=w!=SIZE_MINIMIZED&&IsWindowVisible(h);b->background.visible(visible);KillTimer(h,1);if(visible&&b->background.animated())SetTimer(h,1,67,nullptr);return 0;}
    if(m==WM_SHOWWINDOW){b->background.visible(w!=0);if(w&&b->background.animated())SetTimer(h,1,67,nullptr);else KillTimer(h,1);}
    if(m==WM_TIMER){if(w==translate_timer){if(b->scheduled)translate(*b);}else if(w==idle_timer){KillTimer(h,idle_timer);if(!b->busy&&!b->scheduled)b->engine->reset();}else if(w==background_timer){b->dirty=true;RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);}return 0;}
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PAINTSTRUCT p{};auto dc=m==WM_PAINT?BeginPaint(h,&p):(HDC)w;prepare_surface(*b);if(b->surface)BitBlt(dc,0,0,b->width,b->height,b->surface,0,0,SRCCOPY);if(m==WM_PAINT)EndPaint(h,&p);return 0;}
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_DRAWITEM){
        auto* item=(DRAWITEMSTRUCT*)l;if(item->CtlID>=207&&item->CtlID<=209){
            auto r=item->rcItem;auto brush=CreateSolidBrush((item->itemState&ODS_SELECTED)?RGB(222,234,240):RGB(248,252,254));FillRect(item->hDC,&r,brush);DeleteObject(brush);
            auto pen=CreatePen(PS_SOLID,b->px(2),RGB(50,76,91));auto old_pen=SelectObject(item->hDC,pen),old_brush=SelectObject(item->hDC,GetStockObject(HOLLOW_BRUSH));
            int x=r.right/2,y=r.bottom/2,s=b->px(6);
            if(item->CtlID==207){MoveToEx(item->hDC,x-s,y,nullptr);LineTo(item->hDC,x+s+1,y);}
            else if(item->CtlID==208){if(IsZoomed(h)){Rectangle(item->hDC,x-s+2,y-s-2,x+s+2,y+s-2);Rectangle(item->hDC,x-s,y-s,x+s,y+s);}else Rectangle(item->hDC,x-s,y-s,x+s,y+s);}
            else{MoveToEx(item->hDC,x-s,y-s,nullptr);LineTo(item->hDC,x+s+1,y+s+1);MoveToEx(item->hDC,x+s,y-s,nullptr);LineTo(item->hDC,x-s-1,y+s+1);}
            SelectObject(item->hDC,old_brush);SelectObject(item->hDC,old_pen);DeleteObject(pen);
            if((item->itemState&ODS_FOCUS)&&!(item->itemState&ODS_NOFOCUSRECT)){InflateRect(&r,-b->px(4),-b->px(4));DrawFocusRect(item->hDC,&r);}return TRUE;
        }
        if(item->CtlID!=203&&item->CtlID!=204&&item->CtlID!=206)return FALSE;
        prepare_surface(*b);
        auto area=panel_rect(*b,item->hwndItem);auto bounds=item->rcItem;BitBlt(item->hDC,0,0,bounds.right,bounds.bottom,b->surface,area.left,area.top,SRCCOPY);
        paint_glass(item->hDC,bounds,(item->itemState&ODS_SELECTED)?32:18,b->px(6));
        auto pen=CreatePen(PS_SOLID,1,RGB(211,224,230));auto old_pen=SelectObject(item->hDC,pen),old_brush=SelectObject(item->hDC,GetStockObject(HOLLOW_BRUSH));RoundRect(item->hDC,0,0,bounds.right,bounds.bottom,b->px(6),b->px(6));SelectObject(item->hDC,old_brush);SelectObject(item->hDC,old_pen);DeleteObject(pen);
        auto old_font=SelectObject(item->hDC,b->font);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,(item->itemState&ODS_DISABLED)?RGB(135,149,158):RGB(50,76,91));wchar_t text[128]{};GetWindowTextW(item->hwndItem,text,128);DrawTextW(item->hDC,text,-1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if((item->itemState&ODS_FOCUS)&&!(item->itemState&ODS_NOFOCUSRECT)){InflateRect(&bounds,-b->px(4),-b->px(4));DrawFocusRect(item->hDC,&bounds);}SelectObject(item->hDC,old_font);return TRUE;
    }
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT){prepare_surface(*b);auto dc=(HDC)w;SetTextColor(dc,RGB(65,94,110));SetBkMode(dc,TRANSPARENT);POINT origin{};LPtoDP(dc,&origin,1);SetBrushOrgEx(dc,origin.x,origin.y,nullptr);for(auto& p:b->panels)if(p.control==(HWND)l&&p.brush)return (LRESULT)p.brush;return (LRESULT)b->paper;}
    if(m==WM_DPICHANGED){b->dpi=HIWORD(w);auto font=CreateFontW(-b->px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");for(HWND control:{b->input,b->output,b->button,b->direction,b->clear})SendMessageW(control,WM_SETFONT,(WPARAM)font,TRUE);DeleteObject(b->font);b->font=font;auto*r=(RECT*)l;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);extend_frame(*b);layout(*b);return 0;}
    if(m==WM_GETMINMAXINFO){((MINMAXINFO*)l)->ptMinTrackSize={b->px(600),b->px(400)};return 0;}
    if(m==WM_COMMAND){auto id=LOWORD(w);if(id==201&&HIWORD(w)==EN_CHANGE){schedule(*b);InvalidateRect(b->input,nullptr,TRUE);}else if(id==202&&HIWORD(w)==EN_CHANGE){output_changed(*b);InvalidateRect(b->output,nullptr,TRUE);}else if(id==204&&HIWORD(w)==BN_CLICKED)direction_menu(*b);else if(id==203&&HIWORD(w)==BN_CLICKED)copy_result(*b);else if(id==206&&HIWORD(w)==BN_CLICKED)clear_text(*b);else if(id==207&&HIWORD(w)==BN_CLICKED)ShowWindow(h,SW_MINIMIZE);else if(id==208&&HIWORD(w)==BN_CLICKED)ShowWindow(h,IsZoomed(h)?SW_RESTORE:SW_MAXIMIZE);else if(id==209&&HIWORD(w)==BN_CLICKED)PostMessageW(h,WM_CLOSE,0,0);else if(id>=211&&id<=213)set_direction(*b,id-211);return 0;}
    if(m==translated_message){
        if(!b->busy||w!=b->ticket)return 0;
        if(b->worker.joinable())b->worker.join();
        b->busy=false;
        {std::lock_guard<std::mutex>lock(b->state->mutex);if(b->state->revision==b->revision){set_output(*b,b->state->text.empty()?b->state->status.c_str():b->state->text.c_str());set_status(*b,b->state->status);SetWindowTextW(b->button,L"复制译文");EnableWindow(b->button,!b->state->text.empty());}}
        if(b->pending)translate(*b);else SetTimer(h,idle_timer,60000,nullptr);return 0;
    }
    if(m==WM_SETFOCUS){SetFocus(b->input);return 0;}
    if(m==WM_CLOSE){DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){for(auto timer:{background_timer,translate_timer,idle_timer})KillTimer(h,timer);b->background.visible(false);b->state->cancelled=true;{std::lock_guard<std::mutex>lock(b->state->mutex);b->state->window=nullptr;}if(b->worker.joinable())b->worker.join();translation_window=nullptr;return 0;}
    if(m==WM_NCDESTROY){DeleteObject(b->font);DeleteObject(b->paper);SetWindowLongPtrW(h,GWLP_USERDATA,0);delete b;}
    return DefWindowProcW(h,m,w,l);
}
}
HWND show_translation_box(HINSTANCE instance,HWND owner,DictionaryProvider provider,const std::wstring& root,bool visible,const std::wstring& appearance_root){
    if(translation_window&&IsWindow(translation_window)){ShowWindow(translation_window,SW_RESTORE);SetForegroundWindow(translation_window);return translation_window;}
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=box_proc;cls.lpszClassName=L"PinyinShift.Translation";cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,LR_SHARED);RegisterClassW(&cls);
    auto*b=new Box;b->provider=std::move(provider);b->root=root;b->appearance_root=appearance_root.empty()?root:appearance_root;int dpi=(int)GetDpiForSystem();
    translation_window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"翻译框",(WS_OVERLAPPEDWINDOW&~WS_CAPTION)|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(840,dpi,96),MulDiv(600,dpi,96),owner,nullptr,instance,b);
    if(translation_window&&visible){ShowWindow(translation_window,SW_SHOWNORMAL);SetForegroundWindow(translation_window);SetFocus(b->input);}return translation_window;
}
void close_translation_box(){if(translation_window&&IsWindow(translation_window))DestroyWindow(translation_window);}
void reload_translation_background(bool media){if(translation_window){auto*b=(Box*)GetWindowLongPtrW(translation_window,GWLP_USERDATA);if(b)b->reload(media);}}
}
