#include "neural.hpp"
#include "dictionary.hpp"
#include "sentence.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
int main(int argc,char**argv){
    if(argc!=2)return 2;
    std::atomic<bool> cancelled{false};auto root=ea::wide(argv[1]);
    auto a=ea::neural_translate(root,L"Stateless GitHub App installation tokens rolled out",cancelled);
    std::cout<<ea::utf8(a.text)<<"\n"<<ea::utf8(a.error)<<"\n";
    if(!a.error.empty()||a.text.find(L"无状态")==std::wstring::npos||a.text.find(L"GitHub")==std::wstring::npos||a.text.find(L"令牌")==std::wstring::npos)return 1;
    auto b=ea::neural_translate(root,L"The server processes each request independently and does not store session data.\nThe train was delayed because of heavy rain.",cancelled);
    if(!b.error.empty()||b.text.find(L'\n')==std::wstring::npos||b.text.find(L"请求")==std::wstring::npos||b.text.find(L"火车")==std::wstring::npos||b.text.find(L"未收录")!=std::wstring::npos)return 1;
    {
        ea::NeuralTranslator session(root);auto forward=session.translate(L"我把英文输入进去后，希望自动翻译成中文。",cancelled,ea::TranslationDirection::ChineseToEnglish);
        std::cout<<ea::utf8(forward.text)<<"\n"<<ea::utf8(forward.error)<<"\n";
        if(!forward.error.empty()||forward.text.find(L"English")==std::wstring::npos||forward.text.find(L"Chinese")==std::wstring::npos||forward.text.find(L"[untranslated:")!=std::wstring::npos)return 1;
        auto reverse=session.translate(L"The train was delayed because of heavy rain.",cancelled);if(!reverse.error.empty()||reverse.text.find(L"火车")==std::wstring::npos)return 1;
        auto paragraph=session.translate(L"我正在开会。请稍等。\n我把英文输入进去后，希望自动翻译成中文。",cancelled,ea::TranslationDirection::ChineseToEnglish);if(!paragraph.error.empty()||paragraph.text.find(L'\n')==std::wstring::npos||paragraph.text.find(L"English")==std::wstring::npos)return 1;
    }
    {
        std::atomic<int> updates{0};ea::SentenceEngine sentences(root,[&]{++updates;});auto old=std::wstring(4000,L'中'),current=std::wstring(L"我把英文输入进去后，希望自动翻译成中文。");
        sentences.select(old);sentences.select(current);auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);std::optional<ea::NeuralReply> translated;
        while(!(translated=sentences.lookup(current))&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(!translated||!translated->error.empty()||translated->text.find(L"English")==std::wstring::npos||sentences.lookup(old)||updates!=1)return 1;
        sentences.select(L"");sentences.select(current);if(!sentences.lookup(current))return 1;
    }
    std::thread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(50));cancelled=true;});
    auto start=std::chrono::steady_clock::now();auto c=ea::neural_translate(root,std::wstring(4000,L'a'),cancelled);cancel.join();
    if(c.error.empty()||std::chrono::steady_clock::now()-start>std::chrono::seconds(5))return 1;
    {
        auto cold=std::filesystem::path(root)/L"work/cold-runtime";std::filesystem::create_directories(cold/L"runtime");std::filesystem::copy_file(std::filesystem::path(root)/L"runtime/windows-runtime.zip",cold/L"runtime/windows-runtime.zip",std::filesystem::copy_options::overwrite_existing);
        std::atomic<bool> cancelled_unpack{false};std::thread stop_unpack([&]{std::this_thread::sleep_for(std::chrono::milliseconds(50));cancelled_unpack=true;});auto begun=std::chrono::steady_clock::now();auto interrupted=ea::neural_translate(cold.wstring(),L"Hello.",cancelled_unpack);stop_unpack.join();
        if(interrupted.error.empty()||std::chrono::steady_clock::now()-begun>std::chrono::seconds(3))return 1;
        for(auto& entry:std::filesystem::directory_iterator(cold/L"runtime"))if(entry.path().filename().wstring().rfind(L"unpack-",0)==0)return 1;
    }
    std::cout<<"Neural translation and cancellation passed\n";
}
