#include "dictionary.hpp"
#include "candidates.hpp"
#include "options.hpp"
#include "offline.hpp"
#include "startup.hpp"
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
        std::vector<EnglishOption> layout_options;
        for(int i=0;i<20;++i)layout_options.push_back({i,0,L"词",L"word"});
        std::vector<int> widths(20,60);
        auto placement=popup_layout({103,523,828,570},{0,0,1030,656},120,layout_options,widths,0,true);
        require(placement.capacity>0&&placement.capacity<8&&placement.above,"Small upper area reduces rows rather than overlapping IME");
        require(placement.bounds.bottom<=523-MulDiv(44,120,96)&&placement.bounds.top>=MulDiv(6,120,96),"Pinyin reserve and upper screen margin remain clear");
        require(placement.bounds.left>=0&&placement.bounds.right<=1030,"Popup stays inside monitor width");
        auto last=popup_layout({103,523,828,570},{0,0,1030,656},120,layout_options,widths,4,true);
        require(last.capacity==placement.capacity&&last.page==4,"Short last page retains stable page capacity");
        auto below=popup_layout({10,20,700,60},{0,0,1920,1080},96,layout_options,widths,0,false);
        require(!below.above&&below.bounds.top>=72,"Near top of screen falls back below IME with clear gap");
        auto high_dpi=popup_layout({400,950,1200,1030},{0,0,1920,1080},192,layout_options,widths,0,true);
        require(high_dpi.capacity>0&&high_dpi.bounds.bottom<=862&&high_dpi.bounds.top>=12,"High DPI reserves scaled pinyin space");
        auto negative=popup_layout({-500,-100,-200,-50},{-1920,-1080,0,0},144,layout_options,widths,0,true);
        require(negative.capacity>0&&negative.bounds.left>=-1920&&negative.bounds.right<=0&&negative.bounds.top>=-1080,"Secondary monitor negative coordinates supported");
        require(popup_layout({10,40,100,80},{0,0,120,110},96,layout_options,widths,0,true).capacity==0,"Insufficient space never covers IME with a clamped popup");
        std::vector<EnglishOption> grouped={{1,0,L"发展",L"develop"},{1,1,L"发展",L"development"},{1,2,L"发展",L"growth"},{2,0,L"罚站",L"stand as punishment"}};
        std::vector<int> grouped_widths={60,110,55,160};
        auto wide_flow=grouped_flow(grouped,grouped_widths,0,9,560,96);
        require(wide_flow.groups.size()==2&&wide_flow.cells.size()==4,"Senses share a single Chinese group without losing choices");
        require(wide_flow.cells[0].bounds.top==wide_flow.cells[2].bounds.top&&wide_flow.cells[0].bounds.right<wide_flow.cells[1].bounds.left,"Wide panel places development senses side by side");
        auto narrow_flow=grouped_flow(grouped,grouped_widths,0,9,240,96);
        require(narrow_flow.cells[0].bounds.top<narrow_flow.cells[1].bounds.top&&narrow_flow.height>wide_flow.height,"Narrow panel wraps senses within their group");
        bool inside=true;for(const auto&cell:narrow_flow.cells)inside=inside&&cell.bounds.left>=18&&cell.bounds.right<=222;
        require(inside&&narrow_flow.groups[0].bounds.bottom<=narrow_flow.groups[1].bounds.top,"Wrapped cells stay inside panel and do not overlap next group");
        auto slice=grouped_flow(grouped,grouped_widths,1,2,420,96);
        require(slice.groups.size()==1&&slice.cells[0].index==1&&slice.cells[1].index==2,"Page split preserves native sense indices and group label");
        auto sentence_flow=grouped_flow({{1,0,L"长句",L"a long dictionary composition",true}},{1400},0,9,420,96);
        require(sentence_flow.cells[0].bounds.bottom-sentence_flow.cells[0].bounds.top>30&&sentence_flow.groups[0].word.find(L"词组参考")!=std::wstring::npos,"Long sentence wraps and reference label remains explicit");
        auto compact=popup_layout({103,523,828,570},{0,0,1030,656},120,grouped,grouped_widths,0,true);
        require(compact.capacity==page_size&&compact.flow.cells.size()==4,"Grouped senses fit without unnecessary paging");
        auto scaled=grouped_flow(grouped,{120,220,110,320},0,9,1120,192);
        require(scaled.height==2*wide_flow.height&&scaled.cells[1].bounds.left==2*wide_flow.cells[1].bounds.left,"Group layout scales consistently with DPI");
        auto small_window=popup_layout({100,800,800,850},{0,0,1920,1080},96,grouped,grouped_widths,0,false,300);
        require(small_window.bounds.right-small_window.bounds.left==288&&small_window.flow.cells[0].bounds.top<small_window.flow.cells[2].bounds.top,"Small target application wraps senses even on a large monitor");
        require(popup_layout({0,500,20,530},{0,0,60,1080},96,grouped,grouped_widths,0,false).capacity==0,"Unusable monitor width never produces invisible selectable cells");
        snapshot.candidates={{3,L"托盘",false,{L"tray"}},{1,L"托盘中增加一个选项",true,{L"Add an option to the system tray."}},{2,L"托盘中",false,{L"in the system tray"}}};
        options=english_options(snapshot);
        require(options[0].candidate==1&&options[1].candidate==2&&options[2].candidate==3,"Complete sentence precedes shorter translated candidates");
        require(options[0].sense==0&&options[0].text==L"Add an option to the system tray.","Sentence ranking retains native output mapping");
        require(startup_command(L"E:\\My App\\EnglishAssistant.exe")==L"\"E:\\My App\\EnglishAssistant.exe\"","Startup path with spaces is quoted");
        std::wstring test_key=L"Software\\EnglishAssistant\\Tests\\Run-"+std::to_wstring(GetCurrentProcessId());
        require(set_startup(L"E:\\My App\\EnglishAssistant.exe",true,test_key.c_str())&&startup_enabled(L"E:\\My App\\EnglishAssistant.exe",test_key.c_str()),"Enable startup uses test registry only");
        require(!startup_enabled(L"E:\\Moved\\EnglishAssistant.exe",test_key.c_str()),"Moved executable is not mistaken for existing startup command");
        require(set_startup(L"E:\\Moved\\EnglishAssistant.exe",true,test_key.c_str())&&startup_enabled(L"E:\\Moved\\EnglishAssistant.exe",test_key.c_str()),"Startup command can update after move");
        require(set_startup(L"E:\\Moved\\EnglishAssistant.exe",false,test_key.c_str())&&!startup_enabled(L"E:\\Moved\\EnglishAssistant.exe",test_key.c_str()),"Disable removes only own startup entry");
        require(set_startup(L"E:\\Moved\\EnglishAssistant.exe",false,test_key.c_str()),"Disabling absent startup entry succeeds");
        RegDeleteKeyW(HKEY_CURRENT_USER,test_key.c_str());
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
            auto source_data=std::filesystem::path(wide(argv[1])).parent_path();
            auto offline_root=std::filesystem::absolute("test-fixtures/offline");std::filesystem::create_directories(offline_root/L"data");
            for(auto name:{L"glossary-en.tsv",L"glossary-zh.tsv",L"phrases.tsv",L"supplements.tsv",L"chat-patterns.tsv"})std::filesystem::copy_file(source_data/name,offline_root/L"data"/name,std::filesystem::copy_options::overwrite_existing);
            {std::ofstream out{offline_root/L"personal.tsv"};out<<utf8(L"测试工具\tdiagnostic tool\n私有测试\tprivate test\n");}
            OfflineTranslator offline;require(offline.open(offline_root.wstring()),"Filtered bilingual offline library opens");
            require(offline.english_size()==232202&&offline.chinese_size()==44190,"Imported bilingual coverage matches screening report");
            require(offline.to_english(L"托盘中增加一个选项")==std::vector<std::wstring>{L"Add an option to the system tray."},"User screenshot sentence available offline");
            require(offline.to_english(L"请打开测试工具")==std::vector<std::wstring>{L"Please open diagnostic tool."},"Template uses complete known personal slot");
            require(offline.to_english(L"请打开完全未收录的超长私有工具").empty(),"Unknown template slot never produces a partial English sentence");
            require(offline.to_english(L"私有测试")[0]==L"private test","Personal dictionary takes priority in offline translator");
            auto translated=offline.to_chinese(L"I am in a meeting.");
            require(translated.exact&&translated.text==L"我正在开会","English chat sentence translates fully offline");
            translated=offline.to_chinese(L"  I NEED MORE TIME!  ");
            require(translated.exact&&translated.text==L"我需要更多时间","Case whitespace and terminal punctuation normalized");
            translated=offline.to_chinese(L"Please open diagnostic tool.");
            require(translated.exact&&translated.text==L"请打开测试工具","Reverse sentence template uses personal term");
            translated=offline.to_chinese(L"private test");
            require(translated.exact&&translated.text==L"私有测试","Personal bilingual entry applies to pasted English");
            translated=offline.to_chinese(L"development");
            require(translated.exact&&translated.text.find(L"发展")!=std::wstring::npos,"Qingjian English-to-Chinese noun entry retained");
            translated=offline.to_chinese(L"I need development.");
            require(translated.exact&&translated.text==L"我需要发展","Sentence slot selects one meaning rather than joining alternative noun senses");
            translated=offline.to_chinese(L"Hello zxqvunknownterm!");
            require(!translated.exact&&translated.unknown==std::vector<std::wstring>{L"zxqvunknownterm"}&&translated.text.find(L"〔未收录：zxqvunknownterm〕")!=std::wstring::npos,"Unknown English is marked and partial result is not a full translation");
            translated=offline.to_chinese(L"hello\nworld");
            require(!translated.exact&&translated.text.find(L"\r\n")!=std::wstring::npos,"Reference output retains paragraph boundary");
            require(offline.to_chinese(L" \r\n ").text.empty(),"Empty input has no invented translation");
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
