#pragma once
#include <windows.h>
#include <uiautomation.h>
#include <string>
#include <vector>
#include <cstdint>

namespace ea {
template<class T> class Com {
    T* p_=nullptr;
public:
    ~Com(){if(p_)p_->Release();}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    T* operator->()const{return p_;}T* get()const{return p_;}
    T** put(){reset();return &p_;}void reset(){if(p_)p_->Release();p_=nullptr;}
    explicit operator bool()const{return p_!=nullptr;}
};
struct Candidate { int number=0;std::wstring word;bool selected=false;std::vector<std::wstring> senses; };
struct Snapshot {
    HWND foreground=nullptr,focus=nullptr,host=nullptr;
    RECT bounds{};uint64_t epoch=0;ULONGLONG time=0;
    std::vector<Candidate> candidates;
    std::vector<int> focus_runtime;
    bool valid()const{return foreground && host && !candidates.empty();}
};
class CandidateReader {
    Com<IUIAutomation> uia_;
    Com<IUIAutomationCondition> menu_condition_,item_condition_,index_condition_;
    Com<IUIAutomationCacheRequest> item_cache_;
    HWND cached_host_=nullptr;
    bool init_=false;
    bool read_host(HWND host,Snapshot& out);
    bool trusted_host(HWND host);
    std::vector<int> focus_token();
public:
    CandidateReader();
    bool available()const{return init_;}
    Snapshot read(uint64_t epoch);
    bool password_focus();
    bool candidate_gone(HWND host);
    bool focus_matches(const Snapshot& snapshot);
};
bool same_target(const Snapshot& s);
bool same_candidates(const Snapshot& a,const Snapshot& b);
}
