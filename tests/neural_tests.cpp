#include "neural.hpp"
#include "dictionary.hpp"
#include <iostream>
#include <thread>
#include <chrono>
int main(int argc,char**argv){
    if(argc!=2)return 2;
    std::atomic<bool> cancelled{false};auto root=ea::wide(argv[1]);
    auto a=ea::neural_translate(root,L"Stateless GitHub App installation tokens rolled out",cancelled);
    std::cout<<ea::utf8(a.text)<<"\n"<<ea::utf8(a.error)<<"\n";
    if(!a.error.empty()||a.text.find(L"无状态")==std::wstring::npos||a.text.find(L"GitHub")==std::wstring::npos||a.text.find(L"令牌")==std::wstring::npos)return 1;
    auto b=ea::neural_translate(root,L"The server processes each request independently and does not store session data.\nThe train was delayed because of heavy rain.",cancelled);
    if(!b.error.empty()||b.text.find(L'\n')==std::wstring::npos||b.text.find(L"请求")==std::wstring::npos||b.text.find(L"火车")==std::wstring::npos||b.text.find(L"未收录")!=std::wstring::npos)return 1;
    std::thread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(50));cancelled=true;});
    auto start=std::chrono::steady_clock::now();auto c=ea::neural_translate(root,std::wstring(4000,L'a'),cancelled);cancel.join();
    if(c.error.empty()||std::chrono::steady_clock::now()-start>std::chrono::seconds(5))return 1;
    std::cout<<"Neural translation and cancellation passed\n";
}
