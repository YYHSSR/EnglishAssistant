#include "translation_box.hpp"
#include "neural.hpp"
#include "background.hpp"
#include <commctrl.h>
#include <mutex>
#include <thread>
namespace ea {
namespace {
constexpr UINT translated_message=WM_APP+40;
HWND translation_window=nullptr;
struct State {std::mutex mutex;HWND window=nullptr;std::wstring text,status;std::atomic<bool> cancelled{false};};
struct Box {
    HWND window=nullptr,input=nullptr,output=nullptr,header=nullptr,status=nullptr,button=nullptr;
    HFONT font=nullptr;int dpi=96;DictionaryProvider provider;std::wstring root;Background background;HBRUSH paper=nullptr;
    std::shared_ptr<State> state=std::make_shared<State>();std::thread worker;
    int px(int value)const{return MulDiv(value,dpi,96);}
    void reload(){background.load(background_path(root,BackgroundKind::Translation),background_sound(root,BackgroundKind::Translation));background.visible(IsWindowVisible(window));KillTimer(window,1);if(IsWindowVisible(window)&&background.animated())SetTimer(window,1,67,nullptr);InvalidateRect(window,nullptr,TRUE);}
};
void layout(Box& b){
    RECT r{};GetClientRect(b.window,&r);int margin=b.px(16),half=(r.bottom-b.px(120))/2;int content=(r.right-2*margin)*76/100;
    MoveWindow(b.header,margin,b.px(10),r.right-2*margin,b.px(26),TRUE);
    MoveWindow(b.input,margin,b.px(42),content,half,TRUE);
    MoveWindow(b.button,margin+content-b.px(120),b.px(48)+half,b.px(120),b.px(30),TRUE);
    MoveWindow(b.output,margin,b.px(86)+half,content,half,TRUE);
    MoveWindow(b.status,margin,r.bottom-b.px(28),r.right-2*margin,b.px(24),TRUE);
}
void translate(Box& b){
    if(!IsWindowEnabled(b.button))return;
    int length=GetWindowTextLengthW(b.input);if(!length){SetWindowTextW(b.status,L"请先粘贴英文。");return;}
    std::wstring text(length+1,L'\0');GetWindowTextW(b.input,text.data(),length+1);text.resize(length);
    if(b.worker.joinable())b.worker.join();
    b.state->cancelled=false;auto dictionary=b.provider();EnableWindow(b.button,FALSE);EnableWindow(b.input,FALSE);SetWindowTextW(b.status,L"正在运行本地翻译模型…首次使用会解压运行库。");SetWindowTextW(b.output,L"");
    b.worker=std::thread([state=b.state,dictionary,root=b.root,text=std::move(text)]{
        std::wstring result,status;
        try{auto exact=dictionary->to_chinese(text);if(exact.exact){result=std::move(exact.text);status=L"已匹配本地词条 · 选中译文后 Ctrl + C 复制";}
        else{auto reply=neural_translate(root,text,state->cancelled);result=std::move(reply.text);status=reply.error.empty()?L"本地模型译文 · 请核对专名及专业术语":L"翻译失败："+reply.error;}}
        catch(const std::exception&){status=L"翻译失败，请检查本地模型文件。";}
        std::lock_guard<std::mutex>lock(state->mutex);state->text=std::move(result);state->status=std::move(status);if(state->window)PostMessageW(state->window,translated_message,0,0);
    });
}
LRESULT CALLBACK input_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    if(m==WM_KEYDOWN&&w==VK_RETURN&&(GetKeyState(VK_CONTROL)&0x8000)){translate(*(Box*)data);return 0;}
    if(m==WM_CHAR&&w==10)return 0;
    return DefSubclassProc(h,m,w,l);
}
LRESULT CALLBACK box_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto*b=(Box*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_NCCREATE){b=(Box*)((CREATESTRUCTW*)l)->lpCreateParams;b->window=h;b->state->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)b);}
    if(!b)return DefWindowProcW(h,m,w,l);
    if(m==WM_CREATE){
        auto instance=(HINSTANCE)GetWindowLongPtrW(h,GWLP_HINSTANCE);b->dpi=(int)GetDpiForWindow(h);b->paper=CreateSolidBrush(RGB(248,252,254));
        b->font=CreateFontW(-b->px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        b->header=CreateWindowW(L"STATIC",L"英文 → 中文 · 粘贴英文后点击翻译（Ctrl + Enter）",WS_CHILD|WS_VISIBLE,0,0,0,0,h,nullptr,instance,nullptr);
        DWORD style=WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL;
        b->input=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",style,0,0,0,0,h,(HMENU)201,instance,nullptr);
        b->output=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",style|ES_READONLY,0,0,0,0,h,(HMENU)202,instance,nullptr);
        b->button=CreateWindowW(L"BUTTON",L"翻译为中文",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,h,(HMENU)203,instance,nullptr);
        b->status=CreateWindowW(L"STATIC",L"完全离线 · 支持完整英文句子和段落",WS_CHILD|WS_VISIBLE,0,0,0,0,h,nullptr,instance,nullptr);
        for(HWND control:{b->header,b->input,b->output,b->button,b->status})SendMessageW(control,WM_SETFONT,(WPARAM)b->font,TRUE);
        SendMessageW(b->input,EM_SETLIMITTEXT,8000,0);SendMessageW(b->output,EM_SETLIMITTEXT,128000,0);SetWindowSubclass(b->input,input_proc,1,(DWORD_PTR)b);layout(*b);b->reload();return 0;
    }
    if(m==WM_SIZE){layout(*b);bool visible=w!=SIZE_MINIMIZED&&IsWindowVisible(h);b->background.visible(visible);KillTimer(h,1);if(visible&&b->background.animated())SetTimer(h,1,67,nullptr);return 0;}
    if(m==WM_SHOWWINDOW){b->background.visible(w!=0);if(w&&b->background.animated())SetTimer(h,1,67,nullptr);else KillTimer(h,1);}
    if(m==WM_TIMER){InvalidateRect(h,nullptr,FALSE);return 0;}
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PAINTSTRUCT p{};auto dc=m==WM_PAINT?BeginPaint(h,&p):(HDC)w;RECT r{};GetClientRect(h,&r);FillRect(dc,&r,b->paper);b->background.paint(dc,r);if(m==WM_PAINT)EndPaint(h,&p);return 0;}
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT){SetTextColor((HDC)w,RGB(38,68,84));if((HWND)l==b->input||(HWND)l==b->output){SetBkColor((HDC)w,RGB(248,252,254));return (LRESULT)b->paper;}SetBkMode((HDC)w,TRANSPARENT);return (LRESULT)GetStockObject(NULL_BRUSH);}
    if(m==WM_DPICHANGED){b->dpi=HIWORD(w);auto font=CreateFontW(-b->px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");for(HWND control:{b->header,b->input,b->output,b->button,b->status})SendMessageW(control,WM_SETFONT,(WPARAM)font,TRUE);DeleteObject(b->font);b->font=font;auto*r=(RECT*)l;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout(*b);return 0;}
    if(m==WM_GETMINMAXINFO){((MINMAXINFO*)l)->ptMinTrackSize={b->px(520),b->px(400)};return 0;}
    if(m==WM_COMMAND&&LOWORD(w)==203&&HIWORD(w)==BN_CLICKED){translate(*b);return 0;}
    if(m==translated_message){
        std::lock_guard<std::mutex>lock(b->state->mutex);SetWindowTextW(b->output,b->state->text.c_str());SetWindowTextW(b->status,b->state->status.c_str());EnableWindow(b->button,TRUE);EnableWindow(b->input,TRUE);return 0;
    }
    if(m==WM_SETFOCUS){SetFocus(b->input);return 0;}
    if(m==WM_CLOSE){DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){KillTimer(h,1);b->background.visible(false);b->state->cancelled=true;{std::lock_guard<std::mutex>lock(b->state->mutex);b->state->window=nullptr;}if(b->worker.joinable())b->worker.join();translation_window=nullptr;return 0;}
    if(m==WM_NCDESTROY){DeleteObject(b->font);DeleteObject(b->paper);SetWindowLongPtrW(h,GWLP_USERDATA,0);delete b;}
    return DefWindowProcW(h,m,w,l);
}
}
HWND show_translation_box(HINSTANCE instance,HWND owner,DictionaryProvider provider,const std::wstring& root,bool visible){
    if(translation_window&&IsWindow(translation_window)){ShowWindow(translation_window,SW_RESTORE);SetForegroundWindow(translation_window);return translation_window;}
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=box_proc;cls.lpszClassName=L"EnglishAssistant.Translation";cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,LR_SHARED);RegisterClassW(&cls);
    auto*b=new Box;b->provider=std::move(provider);b->root=root;int dpi=(int)GetDpiForSystem();
    translation_window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"EnglishAssistant — 离线翻译框",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(840,dpi,96),MulDiv(600,dpi,96),owner,nullptr,instance,b);
    if(translation_window&&visible){ShowWindow(translation_window,SW_SHOWNORMAL);SetForegroundWindow(translation_window);SetFocus(b->input);}return translation_window;
}
void close_translation_box(){if(translation_window&&IsWindow(translation_window))DestroyWindow(translation_window);}
void reload_translation_background(){if(translation_window){auto*b=(Box*)GetWindowLongPtrW(translation_window,GWLP_USERDATA);if(b)b->reload();}}
}
