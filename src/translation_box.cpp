#include "translation_box.hpp"
#include <commctrl.h>
#include <mutex>
#include <thread>
namespace ea {
namespace {
constexpr UINT translated_message=WM_APP+40;
HWND translation_window=nullptr;
struct State {std::mutex mutex;HWND window=nullptr;OfflineReply reply;};
struct Box {
    HWND window=nullptr,input=nullptr,output=nullptr,header=nullptr,status=nullptr,button=nullptr;
    HFONT font=nullptr;int dpi=96;DictionaryProvider provider;
    std::shared_ptr<State> state=std::make_shared<State>();std::thread worker;
    int px(int value)const{return MulDiv(value,dpi,96);}
};
void layout(Box& b){
    RECT r{};GetClientRect(b.window,&r);int margin=b.px(16),half=(r.bottom-b.px(120))/2;
    MoveWindow(b.header,margin,b.px(10),r.right-2*margin,b.px(26),TRUE);
    MoveWindow(b.input,margin,b.px(42),r.right-2*margin,half,TRUE);
    MoveWindow(b.button,r.right-margin-b.px(120),b.px(48)+half,b.px(120),b.px(30),TRUE);
    MoveWindow(b.output,margin,b.px(86)+half,r.right-2*margin,half,TRUE);
    MoveWindow(b.status,margin,r.bottom-b.px(28),r.right-2*margin,b.px(24),TRUE);
}
void translate(Box& b){
    if(!IsWindowEnabled(b.button))return;
    int length=GetWindowTextLengthW(b.input);if(!length){SetWindowTextW(b.status,L"请先粘贴英文。");return;}
    std::wstring text(length+1,L'\0');GetWindowTextW(b.input,text.data(),length+1);text.resize(length);
    if(b.worker.joinable())b.worker.join();
    auto dictionary=b.provider();EnableWindow(b.button,FALSE);EnableWindow(b.input,FALSE);SetWindowTextW(b.status,L"正在查询本地词库…");SetWindowTextW(b.output,L"");
    b.worker=std::thread([state=b.state,dictionary,text=std::move(text)]{
        auto reply=dictionary->to_chinese(text);
        std::lock_guard<std::mutex>lock(state->mutex);state->reply=std::move(reply);if(state->window)PostMessageW(state->window,translated_message,0,0);
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
        auto instance=(HINSTANCE)GetWindowLongPtrW(h,GWLP_HINSTANCE);b->dpi=(int)GetDpiForWindow(h);
        b->font=CreateFontW(-b->px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        b->header=CreateWindowW(L"STATIC",L"英文 → 中文 · 粘贴英文后点击翻译（Ctrl + Enter）",WS_CHILD|WS_VISIBLE,0,0,0,0,h,nullptr,instance,nullptr);
        DWORD style=WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL;
        b->input=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",style,0,0,0,0,h,(HMENU)201,instance,nullptr);
        b->output=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",style|ES_READONLY,0,0,0,0,h,(HMENU)202,instance,nullptr);
        b->button=CreateWindowW(L"BUTTON",L"翻译为中文",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,h,(HMENU)203,instance,nullptr);
        b->status=CreateWindowW(L"STATIC",L"完全离线 · 优先整句匹配，未收录长句显示词组参考",WS_CHILD|WS_VISIBLE,0,0,0,0,h,nullptr,instance,nullptr);
        for(HWND control:{b->header,b->input,b->output,b->button,b->status})SendMessageW(control,WM_SETFONT,(WPARAM)b->font,TRUE);
        SendMessageW(b->input,EM_SETLIMITTEXT,8000,0);SendMessageW(b->output,EM_SETLIMITTEXT,128000,0);SetWindowSubclass(b->input,input_proc,1,(DWORD_PTR)b);layout(*b);return 0;
    }
    if(m==WM_SIZE){layout(*b);return 0;}
    if(m==WM_GETMINMAXINFO){((MINMAXINFO*)l)->ptMinTrackSize={b->px(520),b->px(400)};return 0;}
    if(m==WM_COMMAND&&LOWORD(w)==203&&HIWORD(w)==BN_CLICKED){translate(*b);return 0;}
    if(m==translated_message){
        OfflineReply reply;{std::lock_guard<std::mutex>lock(b->state->mutex);reply=std::move(b->state->reply);}
        SetWindowTextW(b->output,reply.text.c_str());EnableWindow(b->button,TRUE);EnableWindow(b->input,TRUE);
        auto status=reply.exact?L"整句／词条匹配 · 可选中下方中文后 Ctrl + C 复制":L"词组参考（不是通顺的整句译文） · 未收录 "+std::to_wstring(reply.unknown.size())+L" 项";
        SetWindowTextW(b->status,status.c_str());return 0;
    }
    if(m==WM_SETFOCUS){SetFocus(b->input);return 0;}
    if(m==WM_CLOSE){DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){{std::lock_guard<std::mutex>lock(b->state->mutex);b->state->window=nullptr;}if(b->worker.joinable())b->worker.join();translation_window=nullptr;return 0;}
    if(m==WM_NCDESTROY){DeleteObject(b->font);SetWindowLongPtrW(h,GWLP_USERDATA,0);delete b;}
    return DefWindowProcW(h,m,w,l);
}
}
HWND show_translation_box(HINSTANCE instance,HWND owner,DictionaryProvider provider){
    if(translation_window&&IsWindow(translation_window)){ShowWindow(translation_window,SW_RESTORE);SetForegroundWindow(translation_window);return translation_window;}
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=box_proc;cls.lpszClassName=L"EnglishAssistant.Translation";cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,LR_SHARED);RegisterClassW(&cls);
    auto*b=new Box;b->provider=std::move(provider);int dpi=(int)GetDpiForSystem();
    translation_window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"EnglishAssistant — 离线翻译框",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(760,dpi,96),MulDiv(600,dpi,96),owner,nullptr,instance,b);
    if(translation_window){ShowWindow(translation_window,SW_SHOWNORMAL);SetForegroundWindow(translation_window);SetFocus(b->input);}return translation_window;
}
void close_translation_box(){if(translation_window&&IsWindow(translation_window))DestroyWindow(translation_window);}
}
