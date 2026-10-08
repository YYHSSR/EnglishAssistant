#include "translation_box.hpp"
#include "background.hpp"
#include "dictionary.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <commctrl.h>
#include <dwmapi.h>
namespace {
bool wait(HWND button,int seconds){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);while(!IsWindowEnabled(button)&&std::chrono::steady_clock::now()<end){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}std::this_thread::sleep_for(std::chrono::milliseconds(5));}return IsWindowEnabled(button);}
void print_client(HWND window,HDC dc){
    SendMessageW(window,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT|PRF_ERASEBKGND);
    for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        RECT area{};GetWindowRect(child,&area);MapWindowPoints(HWND_DESKTOP,window,(POINT*)&area,2);int width=area.right-area.left,height=area.bottom-area.top;if(width<=0||height<=0)continue;
        auto memory=CreateCompatibleDC(dc);auto bmp=CreateCompatibleBitmap(dc,width,height);auto old=SelectObject(memory,bmp);SendMessageW(child,WM_PRINT,(WPARAM)memory,PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND);BitBlt(dc,area.left,area.top,width,height,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteObject(bmp);DeleteDC(memory);
    }
}
void capture(HWND window,const std::filesystem::path& path){
    RECT area{};GetClientRect(window,&area);auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=-area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);print_client(window,dc);GdiFlush();
    if(path.extension()==L".png"){if(!ea::save_png(bitmap,path.wstring()))throw std::runtime_error("PNG capture failed");SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);return;}
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+area.right*area.bottom*4;std::ofstream file(path,std::ios::binary);file.write((const char*)&header,sizeof(header));file.write((const char*)&info.bmiHeader,sizeof(BITMAPINFOHEADER));file.write((const char*)bits,area.right*area.bottom*4);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
}
COLORREF pixel(HWND window,int x,int y,bool children){
    auto dc=CreateCompatibleDC(nullptr);auto bmp=CreateBitmap(900,700,1,32,nullptr);auto old=SelectObject(dc,bmp);
    if(children)print_client(window,dc);else SendMessageW(window,WM_PRINTCLIENT,(WPARAM)dc,PRF_CLIENT);
    auto value=GetPixel(dc,x,y);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);return value;
}
bool test_backgrounds(const std::wstring& root,ea::DictionaryProvider provider){
    auto folder=std::filesystem::path(root)/L"work/background-settings";std::filesystem::create_directories(folder);auto isolated=folder.wstring(),config=(folder/L"settings.ini").wstring();
    for(auto kind:{ea::BackgroundKind::Translation,ea::BackgroundKind::Candidates}){auto section=kind==ea::BackgroundKind::Translation?L"background_translation":L"background_candidates";WritePrivateProfileStringW(section,L"path",ea::background_path(root,kind).c_str(),config.c_str());WritePrivateProfileStringW(section,L"opacity",nullptr,config.c_str());}
    auto instance=GetModuleHandleW(nullptr);auto translation=ea::show_background_settings(instance,nullptr,isolated,ea::BackgroundKind::Translation,false),candidates=ea::show_background_settings(instance,nullptr,isolated,ea::BackgroundKind::Candidates,false);
    bool ok=translation&&candidates&&ea::background_opacity(isolated,ea::BackgroundKind::Translation)==40;
    SendMessageW(translation,WM_SHOWWINDOW,TRUE,0);std::this_thread::sleep_for(std::chrono::milliseconds(180));MSG timer{};ok&=!PeekMessageW(&timer,translation,WM_TIMER,WM_TIMER,PM_REMOVE);SendMessageW(translation,WM_SHOWWINDOW,FALSE,0);
    auto window=ea::show_translation_box(instance,nullptr,provider,isolated,false),input=GetDlgItem(window,201);
    // Transparent native EDIT must match the composited parent pixel exactly.
    for(int opacity:{0,100,35}){
        auto slider=GetDlgItem(translation,304);SendMessageW(slider,TBM_SETPOS,TRUE,opacity);SendMessageW(translation,WM_HSCROLL,TB_THUMBTRACK,(LPARAM)slider);ea::reload_translation_background(false);
        ok&=ea::background_opacity(isolated,ea::BackgroundKind::Translation)==opacity&&ea::background_opacity(isolated,ea::BackgroundKind::Candidates)==40;
        RECT bounds{};GetWindowRect(input,&bounds);MapWindowPoints(HWND_DESKTOP,window,(POINT*)&bounds,2);int x=bounds.left+100,y=bounds.top+100;
        auto control=pixel(window,x,y,true),parent=pixel(window,x,y,false);ok&=control==parent;
        if(opacity==100)ok&=control==RGB(255,255,255);
        if(opacity==0)ok&=control!=RGB(255,255,255);
    }
    SetWindowTextW(input,L"Editable text remains selectable.");SendMessageW(input,EM_SETSEL,0,8);DWORD begin=0,end=0;SendMessageW(input,EM_GETSEL,(WPARAM)&begin,(LPARAM)&end);ok&=begin==0&&end==8;SendMessageW(input,EM_REPLACESEL,TRUE,(LPARAM)L"Updated");wchar_t text[128]{};GetWindowTextW(input,text,128);ok&=std::wstring(text)==L"Updated text remains selectable.";
    // Long text must remain reachable after removing visible scrollbars.
    std::wstring long_text;for(int line=0;line<120;++line)long_text+=L"A selectable line of translated text.\r\n";
    auto output=GetDlgItem(window,202);SetWindowTextW(output,long_text.c_str());SendMessageW(output,EM_SETSEL,0,8);
    auto before=SendMessageW(output,EM_GETFIRSTVISIBLELINE,0,0);UINT wheel_lines=0;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&wheel_lines,0);
    SendMessageW(output,WM_MOUSEWHEEL,MAKEWPARAM(0,(WORD)-60),0);SendMessageW(output,WM_MOUSEWHEEL,MAKEWPARAM(0,(WORD)-60),0);
    if(wheel_lines)ok&=SendMessageW(output,EM_GETFIRSTVISIBLELINE,0,0)>before;
    SendMessageW(output,EM_GETSEL,(WPARAM)&begin,(LPARAM)&end);ok&=begin==0&&end==8;
    SendMessageW(output,EM_SETSEL,-1,-1);SendMessageW(output,EM_SCROLLCARET,0,0);ok&=SendMessageW(output,EM_GETFIRSTVISIBLELINE,0,0)>before;
    ok&=!(GetWindowLongPtrW(output,GWL_STYLE)&WS_VSCROLL);SetWindowTextW(output,L"");
    auto handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<40;++i){auto slider=GetDlgItem(translation,304);SendMessageW(slider,TBM_SETPOS,TRUE,i);SendMessageW(translation,WM_HSCROLL,TB_THUMBTRACK,(LPARAM)slider);ea::reload_translation_background(false);pixel(window,100,100,true);}
    ok&=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=handles+2;
    auto slider=GetDlgItem(translation,304);SendMessageW(slider,TBM_SETPOS,TRUE,35);SendMessageW(translation,WM_HSCROLL,TB_THUMBTRACK,(LPARAM)slider);ea::reload_translation_background(false);
    SetWindowPos(window,nullptr,0,0,650,480,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ea::reload_translation_background(false);capture(window,std::filesystem::path(root)/L"work/translation-resized.bmp");
    capture(translation,std::filesystem::path(root)/L"work/background-settings.bmp");
    ea::close_translation_box();ea::close_background_settings();
    auto reopened=ea::show_background_settings(instance,nullptr,isolated,ea::BackgroundKind::Translation,false);ok&=SendMessageW(GetDlgItem(reopened,304),TBM_GETPOS,0,0)==35;ea::close_background_settings();
    // A synthetic wide image verifies complete fit, letterboxing and cache mode changes.
    auto fixture=folder/L"wide.bmp";BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=54;header.bfSize=62;BITMAPINFOHEADER info{};info.biSize=40;info.biWidth=2;info.biHeight=1;info.biPlanes=1;info.biBitCount=32;BYTE colors[8]={0,0,255,255,255,0,0,255};
    {std::ofstream file(fixture,std::ios::binary);file.write((const char*)&header,sizeof(header));file.write((const char*)&info,sizeof(info));file.write((const char*)colors,8);}
    ea::Background image;ok&=image.load(fixture.wstring(),false)&&!image.animated();auto dc=CreateCompatibleDC(nullptr);auto bmp=CreateBitmap(40,80,1,32,nullptr);auto old=SelectObject(dc,bmp);RECT area{0,0,40,80};FillRect(dc,&area,(HBRUSH)GetStockObject(WHITE_BRUSH));image.paint(dc,area,true);
    auto left=GetPixel(dc,5,40),right=GetPixel(dc,34,40);ok&=GetPixel(dc,20,5)==RGB(255,255,255)&&GetRValue(left)>GetBValue(left)&&GetBValue(right)>GetRValue(right);
    image.paint(dc,area,false);ok&=GetPixel(dc,20,5)!=RGB(255,255,255);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);
    std::cout<<"Background fit, independent opacity persistence, native editing and resizing="<<ok<<"\n";return ok;
}
}
int main(int argc,char**argv){
    if(argc<2)return 2;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    bool documentation=argc==3&&std::string(argv[2])=="--docs";
    int result=0;
    {
        auto root=ea::wide(argv[1]);auto dictionary=std::make_shared<ea::OfflineTranslator>();if(!dictionary->open(root,!documentation))return 1;
        std::wstring appearance;if(documentation){auto folder=std::filesystem::path(root)/L"work/docs-appearance";std::filesystem::create_directories(folder);appearance=folder.wstring();WritePrivateProfileStringW(L"background_translation",L"path",(std::filesystem::path(root)/L"resources/backgrounds/moon-garden.png").c_str(),(folder/L"settings.ini").c_str());}
        HWND window=ea::show_translation_box(GetModuleHandleW(nullptr),nullptr,[&]{return dictionary;},root,false,appearance);
        // Exercise the opaque title toolbar offscreen without taking focus.
        RECT original{};GetWindowRect(window,&original);SetWindowPos(window,nullptr,-20000,-20000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(window,SW_SHOWNOACTIVATE);DwmFlush();
        bool caption_ok=true;
        for(int id:{207,208,209}){auto control=GetDlgItem(window,id);RECT bounds{};GetWindowRect(control,&bounds);caption_ok&=control&&IsWindowVisible(control)&&SendMessageW(window,WM_NCHITTEST,0,MAKELPARAM((bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2))==HTCLIENT;}
        SendMessageW(GetDlgItem(window,207),BM_CLICK,0,0);caption_ok&=IsIconic(window);ShowWindow(window,SW_SHOWNOACTIVATE);
        SendMessageW(GetDlgItem(window,208),BM_CLICK,0,0);caption_ok&=IsZoomed(window);SendMessageW(GetDlgItem(window,208),BM_CLICK,0,0);caption_ok&=!IsZoomed(window);
        ShowWindow(window,SW_HIDE);SetWindowPos(window,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,SWP_NOZORDER|SWP_NOACTIVATE);std::cout<<"Caption toolbar hit tests and minimize/maximize/restore="<<caption_ok<<"\n";if(!caption_ok)result=1;
        auto input=GetDlgItem(window,201),output=GetDlgItem(window,202),button=GetDlgItem(window,203),clear=GetDlgItem(window,206);
        RECT clear_bounds{},direction_bounds{},copy_bounds{},input_bounds{};GetWindowRect(clear,&clear_bounds);GetWindowRect(GetDlgItem(window,204),&direction_bounds);GetWindowRect(button,&copy_bounds);GetWindowRect(input,&input_bounds);
        if(clear_bounds.top!=direction_bounds.top||clear_bounds.top!=copy_bounds.top||clear_bounds.right>direction_bounds.left||direction_bounds.right>copy_bounds.left||copy_bounds.bottom>=input_bounds.top)result=1;
        for(auto bounds:{clear_bounds,direction_bounds,copy_bounds})if(SendMessageW(window,WM_NCHITTEST,0,MAKELPARAM((bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2))!=HTCLIENT)result=1;
        RECT outer{};GetWindowRect(window,&outer);auto hit=[&](int x,int y){return SendMessageW(window,WM_NCHITTEST,0,MAKELPARAM(outer.left+x,outer.top+y));};
        if(hit(1,1)!=HTTOPLEFT||hit((outer.right-outer.left)/2,1)!=HTTOP||hit((outer.right-outer.left)*3/4,20)!=HTCAPTION)result=1;
        SetWindowTextW(input,L"Stateless GitHub App installation tokens rolled out");
        if(!wait(button,30))return 1;wchar_t text[1024]{};GetWindowTextW(output,text,1024);
        std::wstring translated=text;if(translated.find(L"无状态")==std::wstring::npos||translated.find(L"令牌")==std::wstring::npos)result=1;
        std::cout<<ea::utf8(translated)<<"\n";
        std::filesystem::create_directories(std::filesystem::path(root)/L"work");capture(window,std::filesystem::path(root)/L"work/translation-background.bmp");
        if(documentation){std::filesystem::create_directories(std::filesystem::path(root)/L"resources/screenshots");capture(window,std::filesystem::path(root)/L"resources/screenshots/translation-en-zh.png");}
        SetWindowTextW(input,L"我把英文输入进去后，希望自动翻译成中文。");if(!wait(button,30))return 1;GetWindowTextW(output,text,1024);std::wstring english=text;if(english.find(L"English")==std::wstring::npos||english.find(L"Chinese")==std::wstring::npos||english.find(L"[untranslated:")!=std::wstring::npos)result=1;
        std::cout<<"Automatic Chinese -> English: "<<ea::utf8(english)<<"\n";
        capture(window,std::filesystem::path(root)/L"work/translation-chinese-english.bmp");
        if(documentation)capture(window,std::filesystem::path(root)/L"resources/screenshots/translation.png");
        // The output remains editable. Partial edits preserve the source, and
        // deleting the entire result clears the source without rescheduling it.
        if(GetWindowLongPtrW(output,GWL_STYLE)&ES_READONLY)result=1;
        auto source_length=GetWindowTextLengthW(input);SendMessageW(output,EM_SETSEL,0,4);SendMessageW(output,EM_REPLACESEL,TRUE,(LPARAM)L"Edited");
        if(GetWindowTextLengthW(input)!=source_length||!IsWindowEnabled(button))result=1;
        SendMessageW(output,EM_SETSEL,0,-1);SendMessageW(output,WM_CHAR,VK_BACK,0);
        if(GetWindowTextLengthW(input)||GetWindowTextLengthW(output)||IsWindowEnabled(button))result=1;
        SetWindowTextW(input,L"Hello.");if(!wait(button,30))return 1;
        SendMessageW(input,EM_SETSEL,0,-1);SendMessageW(input,EM_REPLACESEL,TRUE,(LPARAM)L"");
        if(GetWindowTextLengthW(input)||GetWindowTextLengthW(output)||IsWindowEnabled(button))result=1;
        SendMessageW(clear,BM_CLICK,0,0);if(GetWindowTextLengthW(input)||GetWindowTextLengthW(output)||IsWindowEnabled(button))result=1;
        SendMessageW(window,WM_COMMAND,212,0);SetWindowTextW(input,L"development");if(!wait(button,30))return 1;GetWindowTextW(output,text,1024);if(std::wstring(text).find(L"发展")==std::wstring::npos)result=1;
        SetWindowTextW(input,std::wstring(4000,L'a').c_str());SendMessageW(window,WM_TIMER,2,0);if(!IsWindowEnabled(input))result=1;
        SendMessageW(clear,BM_CLICK,0,0);auto clear_deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
        while(std::chrono::steady_clock::now()<clear_deadline){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        if(GetWindowTextLengthW(input)||GetWindowTextLengthW(output)||IsWindowEnabled(button)||!IsWindowEnabled(clear))result=1;
        SetWindowTextW(input,std::wstring(4000,L'a').c_str());SendMessageW(window,WM_TIMER,2,0);
        SetWindowTextW(output,L"Manually corrected translation");auto edit_deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
        while(std::chrono::steady_clock::now()<edit_deadline){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        GetWindowTextW(output,text,1024);if(std::wstring(text)!=L"Manually corrected translation"||GetWindowTextLengthW(input)!=4000||!IsWindowEnabled(button))result=1;
        SendMessageW(output,EM_SETSEL,0,-1);SendMessageW(output,EM_REPLACESEL,TRUE,(LPARAM)L"");
        if(GetWindowTextLengthW(input)||GetWindowTextLengthW(output)||IsWindowEnabled(button))result=1;
        SetWindowTextW(input,L"Hello.");if(!wait(button,30))return 1;GetWindowTextW(output,text,1024);if(std::wstring(text).find(L"你好")==std::wstring::npos)result=1;
        SetWindowTextW(input,L" \r\n ");GetWindowTextW(output,text,1024);if(text[0]||IsWindowEnabled(button))result=1;
        SetWindowTextW(input,std::wstring(4000,L'a').c_str());SendMessageW(window,WM_TIMER,2,0);auto start=std::chrono::steady_clock::now();ea::close_translation_box();if(std::chrono::steady_clock::now()-start>std::chrono::seconds(5))result=1;
        if(!test_backgrounds(root,[&]{return dictionary;}))result=1;
        {
            const unsigned char gif[]={71,73,70,56,57,97,1,0,1,0,128,0,0,0,0,0,255,255,255,33,255,11,78,69,84,83,67,65,80,69,50,46,48,3,1,0,0,0,33,249,4,0,10,0,0,0,44,0,0,0,0,1,0,1,0,0,2,2,68,1,0,33,249,4,0,10,0,0,0,44,0,0,0,0,1,0,1,0,0,2,2,76,1,0,59};
            auto gif_path=std::filesystem::path(root)/L"work/animated-test.gif";{std::ofstream file(gif_path,std::ios::binary);file.write((const char*)gif,sizeof(gif));}
            ea::Background animation;if(!animation.load(gif_path.wstring(),false)||!animation.animated())return 1;animation.visible(true);
            auto dc=CreateCompatibleDC(nullptr);auto bitmap=CreateBitmap(4,4,1,32,nullptr);auto old=SelectObject(dc,bitmap);animation.paint(dc,{0,0,4,4});auto initial=GetPixel(dc,2,2);std::this_thread::sleep_for(std::chrono::milliseconds(110));animation.paint(dc,{0,0,4,4});auto next=GetPixel(dc,2,2);if(initial==next)result=1;SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);std::cout<<"GIF animation changed="<<(initial!=next)<<"\n";
        }
        if(argc==3&&!documentation){ea::Background background;if(!background.load(ea::wide(argv[2]),true))return 1;background.mute(true);background.visible(true);std::this_thread::sleep_for(std::chrono::seconds(2));
            auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=320;info.bmiHeader.biHeight=-180;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;auto bmp=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bmp);memset(bits,255,320*180*4);background.paint(dc,{0,0,320,180});GdiFlush();
            bool changed=false;auto* pixels=(unsigned char*)bits;for(int i=0;i<320*180*4;i++)if(pixels[i]!=255){changed=true;break;}bool sound=background.sound_playing();if(!changed||!sound||!background.error().empty())result=1;background.visible(false);std::this_thread::sleep_for(std::chrono::milliseconds(200));if(background.sound_playing())result=1;SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);std::cout<<"Video decoded="<<changed<<" audio playing="<<sound<<" hidden paused="<<!background.sound_playing()<<" error="<<ea::utf8(background.error())<<"\n";
            auto folder=std::filesystem::path(root)/L"work/background-settings";auto config=(folder/L"settings.ini").wstring();WritePrivateProfileStringW(L"background_translation",L"path",ea::wide(argv[2]).c_str(),config.c_str());WritePrivateProfileStringW(L"background_translation",L"opacity",L"0",config.c_str());
            auto box=ea::show_translation_box(GetModuleHandleW(nullptr),nullptr,[&]{return dictionary;},folder.wstring(),false);SendMessageW(box,WM_SHOWWINDOW,TRUE,0);std::this_thread::sleep_for(std::chrono::milliseconds(500));SendMessageW(box,WM_TIMER,1,0);bool match=pixel(box,100,150,true)==pixel(box,100,150,false);if(!match)result=1;capture(box,std::filesystem::path(root)/L"work/translation-video.bmp");ea::close_translation_box();
            auto settings=ea::show_background_settings(GetModuleHandleW(nullptr),nullptr,folder.wstring(),ea::BackgroundKind::Translation,false);SendMessageW(settings,WM_SHOWWINDOW,TRUE,0);std::this_thread::sleep_for(std::chrono::milliseconds(150));MSG timer{};bool ticking=PeekMessageW(&timer,settings,WM_TIMER,WM_TIMER,PM_REMOVE)!=0;if(!ticking)result=1;SendMessageW(settings,WM_SHOWWINDOW,FALSE,0);ea::close_background_settings();std::cout<<"Transparent video text panel="<<match<<" animated preview timer="<<ticking<<"\n";
        }
    }
    CoUninitialize();return result;
}
