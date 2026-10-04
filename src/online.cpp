#include "online.hpp"
#include "dictionary.hpp"
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>

namespace ea {
struct Internet {HINTERNET handle=nullptr;~Internet(){if(handle)WinHttpCloseHandle(handle);}Internet(const Internet&)=delete;Internet()=default;};
static std::wstring decode_entities(std::wstring s){
    for(const auto&pair:std::vector<std::pair<std::wstring,std::wstring>>{{L"&quot;",L"\""},{L"&#39;",L"'"},{L"&apos;",L"'"},{L"&lt;",L"<"},{L"&gt;",L">"},{L"&amp;",L"&"}}){size_t at=0;while((at=s.find(pair.first,at))!=std::wstring::npos){s.replace(at,pair.first.size(),pair.second);at+=pair.second.size();}}
    return s;
}
OnlineReply parse_translation(const std::string& body){
    try {
        auto j=nlohmann::json::parse(body);
        OnlineReply reply;
        if(j.contains("quotaFinished")&&j["quotaFinished"].is_boolean())reply.quota=j["quotaFinished"].get<bool>();
        if(reply.quota)return reply;
        if(!j.contains("responseStatus")||!j["responseStatus"].is_number_integer()||j["responseStatus"].get<int>()!=200)return {};
        if(!j.contains("responseData")||!j["responseData"].is_object())return {};
        auto data=j["responseData"];
        if(!data.contains("translatedText")||!data["translatedText"].is_string())return {};
        auto text=decode_entities(wide(data["translatedText"].get<std::string>()));
        bool latin=false;
        for(wchar_t c:text){if(c<32||c==127||(c>=0x3400&&c<=0x9fff))return {};if((c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z'))latin=true;}
        if(latin && text.size()<=500)reply.text=std::move(text);
        return reply;
    }catch(...){return {};}
}
OnlineReply fetch_translation(const std::wstring& text){
    auto bytes=utf8(text);if(bytes.empty()||bytes.size()>500)return {};
    static const char hex[]="0123456789ABCDEF";std::string encoded;
    for(unsigned char c:bytes){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')encoded+=(char)c;else{encoded+='%';encoded+=hex[c>>4];encoded+=hex[c&15];}}
    Internet session,connection,request;
    session.handle=WinHttpOpen(L"EnglishAssistant/0.2",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session.handle)return {};
    if(!WinHttpSetTimeouts(session.handle,2000,2500,2500,3500))return {};
    connection.handle=WinHttpConnect(session.handle,L"api.mymemory.translated.net",INTERNET_DEFAULT_HTTPS_PORT,0);if(!connection.handle)return {};
    auto path=wide("/get?q="+encoded+"&langpair=zh-CN%7Cen&mt=1");
    request.handle=WinHttpOpenRequest(connection.handle,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);if(!request.handle)return {};
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request.handle,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));
    if(!WinHttpSendRequest(request.handle,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request.handle,nullptr))return {};
    DWORD status=0,n=sizeof(status);if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&n,WINHTTP_NO_HEADER_INDEX)||status!=200)return {};
    std::string body;char buffer[4096];DWORD read=0;ULONGLONG start=GetTickCount64();
    do{if(!WinHttpReadData(request.handle,buffer,sizeof(buffer),&read))return {};body.append(buffer,read);if(body.size()>1024*1024||GetTickCount64()-start>5000)return {};}while(read);
    return parse_translation(body);
}
OnlineTranslator::OnlineTranslator(const std::wstring& path,std::function<void()> changed):state_path_(path),changed_(std::move(changed)){thread_=std::thread(&OnlineTranslator::run,this);}
OnlineTranslator::~OnlineTranslator(){ {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;pending_.clear();}wake_.notify_one();if(thread_.joinable())thread_.join();}
void OnlineTranslator::set_enabled(bool value){enabled=value;{std::lock_guard<std::mutex> lock(mutex_);++revision_;if(!value)pending_.clear();}wake_.notify_one();}
std::wstring OnlineTranslator::lookup(const std::wstring& word){std::lock_guard<std::mutex>lock(mutex_);auto i=cache_.find(word);return i==cache_.end()?L"":i->second.text;}
void OnlineTranslator::schedule(const std::vector<std::wstring>& words){
    std::lock_guard<std::mutex>lock(mutex_);if(!enabled){pending_.clear();return;}
    std::vector<std::wstring> next;
    for(const auto&word:words){if(word.size()<2||word.size()>120||word==inflight_)continue;auto i=cache_.find(word);if(i!=cache_.end()&&(!i->second.text.empty()||GetTickCount64()-i->second.time<60000))continue;next.push_back(word);break;}
    if(next==pending_)return;
    pending_=std::move(next);++revision_;wake_.notify_one();
}
void OnlineTranslator::run(){
    std::wstring quota_day;int characters=0;bool daily_exhausted=false;
    for(;;){
        std::vector<std::wstring> words;
        {std::unique_lock<std::mutex>lock(mutex_);wake_.wait(lock,[&]{return stopping_||!pending_.empty();});if(stopping_)break;auto version=revision_;
            if(wake_.wait_for(lock,std::chrono::milliseconds(800),[&]{return stopping_||revision_!=version;}))continue;
            words=std::move(pending_);pending_.clear();}
        for(const auto&word:words){
            {std::lock_guard<std::mutex>lock(mutex_);if(stopping_)return;if(!enabled)break;inflight_=word;}
            SYSTEMTIME now{};GetLocalTime(&now);wchar_t date[32];swprintf_s(date,L"%04u-%02u-%02u",now.wYear,now.wMonth,now.wDay);
            wchar_t saved[32]{};GetPrivateProfileStringW(L"quota",L"date",L"",saved,32,state_path_.c_str());
            if(quota_day!=date){
                quota_day=date;
                characters=wcscmp(saved,date)==0?std::clamp((int)GetPrivateProfileIntW(L"quota",L"characters",0,state_path_.c_str()),0,4000):0;
                daily_exhausted=wcscmp(saved,date)==0&&GetPrivateProfileIntW(L"quota",L"exhausted",0,state_path_.c_str())!=0;
            }
            OnlineReply reply;
            if(!daily_exhausted && characters+(int)word.size()<=4000){
                if(wcscmp(saved,date)!=0)WritePrivateProfileStringW(L"quota",L"exhausted",L"0",state_path_.c_str());
                characters+=(int)word.size();
                bool saved_date=WritePrivateProfileStringW(L"quota",L"date",date,state_path_.c_str())!=0;
                bool saved_count=WritePrivateProfileStringW(L"quota",L"characters",std::to_wstring(characters).c_str(),state_path_.c_str())!=0;
                // Never send if the portable directory cannot persist its usage limit.
                if(saved_date&&saved_count)reply=fetch_translation(word);else reply.quota=true;
                if(reply.quota){daily_exhausted=true;WritePrivateProfileStringW(L"quota",L"exhausted",L"1",state_path_.c_str());}
            }else reply.quota=true;
            quota_exhausted=reply.quota;
            {std::lock_guard<std::mutex>lock(mutex_);inflight_.clear();if(cache_.size()>512)cache_.clear();cache_[word]={enabled?reply.text:L"",GetTickCount64()};}
            changed_();if(reply.quota)break;
        }
    }
}
}
