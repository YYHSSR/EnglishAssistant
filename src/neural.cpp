#include "neural.hpp"
#include "dictionary.hpp"
#include <windows.h>
#include <filesystem>
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
namespace ea {
namespace {
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle()=default;Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;};
struct ProcessGuard {HANDLE value;~ProcessGuard(){if(WaitForSingleObject(value,0)==WAIT_TIMEOUT){TerminateProcess(value,1);WaitForSingleObject(value,1000);}}};
std::wstring quote(std::wstring_view value){std::wstring out=L"\"";size_t slashes=0;for(auto c:value){if(c==L'\\'){++slashes;continue;}out.append(slashes*(c==L'"'?2:1),L'\\');if(c==L'"')out+=L'\\';out+=c;slashes=0;}out.append(slashes*2,L'\\');return out+L'"';}
std::wstring ps_literal(std::wstring_view value){std::wstring out=L"'";for(auto c:value){out+=c;if(c==L'\'')out+=c;}return out+L'\'';}
bool ensure_runtime(const std::wstring& root,const std::atomic<bool>& cancelled){
    static std::mutex unpack_mutex;std::lock_guard<std::mutex> lock(unpack_mutex);
    auto runtime=std::filesystem::path(root)/L"runtime"/L"python";
    if(std::filesystem::exists(runtime/L"ready.txt"))return true;
    auto archive=std::filesystem::path(root)/L"runtime"/L"windows-runtime.zip";
    if(!std::filesystem::exists(archive))return false;
    auto stage=std::filesystem::path(root)/L"runtime"/(L"unpack-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::wstring code=L"Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::ExtractToDirectory("+ps_literal(archive.wstring())+L","+ps_literal(stage.wstring())+L")";
    wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
    std::wstring command=quote(std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe")+L" -NoLogo -NoProfile -NonInteractive -Command "+quote(code);
    STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&start,&process))return false;
    Handle child,thread;child.value=process.hProcess;thread.value=process.hThread;
    while(WaitForSingleObject(child.value,50)==WAIT_TIMEOUT){if(cancelled){TerminateProcess(child.value,1);return false;}}
    DWORD result=1;GetExitCodeProcess(child.value,&result);
    if(result!=0||std::filesystem::exists(runtime))return false;
    std::error_code error;std::filesystem::rename(stage,runtime,error);return !error;
}
}
NeuralReply neural_translate(const std::wstring& root,const std::wstring& text,const std::atomic<bool>& cancelled){
    NeuralReply reply;
    try{
        if(cancelled){reply.error=L"翻译已取消";return reply;}
        if(!ensure_runtime(root,cancelled)){reply.error=L"本地翻译运行库缺失或解压失败，请保留 runtime/windows-runtime.zip。";return reply;}
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        Handle input_read,input_write,output_read,output_write,error_sink;
        if(!CreatePipe(&input_read.value,&input_write.value,&security,0)||!CreatePipe(&output_read.value,&output_write.value,&security,0)){reply.error=L"无法创建本地翻译进程通道。";return reply;}
        SetHandleInformation(input_write.value,HANDLE_FLAG_INHERIT,0);SetHandleInformation(output_read.value,HANDLE_FLAG_INHERIT,0);
        error_sink.value=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_WRITE|FILE_SHARE_READ,&security,OPEN_EXISTING,0,nullptr);
        STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESTDHANDLES;start.hStdInput=input_read.value;start.hStdOutput=output_write.value;start.hStdError=error_sink.value;
        auto python=std::filesystem::path(root)/L"runtime/python/python.exe";
        auto script=std::filesystem::path(root)/L"translation/model.py";
        auto command=quote(python.wstring())+L" -I "+quote(script.wstring());PROCESS_INFORMATION process{};
        if(!CreateProcessW(python.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,root.c_str(),&start,&process)){reply.error=L"无法启动本地翻译运行库。";return reply;}
        Handle child,thread;child.value=process.hProcess;thread.value=process.hThread;
        ProcessGuard guard{child.value};
        CloseHandle(input_read.value);input_read.value=nullptr;CloseHandle(output_write.value);output_write.value=nullptr;
        std::string request="translate\t";const char* digits="0123456789abcdef";
        for(unsigned char c:utf8(text)){request+=digits[c>>4];request+=digits[c&15];}request+='\n';
        DWORD written=0;if(!WriteFile(input_write.value,request.data(),(DWORD)request.size(),&written,nullptr)||written!=request.size()){TerminateProcess(child.value,1);reply.error=L"无法发送本地翻译请求。";return reply;}
        CloseHandle(input_write.value);input_write.value=nullptr;
        auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(3);std::string response;
        while(response.find('\n')==std::string::npos){
            if(cancelled||std::chrono::steady_clock::now()>deadline){TerminateProcess(child.value,1);reply.error=cancelled?L"翻译已取消":L"本地翻译超时，请分段重试。";return reply;}
            DWORD available=0;
            if(!PeekNamedPipe(output_read.value,nullptr,0,nullptr,&available,nullptr))break;
            if(available){char buffer[4096];DWORD got=0;if(!ReadFile(output_read.value,buffer,std::min(available,DWORD(sizeof(buffer))),&got,nullptr))break;response.append(buffer,got);if(response.size()>1024*1024)break;}
            else if(WaitForSingleObject(child.value,15)!=WAIT_TIMEOUT)break;
        }
        auto tab=response.find('\t'),end=response.find_first_of("\r\n");
        if(tab==std::string::npos||end==std::string::npos){reply.error=L"本地翻译模型启动失败，请检查 models 与 runtime 是否完整。";return reply;}
        auto hex=response.substr(tab+1,end-tab-1);std::string decoded;
        auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
        for(size_t at=0;at+1<hex.size();at+=2){int a=digit(hex[at]),b=digit(hex[at+1]);if(a<0||b<0){reply.error=L"本地翻译返回了无效数据。";return reply;}decoded+=char(16*a+b);}
        if(response.substr(0,tab)=="ok")reply.text=wide(decoded);else reply.error=wide(decoded);
        if(WaitForSingleObject(child.value,1000)==WAIT_TIMEOUT)TerminateProcess(child.value,1);
        return reply;
    }catch(const std::exception& error){reply.error=wide(error.what());return reply;}
}
}
