#include "candidates.hpp"
#include <algorithm>
#include <cwctype>

namespace ea {
static HWND current_focus(HWND foreground){
    GUITHREADINFO gui{};gui.cbSize=sizeof(gui);
    return GetGUIThreadInfo(GetWindowThreadProcessId(foreground,nullptr),&gui)?gui.hwndFocus:nullptr;
}
bool same_target(const Snapshot& s){return s.foreground && GetForegroundWindow()==s.foreground && IsWindow(s.foreground) && current_focus(s.foreground)==s.focus;}
bool same_candidates(const Snapshot& a,const Snapshot& b){
    if(!a.valid()||!b.valid()||a.foreground!=b.foreground||a.focus!=b.focus||a.focus_runtime!=b.focus_runtime||a.host!=b.host||a.candidates.size()!=b.candidates.size())return false;
    for(size_t i=0;i<a.candidates.size();++i)if(a.candidates[i].number!=b.candidates[i].number||a.candidates[i].word!=b.candidates[i].word)return false;
    return true;
}
CandidateReader::CandidateReader(){
    HRESULT hr=CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_IUIAutomation,(void**)uia_.put());
    if(FAILED(hr))hr=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_IUIAutomation,(void**)uia_.put());
    if(FAILED(hr))return;
    Com<IUIAutomation2> uia2;
    if(SUCCEEDED(uia_->QueryInterface(IID_IUIAutomation2,(void**)uia2.put()))){uia2->put_ConnectionTimeout(750);uia2->put_TransactionTimeout(500);}
    VARIANT v{};v.vt=VT_BSTR;v.bstrVal=SysAllocString(L"IME_Candidate_Window");
    hr=uia_->CreatePropertyCondition(UIA_AutomationIdPropertyId,v,menu_condition_.put());VariantClear(&v);if(FAILED(hr))return;
    v.vt=VT_BSTR;v.bstrVal=SysAllocString(L"TEMPLATE_PART_CandidateItemIndex");
    hr=uia_->CreatePropertyCondition(UIA_AutomationIdPropertyId,v,index_condition_.put());VariantClear(&v);if(FAILED(hr))return;
    v.vt=VT_I4;v.lVal=UIA_ListItemControlTypeId;
    if(FAILED(uia_->CreatePropertyCondition(UIA_ControlTypePropertyId,v,item_condition_.put())))return;
    if(FAILED(uia_->CreateCacheRequest(item_cache_.put())))return;
    item_cache_->AddProperty(UIA_NamePropertyId);item_cache_->AddProperty(UIA_BoundingRectanglePropertyId);
    item_cache_->AddProperty(UIA_IsOffscreenPropertyId);item_cache_->AddProperty(UIA_SelectionItemIsSelectedPropertyId);
    init_=true;
}
bool CandidateReader::trusted_host(HWND h){
    if(!IsWindow(h)||!IsWindowVisible(h))return false;
    RECT r{};if(!GetWindowRect(h,&r)||r.right<=r.left||r.bottom<=r.top)return false;
    DWORD pid=0;GetWindowThreadProcessId(h,&pid);
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!p)return false;
    wchar_t path[MAX_PATH]{};DWORD n=MAX_PATH;bool ok=QueryFullProcessImageNameW(p,0,path,&n);CloseHandle(p);
    if(!ok)return false;
    std::wstring s=path;auto slash=s.find_last_of(L"\\/");auto name=s.substr(slash==std::wstring::npos?0:slash+1);
    return _wcsicmp(name.c_str(),L"TextInputHost.exe")==0||_wcsicmp(name.c_str(),L"InputApp.exe")==0||_wcsicmp(name.c_str(),L"ChsIME.exe")==0;
}
bool CandidateReader::read_host(HWND h,Snapshot& out){
    if(!trusted_host(h))return false;
    Com<IUIAutomationElement> root,menu;
    if(FAILED(uia_->ElementFromHandle(h,root.put()))||!root)return false;
    if(FAILED(root->FindFirst(TreeScope_Descendants,menu_condition_.get(),menu.put()))||!menu)return false;
    BOOL off=TRUE;
    if(FAILED(menu->get_CurrentIsOffscreen(&off))||off)return false;
    RECT bounds{};if(FAILED(menu->get_CurrentBoundingRectangle(&bounds))||bounds.right<=bounds.left||bounds.bottom<=bounds.top)return false;
    Com<IUIAutomationElementArray> items;
    if(FAILED(menu->FindAllBuildCache(TreeScope_Descendants,item_condition_.get(),item_cache_.get(),items.put()))||!items)return false;
    int count=0;items->get_Length(&count);if(count>100)return false;
    struct Item { Candidate c; RECT r; };std::vector<Item> found;
    for(int i=0;i<count;i++){
        Com<IUIAutomationElement> e;items->GetElement(i,e.put());if(!e)continue;
        BOOL offscreen=TRUE;RECT rect{};BSTR name=nullptr;
        if(FAILED(e->get_CachedIsOffscreen(&offscreen))||offscreen)continue;
        if(FAILED(e->get_CachedBoundingRectangle(&rect))||rect.right<=rect.left||rect.bottom<=rect.top)continue;
        if(FAILED(e->get_CachedName(&name))||!name)continue;
        std::wstring word=name;SysFreeString(name);if(word.empty()||word.size()>100)continue;
        bool selected=false;VARIANT value{};
        if(SUCCEEDED(e->GetCachedPropertyValue(UIA_SelectionItemIsSelectedPropertyId,&value)) && value.vt==VT_BOOL)selected=value.boolVal!=VARIANT_FALSE;
        VariantClear(&value);
        int number=0;Com<IUIAutomationElement> index;
        e->FindFirst(TreeScope_Descendants,index_condition_.get(),index.put());
        if(index){BSTR label=nullptr;index->get_CurrentName(&label);if(label){number=_wtoi(label);SysFreeString(label);}}
        found.push_back({Candidate{number,std::move(word),selected,{}},rect});
    }
    // Geometry provides a fallback for IME builds with no exposed number label.
    std::sort(found.begin(),found.end(),[](const Item&a,const Item&b){if(abs(a.r.top-b.r.top)>8)return a.r.top<b.r.top;return a.r.left<b.r.left;});
    for(size_t i=0;i<found.size() && i<9;i++){if(found[i].c.number<1||found[i].c.number>9)found[i].c.number=(int)i+1;out.candidates.push_back(std::move(found[i].c));}
    if(out.candidates.empty())return false;
    out.bounds=bounds;out.host=h;cached_host_=h;return true;
}
bool CandidateReader::password_focus(){
    if(!uia_)return true;
    Com<IUIAutomationElement> focus;if(FAILED(uia_->GetFocusedElement(focus.put()))||!focus)return true;
    BOOL password=TRUE;if(FAILED(focus->get_CurrentIsPassword(&password)))return true;return password!=FALSE;
}
bool CandidateReader::candidate_gone(HWND host){
    if(!IsWindow(host)||!IsWindowVisible(host))return true;
    Com<IUIAutomationElement> root,menu;
    if(FAILED(uia_->ElementFromHandle(host,root.put()))||!root)return false;
    HRESULT hr=root->FindFirst(TreeScope_Descendants,menu_condition_.get(),menu.put());
    if(FAILED(hr))return false; // A timeout is never treated as cancellation.
    if(!menu)return true;
    BOOL offscreen=FALSE;
    return SUCCEEDED(menu->get_CurrentIsOffscreen(&offscreen)) && offscreen;
}
std::vector<int> CandidateReader::focus_token(){
    Com<IUIAutomationElement> focus;
    if(FAILED(uia_->GetFocusedElement(focus.put()))||!focus)return {};
    BOOL password=TRUE;
    if(FAILED(focus->get_CurrentIsPassword(&password))||password)return {};
    SAFEARRAY* id=nullptr;
    if(FAILED(focus->GetRuntimeId(&id))||!id)return {};
    LONG lower=0,upper=-1;std::vector<int> token;
    if(SafeArrayGetDim(id)==1 && SUCCEEDED(SafeArrayGetLBound(id,1,&lower)) && SUCCEEDED(SafeArrayGetUBound(id,1,&upper)) && upper>=lower && upper-lower<32){
        int* values=nullptr;
        if(SUCCEEDED(SafeArrayAccessData(id,(void**)&values))){token.assign(values,values+upper-lower+1);SafeArrayUnaccessData(id);}
    }
    SafeArrayDestroy(id);return token;
}
bool CandidateReader::focus_matches(const Snapshot& s){return !s.focus_runtime.empty() && focus_token()==s.focus_runtime;}
Snapshot CandidateReader::read(uint64_t epoch){
    Snapshot s;s.foreground=GetForegroundWindow();s.epoch=epoch;s.time=GetTickCount64();
    if(!init_||!s.foreground)return {};
    DWORD tid=GetWindowThreadProcessId(s.foreground,nullptr);
    if(PRIMARYLANGID(LOWORD((UINT_PTR)GetKeyboardLayout(tid)))!=LANG_CHINESE)return {};
    s.focus=current_focus(s.foreground);
    if(cached_host_ && read_host(cached_host_,s)){s.focus_runtime=focus_token();if(!same_target(s)||s.focus_runtime.empty())return {};return s;}
    // EnumWindows omits some immersive IME windows. Enumerate frame windows and
    // their CoreWindow children directly; no screenshots or OCR are involved.
    for(HWND frame=FindWindowExW(nullptr,nullptr,L"ApplicationFrameWindow",nullptr);frame;frame=FindWindowExW(nullptr,frame,L"ApplicationFrameWindow",nullptr)){
        if(!IsWindowVisible(frame))continue;
        for(HWND child=FindWindowExW(frame,nullptr,L"Windows.UI.Core.CoreWindow",nullptr);child;child=FindWindowExW(frame,child,L"Windows.UI.Core.CoreWindow",nullptr)){
            if(read_host(child,s)){s.focus_runtime=focus_token();if(!same_target(s)||s.focus_runtime.empty())return {};return s;}
        }
    }
    for(HWND h=FindWindowExW(nullptr,nullptr,L"Windows.UI.Core.CoreWindow",nullptr);h;h=FindWindowExW(nullptr,h,L"Windows.UI.Core.CoreWindow",nullptr)){
        if(read_host(h,s)){s.focus_runtime=focus_token();if(!same_target(s)||s.focus_runtime.empty())return {};return s;}
    }
    return {};
}
}
