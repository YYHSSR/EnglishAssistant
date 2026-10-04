#include "background.hpp"
#include "candidates.hpp"
#include <gdiplus.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfplay.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <chrono>
namespace ea {
namespace {
struct GraphicsRuntime {
    ULONG_PTR token=0;
    GraphicsRuntime(){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);MFStartup(MF_VERSION);}
    ~GraphicsRuntime(){MFShutdown();if(token)Gdiplus::GdiplusShutdown(token);}
};
void runtime(){static GraphicsRuntime initialized;}
struct AudioState{std::atomic<bool> active{false};std::atomic<HRESULT> failure{S_OK};};
class AudioEvents final:public IMFPMediaPlayerCallback{
    std::atomic<ULONG> refs_{1};std::shared_ptr<AudioState> state_;
public:
    explicit AudioEvents(std::shared_ptr<AudioState> state):state_(std::move(state)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==__uuidof(IMFPMediaPlayerCallback)){*out=this;AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{auto count=--refs_;if(!count)delete this;return count;}
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* event)override{
        if(FAILED(event->hrEvent)){state_->failure=event->hrEvent;return;}if(!state_->active)return;
        if(event->eEventType==MFP_EVENT_TYPE_MEDIAITEM_SET)event->pMediaPlayer->Play();
        if(event->eEventType==MFP_EVENT_TYPE_PLAYBACK_ENDED){PROPVARIANT zero{};zero.vt=VT_I8;zero.hVal.QuadPart=0;event->pMediaPlayer->SetPosition(MFP_POSITIONTYPE_100NS,&zero);event->pMediaPlayer->Play();}
    }
};
const wchar_t* section(BackgroundKind kind){return kind==BackgroundKind::Translation?L"background_translation":L"background_candidates";}
std::wstring extension(const std::wstring& path){auto ext=std::filesystem::path(path).extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),towlower);return ext;}
bool video_file(const std::wstring& path){auto ext=extension(path);return ext==L".mp4"||ext==L".wmv"||ext==L".m4v"||ext==L".mov"||ext==L".avi";}
}
struct Background::Impl {
    std::unique_ptr<Gdiplus::Image> image;
    std::unique_ptr<Gdiplus::Bitmap> cached;int cached_width=0,cached_height=0;bool cached_contain=false;
    std::vector<BYTE> frame;UINT width=0,height=0;
    UINT gif_count=0;std::vector<UINT> gif_delays;ULONGLONG gif_start=0;
    std::thread decoder;std::mutex mutex;std::condition_variable wake;
    bool stop=false,shown=false;std::wstring failure;
    Com<IMFPMediaPlayer> audio;AudioEvents* events=nullptr;
    std::shared_ptr<AudioState> audio_state=std::make_shared<AudioState>();
    ~Impl(){
        audio_state->active=false;
        if(audio)audio->Shutdown();
        audio.reset();if(events)events->Release();
        {std::lock_guard<std::mutex> lock(mutex);stop=true;}wake.notify_all();if(decoder.joinable())decoder.join();
    }
    void decode(std::wstring path){
        struct Apartment {HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~Apartment(){if(SUCCEEDED(result))CoUninitialize();}} apartment;
        auto fail=[&](const wchar_t* message){std::lock_guard<std::mutex> lock(mutex);failure=message;};
        {
            Com<IMFAttributes> attributes;Com<IMFSourceReader> reader;Com<IMFMediaType> type,current;
            if(FAILED(MFCreateAttributes(attributes.put(),2))){fail(L"无法初始化视频解码器。");return;}attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,TRUE);
            if(FAILED(MFCreateSourceReaderFromURL(path.c_str(),attributes.get(),reader.put()))){fail(L"视频格式无法读取，请使用系统支持的 MP4 / WMV。");}
            else{
                reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE);
                if(FAILED(MFCreateMediaType(type.put()))){fail(L"无法初始化视频格式。");return;}type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
                if(FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type.get()))||FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,current.put()))){fail(L"系统无法解码这个视频。建议使用 H.264 MP4。");}
                else{
                    UINT w=0,h=0;MFGetAttributeSize(current.get(),MF_MT_FRAME_SIZE,&w,&h);
                    if(!w||!h||w>4096||h>4096){fail(L"视频尺寸无效或超过 4096 × 4096。");}
                    else{
                        UINT32 raw_stride=0;LONG stride=LONG(w*4);if(SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE,&raw_stride)))stride=LONG(raw_stride);
                        auto baseline=std::chrono::steady_clock::now();bool resumed=true;
                        for(;;){
                            {std::unique_lock<std::mutex> lock(mutex);if(!shown){resumed=true;wake.wait(lock,[&]{return stop||shown;});}if(stop)break;}
                            if(resumed){PROPVARIANT zero{};zero.vt=VT_I8;zero.hVal.QuadPart=0;reader->SetCurrentPosition(GUID_NULL,zero);baseline=std::chrono::steady_clock::now();resumed=false;}
                            Com<IMFSample> sample;DWORD flags=0;LONGLONG timestamp=0;
                            if(FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&timestamp,sample.put()))){fail(L"视频解码中断。");break;}
                            if(flags&MF_SOURCE_READERF_ENDOFSTREAM){resumed=true;continue;}
                            if(!sample)continue;
                            auto presentation=baseline+std::chrono::microseconds(timestamp/10);
                            {std::unique_lock<std::mutex> lock(mutex);wake.wait_until(lock,presentation,[&]{return stop||!shown;});if(stop)break;if(!shown)continue;}
                            Com<IMFMediaBuffer> buffer;if(FAILED(sample->ConvertToContiguousBuffer(buffer.put())))continue;
                            BYTE* data=nullptr;DWORD length=0;if(FAILED(buffer->Lock(&data,nullptr,&length)))continue;
                            if(length>=size_t(std::abs(stride))*h){
                                std::vector<BYTE> pixels(size_t(w)*h*4);
                                for(UINT y=0;y<h;++y){UINT from=stride<0?h-y-1:y;memcpy(pixels.data()+size_t(y)*w*4,data+size_t(from)*std::abs(stride),size_t(w)*4);}
                                std::lock_guard<std::mutex> lock(mutex);width=w;height=h;frame=std::move(pixels);
                            }
                            buffer->Unlock();
                        }
                    }
                }
            }
        }
    }
};
Background::Background(){runtime();impl_=std::make_unique<Impl>();}
Background::~Background()=default;
bool Background::load(const std::wstring& path,bool sound){
    auto replacement=std::make_unique<Impl>();
    if(video_file(path)){
        if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)return false;
        replacement->decoder=std::thread([state=replacement.get(),path]{state->decode(path);});
        if(sound){
            replacement->events=new AudioEvents(replacement->audio_state);
            if(SUCCEEDED(MFPCreateMediaPlayer(nullptr,FALSE,MFP_OPTION_FREE_THREADED_CALLBACK,replacement->events,nullptr,replacement->audio.put()))){
                Com<IMFPMediaItem> item;
                if(SUCCEEDED(replacement->audio->CreateMediaItemFromURL(path.c_str(),TRUE,0,item.put()))){
                    DWORD streams=0;item->GetNumberOfStreams(&streams);
                    for(DWORD index=0;index<streams;++index)item->SetStreamSelection(index,FALSE);
                    std::vector<DWORD> audio_streams;
                    for(DWORD index=0;index<streams;++index){item->SetStreamSelection(index,TRUE);BOOL has=FALSE,selected=FALSE;if(SUCCEEDED(item->HasAudio(&has,&selected))&&has&&selected)audio_streams.push_back(index);item->SetStreamSelection(index,FALSE);}
                    for(auto index:audio_streams)item->SetStreamSelection(index,TRUE);
                    if(!audio_streams.empty())replacement->audio_state->failure=replacement->audio->SetMediaItem(item.get());
                }
            }
        }
    }else{
        replacement->image.reset(Gdiplus::Image::FromFile(path.c_str(),FALSE));
        if(!replacement->image||replacement->image->GetLastStatus()!=Gdiplus::Ok)return false;
        if(replacement->image->GetWidth()>8192||replacement->image->GetHeight()>8192)return false;
        replacement->gif_count=replacement->image->GetFrameCount(&Gdiplus::FrameDimensionTime);
        if(replacement->gif_count>1){
            auto size=replacement->image->GetPropertyItemSize(PropertyTagFrameDelay);std::vector<BYTE> property(size);
            if(size&&replacement->image->GetPropertyItem(PropertyTagFrameDelay,size,(Gdiplus::PropertyItem*)property.data())==Gdiplus::Ok){
                auto* item=(Gdiplus::PropertyItem*)property.data();auto* delays=(UINT*)item->value;
                for(UINT i=0;i<std::min(replacement->gif_count,UINT(item->length/4));++i)replacement->gif_delays.push_back(std::max(20u,delays[i]*10));
            }
        }
    }
    impl_=std::move(replacement);return true;
}
void Background::visible(bool value){
    {std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->shown==value)return;impl_->shown=value;impl_->gif_start=GetTickCount64();}
    impl_->audio_state->active=value;
    if(impl_->audio){if(value){PROPVARIANT zero{};zero.vt=VT_I8;zero.hVal.QuadPart=0;impl_->audio->SetPosition(MFP_POSITIONTYPE_100NS,&zero);impl_->audio->Play();}else impl_->audio->Pause();}
    impl_->wake.notify_all();
}
void Background::paint(HDC dc,RECT bounds,bool contain){
    if(bounds.right<=bounds.left||bounds.bottom<=bounds.top)return;
    Gdiplus::Graphics graphics(dc);graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetClip(Gdiplus::Rect(bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top));
    auto draw=[&](Gdiplus::Image& image){
        float sx=float(bounds.right-bounds.left)/image.GetWidth(),sy=float(bounds.bottom-bounds.top)/image.GetHeight();
        float scale=contain?std::min(sx,sy):std::max(sx,sy);
        float w=image.GetWidth()*scale,h=image.GetHeight()*scale;
        float x=contain?bounds.left+(bounds.right-bounds.left-w)/2:bounds.right-w;
        float y=contain?bounds.top+(bounds.bottom-bounds.top-h)/2:bounds.bottom-h;
        graphics.DrawImage(&image,Gdiplus::RectF(x,y,w,h));
    };
    if(impl_->image){
        if(!impl_->gif_delays.empty()){
            ULONGLONG total=0;for(auto delay:impl_->gif_delays)total+=delay;
            auto time=(GetTickCount64()-impl_->gif_start)%total;UINT frame=0;
            while(frame+1<impl_->gif_delays.size()&&time>=impl_->gif_delays[frame])time-=impl_->gif_delays[frame++];
            impl_->image->SelectActiveFrame(&Gdiplus::FrameDimensionTime,frame);
        }
        int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        if(impl_->gif_count<=1&&width>0&&height>0){
            if(!impl_->cached||impl_->cached_width!=width||impl_->cached_height!=height||impl_->cached_contain!=contain){
                impl_->cached=std::make_unique<Gdiplus::Bitmap>(width,height,PixelFormat32bppPARGB);impl_->cached_width=width;impl_->cached_height=height;impl_->cached_contain=contain;
                Gdiplus::Graphics cache(impl_->cached.get());cache.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                float sx=float(width)/impl_->image->GetWidth(),sy=float(height)/impl_->image->GetHeight();float scale=contain?std::min(sx,sy):std::max(sx,sy);float w=impl_->image->GetWidth()*scale,h=impl_->image->GetHeight()*scale;
                cache.Clear(Gdiplus::Color(0,0,0,0));cache.DrawImage(impl_->image.get(),Gdiplus::RectF(contain?(width-w)/2:width-w,contain?(height-h)/2:height-h,w,h));
            }
            graphics.DrawImage(impl_->cached.get(),int(bounds.left),int(bounds.top),width,height);
        }else draw(*impl_->image);
    }else{
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if(!impl_->frame.empty()){Gdiplus::Bitmap frame(impl_->width,impl_->height,impl_->width*4,PixelFormat32bppRGB,impl_->frame.data());draw(frame);}
    }
}
std::wstring Background::error()const{std::lock_guard<std::mutex> lock(impl_->mutex);if(!impl_->failure.empty())return impl_->failure;return FAILED(impl_->audio_state->failure)?L"视频声音无法播放，请检查系统音频设备或更换文件。":L"";}
bool Background::animated()const{return impl_->decoder.joinable()||impl_->gif_count>1;}
void Background::mute(bool value){if(impl_->audio)impl_->audio->SetMute(value);}
bool Background::sound_playing()const{MFP_MEDIAPLAYER_STATE state=MFP_MEDIAPLAYER_STATE_EMPTY;if(impl_->audio)impl_->audio->GetState(&state);return state==MFP_MEDIAPLAYER_STATE_PLAYING;}
std::wstring background_path(const std::wstring& root,BackgroundKind kind){
    wchar_t path[32768]{};GetPrivateProfileStringW(section(kind),L"path",L"",path,32768,(root+L"\\settings.ini").c_str());
    auto value=std::filesystem::path(path);
    if(!value.empty()){if(value.is_relative())value=std::filesystem::path(root)/value;std::error_code error;if(std::filesystem::is_regular_file(value,error))return value.wstring();}
    return root+(kind==BackgroundKind::Translation?L"\\resources\\backgrounds\\moon-garden.png":L"\\resources\\backgrounds\\morning-ripple.png");
}
bool background_sound(const std::wstring& root,BackgroundKind kind){return GetPrivateProfileIntW(section(kind),L"sound",0,(root+L"\\settings.ini").c_str())!=0;}
int background_opacity(const std::wstring& root,BackgroundKind kind){return std::clamp((int)GetPrivateProfileIntW(section(kind),L"opacity",40,(root+L"\\settings.ini").c_str()),0,100);}
void paint_glass(HDC dc,RECT r,int opacity,int radius){
    runtime();if(r.right<=r.left||r.bottom<=r.top||opacity<=0)return;
    Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::SolidBrush brush(Gdiplus::Color(BYTE(std::clamp(opacity,0,100)*255/100),255,255,255));
    int d=std::max(1,std::min({radius*2,(int)(r.right-r.left),(int)(r.bottom-r.top)}));Gdiplus::GraphicsPath path;
    path.AddArc(r.left,r.top,d,d,180,90);path.AddArc(r.right-d,r.top,d,d,270,90);path.AddArc(r.right-d,r.bottom-d,d,d,0,90);path.AddArc(r.left,r.bottom-d,d,d,90,90);path.CloseFigure();g.FillPath(&brush,&path);
}
namespace {
HWND settings_windows[2]{};
struct Settings {
    HWND window=nullptr,owner=nullptr,label=nullptr,sound=nullptr,slider=nullptr,value=nullptr,hint=nullptr;
    HINSTANCE instance=nullptr;HFONT font=nullptr;HBRUSH paper=nullptr;int dpi=96,opacity=40;
    RECT preview_area{};std::wstring root,last_error;BackgroundKind kind;Background preview;
    int px(int n)const{return MulDiv(n,dpi,96);}
};
void set_value(Settings& s){SetWindowTextW(s.value,(L"文字区域白底："+std::to_wstring(s.opacity)+L"%    0% 透明 · 100% 不透明").c_str());}
void settings_layout(Settings& s){
    RECT r{};GetClientRect(s.window,&r);int margin=s.px(20),width=r.right-2*margin;
    s.preview_area={margin,s.px(48),r.right-margin,r.bottom-s.px(172)};
    MoveWindow(s.hint,margin,s.px(12),width,s.px(26),FALSE);
    MoveWindow(s.label,margin,r.bottom-s.px(160),width,s.px(22),FALSE);
    MoveWindow(s.value,margin,r.bottom-s.px(130),width,s.px(24),FALSE);
    MoveWindow(s.slider,margin,r.bottom-s.px(100),width,s.px(32),FALSE);
    MoveWindow(GetDlgItem(s.window,301),margin,r.bottom-s.px(52),s.px(154),s.px(32),FALSE);
    MoveWindow(GetDlgItem(s.window,302),margin+s.px(166),r.bottom-s.px(52),s.px(150),s.px(32),FALSE);
    MoveWindow(s.sound,margin+s.px(332),r.bottom-s.px(50),std::max(s.px(130),width-s.px(332)),s.px(28),FALSE);
    RedrawWindow(s.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void update(Settings& s,bool notify=true){
    s.last_error.clear();s.preview.load(background_path(s.root,s.kind),false);s.preview.visible(IsWindowVisible(s.window));
    KillTimer(s.window,1);if(IsWindowVisible(s.window)&&s.preview.animated())SetTimer(s.window,1,67,nullptr);
    SendMessageW(s.sound,BM_SETCHECK,background_sound(s.root,s.kind)?BST_CHECKED:BST_UNCHECKED,0);
    s.opacity=background_opacity(s.root,s.kind);SendMessageW(s.slider,TBM_SETPOS,TRUE,s.opacity);set_value(s);
    SetWindowTextW(s.label,(L"当前："+std::filesystem::path(background_path(s.root,s.kind)).filename().wstring()).c_str());
    InvalidateRect(s.window,nullptr,FALSE);if(notify&&s.owner)PostMessageW(s.owner,background_changed,(WPARAM)s.kind,0);
}
void settings_paint(Settings& s,HDC target){
    RECT r{};GetClientRect(s.window,&r);if(r.right<=0||r.bottom<=0)return;
    auto dc=CreateCompatibleDC(target);auto bmp=CreateCompatibleBitmap(target,r.right,r.bottom);auto old=SelectObject(dc,bmp);
    FillRect(dc,&r,s.paper);HBRUSH preview_paper=CreateSolidBrush(RGB(221,234,236));FillRect(dc,&s.preview_area,preview_paper);DeleteObject(preview_paper);
    s.preview.paint(dc,s.preview_area,true);
    // A compact sample demonstrates the panel opacity without cropping the artwork.
    auto sample=s.preview_area;sample.left+=s.px(14);sample.top=sample.bottom-s.px(60);sample.right=std::min(sample.right-s.px(14),sample.left+s.px(280));sample.bottom-=s.px(14);
    paint_glass(dc,sample,s.opacity,s.px(8));auto previous=SelectObject(dc,s.font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(29,63,76));sample.left+=s.px(12);
    DrawTextW(dc,s.kind==BackgroundKind::Translation?L"示例译文 · 月色落在水面上":L"1 develop   2 development",-1,&sample,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);SelectObject(dc,previous);
    BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);
}
void choose(Settings& s,const std::wstring& source){
    auto ext=extension(source);
    if(ext!=L".png"&&ext!=L".jpg"&&ext!=L".jpeg"&&ext!=L".bmp"&&ext!=L".gif"&&!video_file(source)){MessageBoxW(s.window,L"请选择 PNG/JPG/BMP/GIF 图片或 MP4/WMV/MOV/AVI 视频。",L"背景格式",MB_OK);return;}
    Background test;if(!test.load(source,false)){MessageBoxW(s.window,L"文件无法读取或图片尺寸超过 8192。",L"背景",MB_OK);return;}
    auto relative=std::filesystem::path(L"backgrounds/custom")/((s.kind==BackgroundKind::Translation?L"translation-":L"candidates-")+std::to_wstring(GetTickCount64())+ext);
    auto destination=std::filesystem::path(s.root)/relative;
    std::error_code error;std::filesystem::create_directories(destination.parent_path(),error);
    if(!CopyFileW(source.c_str(),destination.c_str(),FALSE)){MessageBoxW(s.window,L"无法保存自定义背景，请检查文件权限。",L"背景",MB_OK);return;}
    WritePrivateProfileStringW(section(s.kind),L"path",relative.wstring().c_str(),(s.root+L"\\settings.ini").c_str());update(s);
}
LRESULT CALLBACK settings_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto* s=(Settings*)GetWindowLongPtrW(h,GWLP_USERDATA);
    if(m==WM_NCCREATE){s=(Settings*)((CREATESTRUCTW*)l)->lpCreateParams;s->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)s);}
    if(!s)return DefWindowProcW(h,m,w,l);
    if(m==WM_CREATE){
        s->dpi=(int)GetDpiForWindow(h);s->paper=CreateSolidBrush(RGB(244,249,249));s->font=CreateFontW(-s->px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        auto create=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD extra=0){auto child=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|extra,0,0,0,0,h,(HMENU)(INT_PTR)id,s->instance,nullptr);SendMessageW(child,WM_SETFONT,(WPARAM)s->font,TRUE);return child;};
        s->hint=create(L"STATIC",L"拖入图片 / GIF / 视频 · 完整预览，实际窗口按比例铺满",0);
        s->label=create(L"STATIC",L"",0);s->value=create(L"STATIC",L"",305);
        create(L"BUTTON",L"选择图片 / 视频",301,WS_TABSTOP);create(L"BUTTON",L"恢复默认背景",302,WS_TABSTOP);
        s->sound=create(L"BUTTON",L"播放视频声音",303,BS_AUTOCHECKBOX|WS_TABSTOP);
        s->slider=create(TRACKBAR_CLASSW,L"",304,WS_TABSTOP|TBS_HORZ|TBS_AUTOTICKS);
        SendMessageW(s->slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));SendMessageW(s->slider,TBM_SETTICFREQ,10,0);SendMessageW(s->slider,TBM_SETPAGESIZE,0,10);
        DragAcceptFiles(h,TRUE);settings_layout(*s);update(*s,false);return 0;
    }
    if(m==WM_COMMAND){auto id=LOWORD(w);
        if(id==301){wchar_t path[32768]{};OPENFILENAMEW file{};file.lStructSize=sizeof(file);file.hwndOwner=h;file.lpstrFilter=L"背景媒体\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.mp4;*.wmv;*.m4v;*.mov;*.avi\0所有文件\0*.*\0";file.lpstrFile=path;file.nMaxFile=32768;file.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&file))choose(*s,path);}
        if(id==302){WritePrivateProfileStringW(section(s->kind),L"path",nullptr,(s->root+L"\\settings.ini").c_str());update(*s);}
        if(id==303){WritePrivateProfileStringW(section(s->kind),L"sound",SendMessageW(s->sound,BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0",(s->root+L"\\settings.ini").c_str());if(s->owner)PostMessageW(s->owner,background_changed,(WPARAM)s->kind,0);}
        return 0;
    }
    if(m==WM_HSCROLL&&(HWND)l==s->slider){
        int opacity=(int)SendMessageW(s->slider,TBM_GETPOS,0,0);if(opacity!=s->opacity){s->opacity=opacity;set_value(*s);WritePrivateProfileStringW(section(s->kind),L"opacity",std::to_wstring(opacity).c_str(),(s->root+L"\\settings.ini").c_str());InvalidateRect(h,&s->preview_area,FALSE);if(s->owner)PostMessageW(s->owner,background_changed,(WPARAM)s->kind,1);}return 0;
    }
    if(m==WM_SIZE){settings_layout(*s);bool visible=w!=SIZE_MINIMIZED&&IsWindowVisible(h);s->preview.visible(visible);KillTimer(h,1);if(visible&&s->preview.animated())SetTimer(h,1,67,nullptr);return 0;}
    if(m==WM_SHOWWINDOW){s->preview.visible(w!=0);KillTimer(h,1);if(w&&s->preview.animated())SetTimer(h,1,67,nullptr);}
    if(m==WM_GETMINMAXINFO){((MINMAXINFO*)l)->ptMinTrackSize={s->px(580),s->px(500)};return 0;}
    if(m==WM_DPICHANGED){s->dpi=HIWORD(w);auto font=CreateFontW(-s->px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");for(auto child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,(WPARAM)font,FALSE);DeleteObject(s->font);s->font=font;auto* r=(RECT*)l;SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);settings_layout(*s);return 0;}
    if(m==WM_DROPFILES){wchar_t path[32768]{};DragQueryFileW((HDROP)w,0,path,32768);DragFinish((HDROP)w);choose(*s,path);return 0;}
    if(m==WM_TIMER){InvalidateRect(h,&s->preview_area,FALSE);auto error=s->preview.error();if(!error.empty()&&error!=s->last_error){s->last_error=error;SetWindowTextW(s->label,error.c_str());}return 0;}
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLORBTN){SetBkMode((HDC)w,TRANSPARENT);SetTextColor((HDC)w,RGB(38,68,84));return (LRESULT)s->paper;}
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PAINTSTRUCT paint{};auto dc=m==WM_PAINT?BeginPaint(h,&paint):(HDC)w;settings_paint(*s,dc);if(m==WM_PAINT)EndPaint(h,&paint);return 0;}
    if(m==WM_CLOSE){DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){KillTimer(h,1);s->preview.visible(false);settings_windows[(int)s->kind]=nullptr;return 0;}
    if(m==WM_NCDESTROY){DeleteObject(s->font);DeleteObject(s->paper);SetWindowLongPtrW(h,GWLP_USERDATA,0);delete s;return DefWindowProcW(h,m,w,l);}
    return DefWindowProcW(h,m,w,l);
}
}
HWND show_background_settings(HINSTANCE instance,HWND owner,const std::wstring& root,BackgroundKind kind,bool visible){
    if(settings_windows[(int)kind]){if(visible){ShowWindow(settings_windows[(int)kind],SW_RESTORE);SetForegroundWindow(settings_windows[(int)kind]);}return settings_windows[(int)kind];}
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW cls{};cls.lpfnWndProc=settings_proc;cls.hInstance=instance;cls.lpszClassName=L"EnglishAssistant.Background";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,LR_SHARED);RegisterClassW(&cls);
    auto* s=new Settings;s->instance=instance;s->owner=owner;s->root=root;s->kind=kind;
    int dpi=(int)GetDpiForSystem();auto h=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,kind==BackgroundKind::Translation?L"EnglishAssistant — 翻译框背景":L"EnglishAssistant — 英文选词框背景",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(660,dpi,96),MulDiv(640,dpi,96),owner,nullptr,instance,s);
    settings_windows[(int)kind]=h;if(h&&visible)ShowWindow(h,SW_SHOW);return h;
}
void close_background_settings(){for(auto h:settings_windows)if(h)DestroyWindow(h);}
}
