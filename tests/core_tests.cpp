#include "dictionary.hpp"
#include "candidates.hpp"
#include "options.hpp"
#include "online.hpp"
#include "documents.hpp"
#include "layout.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace ea;
static int checks=0;
static void require(bool passed,const char* why){++checks;if(!passed)throw std::runtime_error(why);}
int main(int argc,char**argv){
    try {
        require(wide(utf8(L"开发 😀"))==L"开发 😀","UTF-8 round trip");
        require(wide(std::string("\xff",1)).empty(),"Malformed UTF-8 rejected");
        auto s=parse_senses("v. develop\tv. exploit\tn. third");
        require(s.size()==3&&s[0]==L"develop"&&s[1]==L"exploit"&&s[2]==L"third","All output senses without POS");
        s=parse_senses("abbr. U.S.\tphr. a.b\tthird");
        require(s.size()==3&&s[0]==L"U.S."&&s[1]==L"a.b","Periods inside English preserved");
        s=parse_senses("\tn. hello|reading\tv. hello\tadj. world\r");
        require(s.size()==2&&s[0]==L"hello"&&s[1]==L"world","Empty senses, duplicate senses, reading, CRLF");
        std::filesystem::create_directories("test-fixtures");
        auto fixture=std::filesystem::absolute("test-fixtures/glossary.tsv");
        {std::ofstream out(fixture,std::ios::binary);out<<"\xef\xbb\xbf# generated fixture\r\n"<<utf8(L"苹果")<<"\tn. apple\r\n"<<utf8(L"开发")<<"\tv. develop\tv. exploit\r\nmalformed\n";}
        Dictionary d;require(d.open(fixture.wstring()),"Mapped BOM/unsorted fixture opens");
        require(d.size()==2,"Comments and malformed line ignored");
        require(d.lookup(L"开发")==std::vector<std::wstring>({L"develop",L"exploit"}),"Binary lookup after sorting");
        require(d.lookup(L"苹果")==std::vector<std::wstring>({L"apple"}),"Last entry lookup");
        require(d.lookup(L"不存在").empty(),"Unknown word has no guessed translation");
        auto personal=std::filesystem::absolute("test-fixtures/personal.tsv");
        {std::ofstream out(personal,std::ios::binary);out<<utf8(L"开发")<<"\tdevelopment\n"<<utf8(L"自定义")<<"\tcustom\n";}
        d.load_personal(personal.wstring());
        require(d.lookup(L"开发")==std::vector<std::wstring>({L"development"}),"Personal override takes priority");
        require(d.lookup(L"自定义")==std::vector<std::wstring>({L"custom"}),"Personal addition");
        d.load_personal(L"does-not-exist.tsv");
        require(d.lookup(L"开发")[0]==L"develop","Reload resets removed personal override");
        require(parse_senses("one\ttwo\tthree\tfour\tfive\tsix\tseven\teight\tnine").size()==8,"Up to eight English senses");
        Snapshot snapshot;
        snapshot.candidates={{3,L"逆",false,{L"reverse",L"contrary"}},{7,L"拟",false,{L"plan",L"intend"}}};
        auto options=english_options(snapshot);
        require(options.size()==4&&options[0].candidate==3&&options[1].sense==1&&options[2].candidate==7&&options[3].text==L"intend","Independent English numbering preserves native mapping");
        for(int i=0;i<8;i++)snapshot.candidates.push_back({9,L"测试",false,{std::to_wstring(i)}});
        options=english_options(snapshot);
        require(options.size()==12&&options[page_size].candidate==9&&options[page_size].text==L"5","Second page maps ninth offset correctly");
        require(move_selection(-1,1,12,0)==0&&move_selection(-1,-1,12,9)==9,"First arrow selects first item on visible page");
        require(move_selection(8,1,12,0)==9&&move_selection(9,-1,12,9)==8,"Arrow selection crosses pages in both directions");
        require(move_selection(0,-1,12,0)==0&&move_selection(11,1,12,9)==11,"Arrow selection stops at list boundaries");
        require(move_selection(0,1,0,0)==-1,"Empty list has no arrow selection");
        auto placement=popup_layout({103,523,828,570},{0,0,1030,656},120,8,0,true);
        require(placement.capacity>0&&placement.capacity<8&&placement.above,"Small upper area reduces rows rather than overlapping IME");
        require(placement.bounds.bottom<=523-MulDiv(44,120,96)&&placement.bounds.top>=MulDiv(6,120,96),"Pinyin reserve and upper screen margin remain clear");
        require(placement.bounds.left>=0&&placement.bounds.right<=1030,"Popup stays inside monitor width");
        auto last=popup_layout({103,523,828,570},{0,0,1030,656},120,8,1,true);
        require(last.capacity==placement.capacity&&last.page==1,"Short last page retains stable page capacity");
        auto below=popup_layout({10,20,700,60},{0,0,1920,1080},96,4,0,false);
        require(!below.above&&below.bounds.top>=72,"Near top of screen falls back below IME with clear gap");
        auto high_dpi=popup_layout({400,950,1200,1030},{0,0,1920,1080},192,20,0,true);
        require(high_dpi.capacity>0&&high_dpi.bounds.bottom<=862&&high_dpi.bounds.top>=12,"High DPI reserves scaled pinyin space");
        auto negative=popup_layout({-500,-100,-200,-50},{-1920,-1080,0,0},144,20,0,true);
        require(negative.capacity>0&&negative.bounds.left>=-1920&&negative.bounds.right<=0&&negative.bounds.top>=-1080,"Secondary monitor negative coordinates supported");
        require(popup_layout({10,40,100,80},{0,0,120,110},96,8,0,true).capacity==0,"Insufficient space never covers IME with a clamped popup");
        require(parse_translation(R"({"responseStatus":200,"responseData":{"translatedText":"Where are you from?"}})").text==L"Where are you from?","Online sentence parses");
        require(parse_translation(R"({"responseStatus":200,"responseData":{"translatedText":"I&#39;m here &amp; ready."}})").text==L"I'm here & ready.","Online HTML entities decoded");
        require(parse_translation(R"({"responseStatus":429,"quotaFinished":true})").quota,"Provider quota recognized");
        require(parse_translation(R"({"responseStatus":400,"responseData":{"translatedText":"SERVICE ERROR"}})").text.empty(),"Service errors cannot become candidates");
        require(parse_translation("not json").text.empty(),"Invalid JSON rejected");
        require(parse_translation(R"({"responseStatus":200,"responseData":{"translatedText":"hello\nworld"}})").text.empty(),"Multiline output rejected");
        require(parse_translation("{\"responseStatus\":200,\"responseData\":{\"translatedText\":\""+utf8(L"未翻译 hello")+"\"}}").text.empty(),"Untranslated Chinese rejected");
        require(parse_translation(R"({"responseStatus":200,"responseData":{"translatedText":123}})").text.empty(),"Wrong JSON type rejected");
        auto document=std::filesystem::absolute("test-fixtures/document.tsv");std::wstring document_text;
        require(save_document(document.wstring(),L"你好\thello\tHi\r\n"),"Native editor writes UTF-8 atomically");
        require(read_document(document.wstring(),document_text)&&document_text==L"你好\thello\tHi\r\n","Native editor Unicode and tabs round trip");
        {std::ofstream out(document,std::ios::binary);out<<"\xef\xbb\xbf"<<utf8(L"你来自哪里\tWhere are you from?\n");}
        require(read_document(document.wstring(),document_text)&&document_text==L"你来自哪里\tWhere are you from?\r\n","Native editor reads existing BOM and LF files");
        {std::ofstream out{std::filesystem::path(document.wstring()+L".saving")};out<<"occupied";}
        require(!save_document(document.wstring(),L"overwrite")&&read_document(document.wstring(),document_text)&&document_text==L"你来自哪里\tWhere are you from?\r\n","Native editor preserves original when replacement blocked");
        std::filesystem::remove(document.wstring()+L".saving");
        Dictionary missing;require(!missing.open(L"does-not-exist.tsv"),"Missing glossary reports failure");
        if(argc>1){
            Dictionary full;require(full.open(wide(argv[1])),"Full Qingjian glossary opens");
            full.load_phrases((std::filesystem::path(wide(argv[1])).parent_path()/L"phrases.tsv").wstring());
            require(full.lookup(L"你来自哪里")==std::vector<std::wstring>({L"Where are you from?"}),"Screenshot full sentence translated locally");
            require(full.lookup(L"你来自")==std::vector<std::wstring>({L"You come from"}),"Screenshot partial phrase translated locally");
            full.load_personal(personal.wstring());
            require(full.lookup(L"开发")[0]==L"development","Personal overrides bundled glossary");
            full.load_personal(L"does-not-exist.tsv");
            require(full.size()>200000,"Full dataset coverage");
            require(full.lookup(L"开发")==std::vector<std::wstring>({L"develop",L"exploit"}),"Real Qingjian output matches");
            full.load_supplements((std::filesystem::path(wide(argv[1])).parent_path()/L"supplements.tsv").wstring());
            require(full.lookup(L"发展")==std::vector<std::wstring>({L"develop",L"development",L"growth"}),"Development noun forms accompany original verb");
            require(full.lookup(L"开发")==std::vector<std::wstring>({L"develop",L"exploit",L"development"}),"Supplement appends without removing original senses");
            require(full.lookup(L"研究")==std::vector<std::wstring>({L"research",L"study"}),"Identical English forms are not duplicated by supplement");
            full.load_personal(personal.wstring());
            require(full.lookup(L"开发")==std::vector<std::wstring>({L"development"}),"Personal override still wins over supplement");
            full.load_personal(L"does-not-exist.tsv");
            full.load_supplements(L"does-not-exist.tsv");
            require(full.lookup(L"发展")==std::vector<std::wstring>({L"develop"}),"Supplement reload removes obsolete additions");
            full.load_supplements((std::filesystem::path(wide(argv[1])).parent_path()/L"supplements.tsv").wstring());
            const wchar_t* words[]={L"开发",L"编程",L"架构",L"苹果",L"电脑",L"测试",L"时间",L"朋友",L"工作",L"学习",L"数据库",L"程序员",L"未来",L"网络",L"文档",L"学校"};
            auto start=std::chrono::steady_clock::now();size_t hits=0;
            for(int i=0;i<100000;i++)hits+=!full.lookup(words[i%16]).empty();
            auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
            require(hits==100000,"100k common-word lookups");
            std::cout<<"entries="<<full.size()<<" index_bytes="<<full.index_bytes()<<" lookup_100000_us="<<us<<"\n";
        }
        std::cout<<"PASS "<<checks<<" checks\n";return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}
