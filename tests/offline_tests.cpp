#include "offline.hpp"
#include <iostream>
#include <chrono>
#include <stdexcept>
int main(int argc,char**argv){try{
    int checks=0;auto check=[&](bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);};
    check(ea::wide(ea::utf8(L"发展 😀"))==L"发展 😀","Unicode roundtrip");
    for(auto invalid:{"\xff","\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82"})check(ea::wide(invalid).empty(),"Invalid UTF8 rejected");
    ea::OfflineTranslator t;check(argc==2&&t.open(ea::wide(argv[1]),false),"Open shared dictionaries independently of personal overrides");
    check(t.english_size()==232202&&t.chinese_size()==44190,"Dictionary coverage");
    auto r=t.translate_to_english(L"趋于完美了");check(!r.reference&&r.senses[0]==L"It is approaching perfection.","Screenshot sentence");
    r=t.translate_to_english(L"趋于稳定了");check(!r.senses.empty()&&!r.reference,"Productive template");
    r=t.translate_to_english(L"电脑软件设计开发流程");check(r.reference&&!r.senses.empty()&&r.unknown.empty(),"Unlisted sentence uses known words");
    r=t.translate_to_english(L"电脑龘龘软件");check(r.reference&&r.senses.empty()&&r.unknown==std::vector<std::wstring>{L"龘龘"},"Incomplete composition cannot become output; gaps retained as diagnostics");
    r=t.translate_to_english(L"我正在开会。请稍等。");check(!r.senses.empty()&&r.senses[0].find(L".")!=std::wstring::npos,"Clause punctuation");
    check(t.translate_to_english(L" \n ").senses.empty(),"Blank has no output");
    check(t.translate_to_english(std::wstring(8001,L'中')).senses.empty(),"Length guard");
    check(t.to_chinese(L"development").text.find(L"发展")!=std::wstring::npos,"Reverse noun");
    auto begin=std::chrono::steady_clock::now();
    for(int i=0;i<100;i++)check(!t.translate_to_english(L"电脑软件设计开发流程").senses.empty(),"Repeated composition");
    std::cout<<"composition_100_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count()<<" PASS "<<checks<<" checks\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
