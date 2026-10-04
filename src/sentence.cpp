#include "sentence.hpp"
#include "offline.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <chrono>
namespace ea {
struct SentenceEngine::Impl {
    std::mutex mutex;std::condition_variable changed;std::atomic<bool> cancelled{false};bool stop=false;
    uint64_t generation=0,handled=0;std::wstring desired;std::chrono::steady_clock::time_point due;
    std::unordered_map<std::wstring,NeuralReply> cache;std::deque<std::wstring> order;
    std::function<void()> updated;NeuralTranslator engine;std::thread worker;
    Impl(std::wstring root,std::function<void()> callback):updated(std::move(callback)),engine(std::move(root)),worker([this]{run();}){}
    ~Impl(){{std::lock_guard<std::mutex> lock(mutex);stop=true;cancelled=true;}changed.notify_one();worker.join();}
    void run(){
        std::unique_lock<std::mutex> lock(mutex);
        while(!stop){
            if(desired.empty()||generation==handled||cache.count(desired)){
                if(changed.wait_for(lock,std::chrono::seconds(60))==std::cv_status::timeout){lock.unlock();engine.reset();lock.lock();}
                continue;
            }
            auto serial=generation;
            if(changed.wait_until(lock,due,[&]{return stop||serial!=generation;}))continue;
            auto text=desired;handled=serial;cancelled=false;lock.unlock();
            auto reply=engine.translate(text,cancelled,TranslationDirection::ChineseToEnglish);
            if(reply.error.empty()&&!valid_english_output(reply.text)){reply.text.clear();reply.error=L"未生成完整英文，请补全中文句子。";}
            lock.lock();
            if(!stop&&!cancelled&&serial==generation){
                if(cache.size()>=64){cache.erase(order.front());order.pop_front();}cache[text]=std::move(reply);order.push_back(text);
                lock.unlock();if(updated)updated();lock.lock();
            }
        }
    }
};
SentenceEngine::SentenceEngine(std::wstring root,std::function<void()> updated):impl_(std::make_unique<Impl>(std::move(root),std::move(updated))){}
SentenceEngine::~SentenceEngine()=default;
void SentenceEngine::select(const std::wstring& text){
    std::lock_guard<std::mutex> lock(impl_->mutex);if(text==impl_->desired)return;impl_->desired=text;++impl_->generation;impl_->cancelled=true;impl_->due=std::chrono::steady_clock::now()+std::chrono::milliseconds(220);impl_->changed.notify_one();
}
std::optional<NeuralReply> SentenceEngine::lookup(const std::wstring& text){std::lock_guard<std::mutex> lock(impl_->mutex);auto found=impl_->cache.find(text);if(found==impl_->cache.end())return std::nullopt;return found->second;}
}
