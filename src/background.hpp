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
    void paint(HDC dc,RECT bounds,bool contain=false);
    std::wstring error()const;
    bool animated()const;
    void mute(bool value);
    bool sound_playing()const;
};
std::wstring background_path(const std::wstring& root,BackgroundKind kind);
bool background_sound(const std::wstring& root,BackgroundKind kind);
int background_opacity(const std::wstring& root,BackgroundKind kind);
void paint_glass(HDC dc,RECT bounds,int opacity,int radius=10);
HWND show_background_settings(HINSTANCE instance,HWND owner,const std::wstring& root,BackgroundKind kind,bool visible=true);
void close_background_settings();
}
