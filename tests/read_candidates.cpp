#include "candidates.hpp"
#include "dictionary.hpp"
#include <iostream>
int main(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    {ea::CandidateReader reader;auto s=reader.read(1);std::cout<<"valid="<<s.valid()<<" count="<<s.candidates.size()<<"\n";for(const auto&c:s.candidates)std::cout<<c.number<<"\t"<<ea::utf8(c.word)<<"\n";}
    CoUninitialize();
}
