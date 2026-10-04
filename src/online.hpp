#pragma once
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
namespace ea {
struct OnlineReply {std::wstring text;bool quota=false;};
OnlineReply fetch_translation(const std::wstring& text);
OnlineReply parse_translation(const std::string& json);
class OnlineTranslator {
    struct Entry {std::wstring text;ULONGLONG time;};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::unordered_map<std::wstring,Entry> cache_;
    std::vector<std::wstring> pending_;
    std::wstring inflight_,state_path_;
    std::thread thread_;
    std::function<void()> changed_;
    bool stopping_=false;
    uint64_t revision_=0;
    void run();
public:
    std::atomic<bool> enabled{false},quota_exhausted{false};
    OnlineTranslator(const std::wstring& state_path,std::function<void()> changed);
    ~OnlineTranslator();
    void set_enabled(bool value);
    std::wstring lookup(const std::wstring& word);
    void schedule(const std::vector<std::wstring>& words);
};
}
