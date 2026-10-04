#pragma once
#include <windows.h>
#include <memory>
#include <string>
namespace ea {
enum class BackgroundKind {Translation,Candidates};
constexpr UINT background_changed=WM_APP+71;
class Background {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    Background();~Background();
    bool load(const std::wstring& path,bool sound);
    void visible(bool value);
    void paint(HDC dc,RECT bounds);
    std::wstring error()const;
    bool animated()const;
};
std::wstring background_path(const std::wstring& root,BackgroundKind kind);
bool background_sound(const std::wstring& root,BackgroundKind kind);
void show_background_settings(HINSTANCE instance,HWND owner,const std::wstring& root,BackgroundKind kind);
void close_background_settings();
}
