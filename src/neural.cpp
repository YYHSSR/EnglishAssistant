#include "neural.hpp"
#include "dictionary.hpp"
#include <windows.h>
#include <filesystem>
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
#include <vector>
namespace ea {
namespace {
struct Handle {HANDLE value=nullptr;void reset(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=nullptr;}~Handle(){reset();}Handle()=default;Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;};
std::wstring quote(std::wstring_view value){std::wstring out=L"\"";size_t slashes=0;for(auto c:value){if(c==L'\\'){++slashes;continue;}out.append(slashes*(c==L'"'?2:1),L'\\');if(c==L'"')out+=L'\\';out+=c;slashes=0;}out.append(slashes*2,L'\\');return out+L'"';}
std::wstring ps_literal(std::wstring_view value){std::wstring out=L"'";for(auto c:value){out+=c;if(c==L'\'')out+=c;}return out+L'\'';}
bool ensure_runtime(const std::wstring& root,const std::atomic<bool>& cancelled){
    static std::timed_mutex unpack_mutex;std::unique_lock<std::timed_mutex> lock(unpack_mutex,std::defer_lock);
    while(!lock.try_lock_for(std::chrono::milliseconds(50)))if(cancelled)return false;
    if(cancelled)return false;
    auto runtime=std::filesystem::path(root)/L"runtime"/L"python";
    if(std::filesystem::exists(runtime/L"ready.txt"))return true;
    auto archive=std::filesystem::path(root)/L"runtime"/L"windows-runtime.zip";
    if(!std::filesystem::exists(archive))return false;
    auto stage=std::filesystem::path(root)/L"runtime"/(L"unpack-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    auto discard_stage=[&]{
        std::error_code error;auto parent=std::filesystem::weakly_canonical(archive.parent_path(),error);if(error)return;
        auto target=std::filesystem::weakly_canonical(stage,error);
        if(!error&&target.parent_path()==parent&&stage.filename().wstring().rfind(L"unpack-",0)==0)std::filesystem::remove_all(stage,error);
    };
    std::wstring code=L"Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::ExtractToDirectory("+ps_literal(archive.wstring())+L","+ps_literal(stage.wstring())+L")";
    wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
    std::wstring command=quote(std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe")+L" -NoLogo -NoProfile -NonInteractive -Command "+quote(code);
    STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&start,&process))return false;
    Handle child,thread;child.value=process.hProcess;thread.value=process.hThread;
    while(WaitForSingleObject(child.value,50)==WAIT_TIMEOUT){if(cancelled){TerminateProcess(child.value,1);if(WaitForSingleObject(child.value,1000)==WAIT_OBJECT_0)discard_stage();return false;}}
    DWORD result=1;GetExitCodeProcess(child.value,&result);
    if(result!=0||cancelled||std::filesystem::exists(runtime)){discard_stage();return false;}
    std::error_code error;std::filesystem::rename(stage,runtime,error);if(error)discard_stage();return !error;
}
}
struct NeuralTranslator::Impl {
    std::wstring root;Handle child,input,output;
    explicit Impl(std::wstring path):root(std::move(path)){}
    ~Impl(){reset();}
    void reset(){if(child.value&&WaitForSingleObject(child.value,0)==WAIT_TIMEOUT){TerminateProcess(child.value,1);WaitForSingleObject(child.value,1000);}child.reset();input.reset();output.reset();}
    bool start(const std::atomic<bool>& cancelled,std::wstring& error){
        if(child.value&&WaitForSingleObject(child.value,0)==WAIT_TIMEOUT)return true;
        reset();if(!ensure_runtime(root,cancelled)){error=L"本地翻译运行库缺失或解压失败，请保留 runtime/windows-runtime.zip。";return false;}
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};Handle input_read,output_write,error_sink;
        if(!CreatePipe(&input_read.value,&input.value,&security,0)||!CreatePipe(&output.value,&output_write.value,&security,0)){error=L"无法创建本地翻译进程通道。";reset();return false;}
        SetHandleInformation(input.value,HANDLE_FLAG_INHERIT,0);SetHandleInformation(output.value,HANDLE_FLAG_INHERIT,0);
        error_sink.value=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_WRITE|FILE_SHARE_READ,&security,OPEN_EXISTING,0,nullptr);
        STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof(start);start.StartupInfo.dwFlags=STARTF_USESTDHANDLES;start.StartupInfo.hStdInput=input_read.value;start.StartupInfo.hStdOutput=output_write.value;start.StartupInfo.hStdError=error_sink.value;
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<BYTE> storage(bytes);start.lpAttributeList=(LPPROC_THREAD_ATTRIBUTE_LIST)storage.data();
        if(!InitializeProcThreadAttributeList(start.lpAttributeList,1,0,&bytes)){error=L"无法初始化翻译进程。";reset();return false;}
        HANDLE inherited[]={input_read.value,output_write.value,error_sink.value};
        bool attributes=UpdateProcThreadAttribute(start.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)!=0;
        auto python=std::filesystem::path(root)/L"runtime/python/python.exe",script=std::filesystem::path(root)/L"translation/model.py";
        auto command=quote(python.wstring())+L" -I "+quote(script.wstring());PROCESS_INFORMATION process{};
        bool created=attributes&&CreateProcessW(python.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,root.c_str(),&start.StartupInfo,&process);
        DeleteProcThreadAttributeList(start.lpAttributeList);if(!created){error=L"无法启动本地翻译运行库。";reset();return false;}
        child.value=process.hProcess;CloseHandle(process.hThread);return true;
    }
};
NeuralTranslator::NeuralTranslator(std::wstring root):impl_(std::make_unique<Impl>(std::move(root))){}
NeuralTranslator::~NeuralTranslator()=default;
void NeuralTranslator::reset(){impl_->reset();}
NeuralReply NeuralTranslator::translate(const std::wstring& text,const std::atomic<bool>& cancelled,TranslationDirection direction){
    NeuralReply reply;
    try{
        if(cancelled){reply.error=L"翻译已取消";return reply;}
        if(text.empty()||text.size()>8000){reply.error=L"请输入 1–8000 个字符。";return reply;}
        if(!impl_->start(cancelled,reply.error))return reply;
        std::string request=direction==TranslationDirection::ChineseToEnglish?"zh-en\t":"en-zh\t";const char* digits="0123456789abcdef";
        for(unsigned char c:utf8(text)){request+=digits[c>>4];request+=digits[c&15];}request+='\n';
        DWORD written=0;if(!WriteFile(impl_->input.value,request.data(),(DWORD)request.size(),&written,nullptr)||written!=request.size()){impl_->reset();reply.error=L"无法发送本地翻译请求。";return reply;}
        auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(3);std::string response;
        while(response.find('\n')==std::string::npos){
            if(cancelled||std::chrono::steady_clock::now()>deadline){impl_->reset();reply.error=cancelled?L"翻译已取消":L"本地翻译超时，请分段重试。";return reply;}
            DWORD available=0;
            if(!PeekNamedPipe(impl_->output.value,nullptr,0,nullptr,&available,nullptr))break;
            if(available){char buffer[4096];DWORD got=0;if(!ReadFile(impl_->output.value,buffer,std::min(available,DWORD(sizeof(buffer))),&got,nullptr))break;response.append(buffer,got);if(response.size()>1024*1024)break;}
            else if(WaitForSingleObject(impl_->child.value,15)!=WAIT_TIMEOUT)break;
        }
        auto tab=response.find('\t'),end=response.find_first_of("\r\n");
        if(tab==std::string::npos||end==std::string::npos){impl_->reset();reply.error=L"本地翻译模型启动失败，请检查 models 与 runtime 是否完整。";return reply;}
        auto hex=response.substr(tab+1,end-tab-1);std::string decoded;
        auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
        if(hex.size()%2){impl_->reset();reply.error=L"本地翻译返回了无效数据。";return reply;}
        for(size_t at=0;at+1<hex.size();at+=2){int a=digit(hex[at]),b=digit(hex[at+1]);if(a<0||b<0){impl_->reset();reply.error=L"本地翻译返回了无效数据。";return reply;}decoded+=char(16*a+b);}
        if(response.substr(0,tab)=="ok"){reply.text=wide(decoded);if(reply.text.empty())reply.error=L"本地模型没有生成译文。";}else reply.error=wide(decoded);
        return reply;
    }catch(const std::exception& error){impl_->reset();reply.error=wide(error.what());return reply;}
}
NeuralReply neural_translate(const std::wstring& root,const std::wstring& text,const std::atomic<bool>& cancelled,TranslationDirection direction){NeuralTranslator session(root);return session.translate(text,cancelled,direction);}
}
