#include "documents.hpp"
#include "dictionary.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <commctrl.h>

namespace ea {
bool read_document(const std::wstring& path,std::wstring& text){
    std::ifstream file{std::filesystem::path(path),std::ios::binary|std::ios::ate};
    if(!file)return false;
    auto length=file.tellg();if(length<0||length>1024*1024)return false;
    std::string bytes((size_t)length,'\0');file.seekg(0);file.read(bytes.data(),(std::streamsize)bytes.size());
    if(!file)return false;
    if(bytes.size()>=3&&bytes.substr(0,3)=="\xef\xbb\xbf")bytes.erase(0,3);
    auto decoded=wide(bytes);if(!bytes.empty()&&decoded.empty())return false;
    text.clear();for(size_t i=0;i<decoded.size();i++){if(decoded[i]==L'\n'&&(i==0||decoded[i-1]!=L'\r'))text+=L'\r';text+=decoded[i];}
    return true;
}
bool save_document(const std::wstring& path,const std::wstring& text){
    std::wstring normalized;for(size_t i=0;i<text.size();i++){if(text[i]==L'\r'&&i+1<text.size()&&text[i+1]==L'\n')continue;normalized+=text[i];}
    auto bytes=utf8(normalized);if(!normalized.empty()&&bytes.empty())return false;
    // Do not touch the original unless the replacement has been written completely.
    auto temporary=path+L".saving";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool complete=WriteFile(file,bytes.data(),(DWORD)bytes.size(),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(complete)complete=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!complete)DeleteFileW(temporary.c_str());
    return complete;
}
namespace {
constexpr wchar_t document_class[]=L"EnglishAssistant.Document";
struct Document {
    HWND window=nullptr,edit=nullptr,header=nullptr,save=nullptr,status=nullptr;
    HFONT font=nullptr,small=nullptr;
    std::wstring path,title,text;bool editable=false,loading=true,dirty=false;
    int dpi=96;std::function<void()> saved;
    int px(int value)const{return MulDiv(value,dpi,96);}
};
std::unordered_map<std::wstring,HWND> windows;
void layout(Document& d){
    RECT r{};GetClientRect(d.window,&r);int margin=d.px(16),footer=d.px(50);
    MoveWindow(d.header,margin,d.px(8),r.right-margin*2,d.px(36),TRUE);
    MoveWindow(d.edit,margin,d.px(48),r.right-margin*2,r.bottom-d.px(48)-footer,TRUE);
    MoveWindow(d.status,margin,r.bottom-d.px(38),r.right-margin*2-d.px(d.editable?136:0),d.px(24),TRUE);
    if(d.save)MoveWindow(d.save,r.right-margin-d.px(120),r.bottom-d.px(40),d.px(120),d.px(30),TRUE);
}
void save(Document& d){
    int n=GetWindowTextLengthW(d.edit);std::wstring text((size_t)n+1,L'\0');GetWindowTextW(d.edit,text.data(),n+1);text.resize(n);
    if(!save_document(d.path,text)){MessageBoxW(d.window,L"无法保存词表。请检查目录是否可写，或是否已有未完成的 .saving 文件。原文件未被替换。",L"EnglishAssistant",MB_OK|MB_ICONERROR);return;}
    d.dirty=false;SetWindowTextW(d.window,d.title.c_str());SetWindowTextW(d.status,L"已保存并重新加载");if(d.saved)d.saved();
}
LRESULT CALLBACK edit_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto&d=*(Document*)data;
    if(d.editable&&m==WM_KEYDOWN&&w=='S'&&(GetKeyState(VK_CONTROL)&0x8000)){save(d);return 0;}
    if(d.editable&&m==WM_CHAR&&w==19)return 0;
    return DefSubclassProc(h,m,w,l);
}
LRESULT CALLBACK document_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto*d=(Document*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_NCCREATE){d=(Document*)((CREATESTRUCTW*)l)->lpCreateParams;d->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)d);}
    if(!d)return DefWindowProcW(h,m,w,l);
    if(m==WM_CREATE){
        d->dpi=(int)GetDpiForWindow(h);
        auto instance=(HINSTANCE)GetWindowLongPtrW(h,GWLP_HINSTANCE);
        d->font=CreateFontW(-d->px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,d->editable?L"Consolas":L"Microsoft YaHei UI");
        d->small=CreateFontW(-d->px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        d->header=CreateWindowW(L"STATIC",d->editable?L"中文与英文之间按 Tab 分隔；每行一个词或短句。":L"EnglishAssistant · 微软拼音英文候选助手",WS_CHILD|WS_VISIBLE,0,0,0,0,h,nullptr,instance,nullptr);
        DWORD edit_style=WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL;
        if(d->editable)edit_style|=ES_AUTOHSCROLL|WS_HSCROLL;else edit_style|=ES_READONLY;
        d->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",edit_style,0,0,0,0,h,(HMENU)101,instance,nullptr);
        d->status=CreateWindowW(L"STATIC",d->editable?L"保存：Ctrl + S":L"只读说明 · 可选择和复制文字",WS_CHILD|WS_VISIBLE,0,0,0,0,h,nullptr,instance,nullptr);
        if(d->editable)d->save=CreateWindowW(L"BUTTON",L"保存并应用",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,h,(HMENU)102,instance,nullptr);
        SendMessageW(d->edit,EM_SETLIMITTEXT,1024*1024,0);int tabs=32;SendMessageW(d->edit,EM_SETTABSTOPS,1,(LPARAM)&tabs);
        SendMessageW(d->edit,WM_SETFONT,(WPARAM)d->font,TRUE);
        for(HWND control:{d->header,d->status,d->save})if(control)SendMessageW(control,WM_SETFONT,(WPARAM)d->small,TRUE);
        SetWindowTextW(d->edit,d->text.c_str());d->loading=false;
        SetWindowSubclass(d->edit,edit_proc,1,(DWORD_PTR)d);layout(*d);return 0;
    }
    if(m==WM_SIZE){layout(*d);return 0;}
    if(m==WM_GETMINMAXINFO){auto*limits=(MINMAXINFO*)l;limits->ptMinTrackSize={d->px(480),d->px(320)};return 0;}
    if(m==WM_COMMAND){
        if(LOWORD(w)==102&&HIWORD(w)==BN_CLICKED)save(*d);
        if(LOWORD(w)==101&&HIWORD(w)==EN_CHANGE&&!d->loading&&d->editable){d->dirty=true;SetWindowTextW(h,(d->title+L" *").c_str());SetWindowTextW(d->status,L"有未保存的修改 · Ctrl + S 保存");}
        return 0;
    }
    if(m==WM_SETFOCUS){SetFocus(d->edit);return 0;}
    if(m==WM_CLOSE){
        if(d->dirty){int response=MessageBoxW(h,L"保存个人词表的修改吗？",L"EnglishAssistant",MB_YESNOCANCEL|MB_ICONQUESTION);if(response==IDCANCEL)return 0;if(response==IDYES){save(*d);if(d->dirty)return 0;}}
        DestroyWindow(h);return 0;
    }
    if(m==WM_NCDESTROY){windows.erase(d->path);DeleteObject(d->font);DeleteObject(d->small);SetWindowLongPtrW(h,GWLP_USERDATA,0);delete d;}
    return DefWindowProcW(h,m,w,l);
}
}
HWND show_document(HINSTANCE instance,HWND owner,const std::wstring& path,const std::wstring& title,bool editable,std::function<void()> saved){
    auto existing=windows.find(path);if(existing!=windows.end()&&IsWindow(existing->second)){ShowWindow(existing->second,SW_RESTORE);SetForegroundWindow(existing->second);return existing->second;}
    auto d=std::make_unique<Document>();d->path=path;d->title=title;d->editable=editable;d->saved=std::move(saved);
    if(!read_document(path,d->text)){MessageBoxW(owner,L"无法读取说明或词表文件。请保留完整项目目录，并使用 UTF-8 文本。",L"EnglishAssistant",MB_OK|MB_ICONERROR);return nullptr;}
    WNDCLASSW cls{};cls.lpfnWndProc=document_proc;cls.hInstance=instance;cls.lpszClassName=document_class;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);cls.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,LR_SHARED);RegisterClassW(&cls);
    auto*raw=d.release();int dpi=(int)GetDpiForSystem();
    HWND h=CreateWindowExW(WS_EX_APPWINDOW,document_class,title.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(820,dpi,96),MulDiv(600,dpi,96),owner,nullptr,instance,raw);
    if(!h)return nullptr;
    windows[path]=h;ShowWindow(h,SW_SHOWNORMAL);SetForegroundWindow(h);SetFocus(raw->edit);return h;
}
}
