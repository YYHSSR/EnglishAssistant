#include "online.hpp"
#include "dictionary.hpp"
#include <iostream>
int main(){
    auto reply=ea::fetch_translation(L"我正在学习英语");
    if(reply.text.empty()){std::cerr<<(reply.quota?"Quota exceeded":"Network translation unavailable")<<"\n";return 1;}
    std::cout<<ea::utf8(reply.text)<<"\n";return 0;
}
