#include "translation_box.hpp"
#include "background.hpp"
#include "dictionary.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <filesystem>
namespace {
bool wait(HWND button,int seconds){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);while(!IsWindowEnabled(button)&&std::chrono::steady_clock::now()<end){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}std::this_thread::sleep_for(std::chrono::milliseconds(5));}return IsWindowEnabled(button);}
}
int main(int argc,char**argv){
    if(argc<2)return 2;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;
    {
        auto root=ea::wide(argv[1]);auto dictionary=std::make_shared<ea::OfflineTranslator>();if(!dictionary->open(root))return 1;
        HWND window=ea::show_translation_box(GetModuleHandleW(nullptr),nullptr,[&]{return dictionary;},root,false);
        auto input=GetDlgItem(window,201),output=GetDlgItem(window,202),button=GetDlgItem(window,203);
        SetWindowTextW(input,L"Stateless GitHub App installation tokens rolled out");SendMessageW(button,BM_CLICK,0,0);
        if(!wait(button,30))return 1;wchar_t text[1024]{};GetWindowTextW(output,text,1024);
        std::wstring translated=text;if(translated.find(L"无状态")==std::wstring::npos||translated.find(L"令牌")==std::wstring::npos)result=1;
        std::cout<<ea::utf8(translated)<<"\n";
        RECT area{};GetClientRect(window,&area);HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);SendMessageW(window,WM_PRINT,(WPARAM)dc,PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);GdiFlush();
        BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+area.right*area.bottom*4;
        std::filesystem::create_directories(std::filesystem::path(root)/L"work");std::ofstream file(std::filesystem::path(root)/L"work/translation-background.bmp",std::ios::binary);file.write((const char*)&header,sizeof(header));file.write((const char*)&info.bmiHeader,sizeof(BITMAPINFOHEADER));file.write((const char*)bits,area.right*area.bottom*4);
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
        SetWindowTextW(input,std::wstring(4000,L'a').c_str());SendMessageW(button,BM_CLICK,0,0);auto start=std::chrono::steady_clock::now();ea::close_translation_box();if(std::chrono::steady_clock::now()-start>std::chrono::seconds(5))result=1;
        if(argc==3){ea::Background background;if(!background.load(ea::wide(argv[2]),false))return 1;background.visible(true);std::this_thread::sleep_for(std::chrono::seconds(2));
            auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=320;info.bmiHeader.biHeight=-180;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;auto bmp=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bmp);memset(bits,255,320*180*4);background.paint(dc,{0,0,320,180});GdiFlush();
            bool changed=false;auto* pixels=(unsigned char*)bits;for(int i=0;i<320*180*4;i++)if(pixels[i]!=255){changed=true;break;}if(!changed||!background.error().empty())result=1;background.visible(false);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);std::cout<<"Video decoded="<<changed<<" error="<<ea::utf8(background.error())<<"\n";
        }
    }
    CoUninitialize();return result;
}
