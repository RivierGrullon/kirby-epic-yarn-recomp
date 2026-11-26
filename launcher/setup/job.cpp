// SPDX-License-Identifier: GPL-3.0-only
#include "job.h"
#include <array>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <stdexcept>
#ifndef _WIN32
#include <fcntl.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <thread>
#include <vector>

#ifndef _WIN32
extern char** environ;
#endif
namespace Launcher::Setup {
namespace {
#ifndef _WIN32
void Nonblocking(int fd) {
    if (fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)<0) throw std::runtime_error("Cannot configure setup pipe");
}
#endif
}
Job::~Job() {
    Cancel();
    while (view_.Busy()) { Poll();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    Close();
}
bool Job::Start(const std::filesystem::path& worker,const std::filesystem::path& source,
                const std::filesystem::path& workspace,const std::string& python,Operation operation) {
    if (view_.Busy()) return false;
    Close();view_=View{};sequence_=1;exited_=outputEof_=errorEof_=cancelled_=termSent_=killSent_=false;
    pending_.clear();buffer_.clear();stderr_.clear();abort_.clear();terminal_.clear();terminalError_.clear();result_.reset();
    installation_.reset();view_.operation=operation;operation_=operation==Operation::Install ? "install" : "inspect";
    #ifndef _WIN32
    int input[2]{-1,-1},output[2]{-1,-1},error[2]{-1,-1};
    struct Pipes {
        int *input,*output,*error;
        ~Pipes() { for (auto pair : {input,output,error}) for (unsigned i=0;i<2;++i) if (pair[i]>=0) close(pair[i]); }
    } pipes{input,output,error};
    #endif
    try {
        view_.source=std::filesystem::absolute(source);
        if (Utf8(view_.source).size()>8192 || Utf8(view_.source).find('\0')!=std::string::npos)
            throw std::runtime_error("Invalid selected source path");
#ifdef _WIN32
        identifier_="launcher-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(Clock::now().time_since_epoch().count());
#else
        identifier_="launcher-"+std::to_string(getpid())+"-"+std::to_string(Clock::now().time_since_epoch().count());
#endif
        workspace_=std::filesystem::absolute(workspace);
        pending_=operation==Operation::Install ? InstallRequest(identifier_,view_.source) : InspectRequest(identifier_,view_.source);
        #ifndef _WIN32
#ifdef __APPLE__
        if (socketpair(AF_UNIX,SOCK_STREAM,0,input) || pipe(output) || pipe(error))
            throw std::runtime_error("Cannot open setup process pipes");
        for (auto pair : {input,output,error}) for (int fd : {pair[0],pair[1]})
            if (fcntl(fd,F_SETFD,FD_CLOEXEC)<0) throw std::runtime_error("Cannot protect setup pipe");
        const int noSignal=1;
        if (setsockopt(input[0],SOL_SOCKET,SO_NOSIGPIPE,&noSignal,sizeof(noSignal)))
            throw std::runtime_error("Cannot protect setup socket from SIGPIPE");
#else
        if (socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,input) || pipe2(output,O_CLOEXEC) || pipe2(error,O_CLOEXEC))
            throw std::runtime_error("Cannot open setup process pipes");
#endif
        // Desktop launchers may start with one of the standard descriptors closed.
        // Keep our close actions separate from the child's three dup2 targets.
        for (auto pair : {input,output,error}) for (unsigned i=0;i<2;++i) if (pair[i]<3) {
            const auto replacement=fcntl(pair[i],F_DUPFD_CLOEXEC,3);
            if (replacement<0) throw std::runtime_error("Cannot reserve setup pipe descriptor");
            close(pair[i]);pair[i]=replacement;
        }
        Nonblocking(input[0]);Nonblocking(output[0]);Nonblocking(error[0]);
        std::vector<std::string> arguments;
        if (!python.empty()) arguments={python,"-X","utf8","-u"};
        arguments.insert(arguments.end(),{std::filesystem::absolute(worker).string(),"--workspace",
                                         std::filesystem::absolute(workspace).string()});
        std::vector<char*> argv;for (auto& argument : arguments) argv.push_back(argument.data());argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,input[1],STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions,output[1],STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions,error[1],STDERR_FILENO);
        for (auto pair : {input,output,error}) for (unsigned i=0;i<2;++i) posix_spawn_file_actions_addclose(&actions,pair[i]);
        posix_spawnattr_t attributes;posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attributes,0);
        const int code=posix_spawnp(&pid_,argv.front(),&actions,&attributes,argv.data(),environ);
        posix_spawnattr_destroy(&attributes);posix_spawn_file_actions_destroy(&actions);
        if (code) { pid_=-1;throw std::runtime_error("Cannot start setup worker: "+std::string(std::strerror(code))); }
        input_=input[0];output_=output[0];error_=error[0];input[0]=output[0]=error[0]=-1;
        #else
        StartWindows(worker,workspace,python);
        #endif
        view_.status=Status::Running;view_.stage="inspect";view_.message="Checking selected game data...";
        view_.startedAt=view_.stageStartedAt=Clock::now();
        return true;
    } catch (const std::exception& errorMessage) {
        Close();view_.status=Status::Failed;view_.message=errorMessage.what();return false;
    }
}
#ifdef _WIN32
namespace {
struct Handle {
    HANDLE value=nullptr;
    ~Handle() { if (value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::wstring Quote(const std::wstring& value) {
    std::wstring result=L"\"";size_t slashes=0;
    for (const auto character : value) {
        if (character==L'\\') { ++slashes;continue; }
        result.append(character==L'"' ? slashes*2+1 : slashes,L'\\');
        slashes=0;result+=character;
    }
    result.append(slashes*2,L'\\');result+=L'"';return result;
}
}
void Job::StartWindows(const std::filesystem::path& worker,const std::filesystem::path& workspace,const std::string& python) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle childInput,childOutput,childError;
    const auto name=L"\\\\.\\pipe\\kirby-setup-"+FromUtf8(identifier_).wstring();
    input_=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_OUTBOUND|FILE_FLAG_FIRST_PIPE_INSTANCE,
                           PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_NOWAIT,1,65536,65536,0,nullptr);
    if (input_==INVALID_HANDLE_VALUE) { input_=nullptr;throw std::runtime_error("Cannot create setup input pipe"); }
    childInput.value=CreateFileW(name.c_str(),GENERIC_READ,0,&security,OPEN_EXISTING,0,nullptr);
    if (childInput.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open setup input pipe");
    if (!ConnectNamedPipe(input_,nullptr) && GetLastError()!=ERROR_PIPE_CONNECTED)
        throw std::runtime_error("Cannot connect setup input pipe");
    if (!CreatePipe(&output_,&childOutput.value,&security,65536) ||
        !CreatePipe(&error_,&childError.value,&security,65536) ||
        !SetHandleInformation(output_,HANDLE_FLAG_INHERIT,0) || !SetHandleInformation(error_,HANDLE_FLAG_INHERIT,0))
        throw std::runtime_error("Cannot create setup output pipes");
    processGroup_=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!processGroup_ || !SetInformationJobObject(processGroup_,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
        throw std::runtime_error("Cannot create setup process group");
    // Restrict handle inheritance to the worker's three streams. Starting suspended
    // prevents it from spawning descendants before the Job Object owns the tree.
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<unsigned char> storage(bytes);
    auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes,1,0,&bytes))
        throw std::runtime_error("Cannot prepare setup process attributes");
    struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST value;~Attributes(){DeleteProcThreadAttributeList(value);} } cleanup{attributes};
    HANDLE inherited[]{childInput.value,childOutput.value,childError.value};
    if (!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))
        throw std::runtime_error("Cannot restrict setup pipe inheritance");
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
    startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=childInput.value;startup.StartupInfo.hStdOutput=childOutput.value;
    startup.StartupInfo.hStdError=childError.value;
    auto command=(python.empty() ? std::wstring{} : Quote(FromUtf8(python).wstring())+L" -X utf8 -u ")+
                 Quote(std::filesystem::absolute(worker).wstring())+L" --workspace "+
                 Quote(std::filesystem::absolute(workspace).wstring());
    PROCESS_INFORMATION information{};
    if (!CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,
                        EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,
                        nullptr,nullptr,&startup.StartupInfo,&information))
        throw std::runtime_error("Cannot start setup worker (Windows error "+std::to_string(GetLastError())+")");
    process_=information.hProcess;Handle thread{information.hThread};
    if (!AssignProcessToJobObject(processGroup_,process_) || ResumeThread(thread.value)==static_cast<DWORD>(-1)) {
        TerminateProcess(process_,1);WaitForSingleObject(process_,INFINITE);
        throw std::runtime_error("Cannot supervise setup process tree");
    }
}
#endif
void Job::Cancel() {
    if (!view_.Busy() || cancelled_) return;
    cancelled_=true;cancelAt_=Clock::now();pending_+=CancelRequest(identifier_);
    view_.status=Status::Cancelling;view_.message="Cancelling setup...";view_.result.reset();view_.installation.reset();
}
void Job::Abort(std::string message) {
    if (abort_.empty()) abort_=message.substr(0,1024);
    Cancel();view_.message="Stopping failed setup...";
}
void Job::Consume(std::string_view line) {
    if (!abort_.empty()) return;
    try {
        if (!terminal_.empty()) throw std::runtime_error("Setup worker sent data after its final event");
        auto event=ParseEvent(line,identifier_,sequence_++);
        if (event.stage=="request") return;
        if (view_.stage!=event.stage) view_.stageStartedAt=Clock::now();
        view_.stage=event.stage;view_.completed=event.completed;view_.total=event.total;view_.unit=event.unit;
        view_.detail=event.detail;
        if (!event.logPath.empty()) view_.logPath=std::move(event.logPath);
        if (event.result) {
            if (view_.operation!=Operation::Inspect) throw std::runtime_error("Unexpected inspection completion during installation");
            if (std::filesystem::weakly_canonical(event.result->source)!=std::filesystem::weakly_canonical(view_.source))
                throw std::runtime_error("Setup result belongs to another selected source");
            result_=std::move(event.result);
        }
        if (event.installation) {
            if (view_.operation!=Operation::Install ||
                std::filesystem::weakly_canonical(event.installation->source)!=std::filesystem::weakly_canonical(view_.source) ||
                event.installation->executable.lexically_normal()!=
                    (workspace_/"installations"/event.installation->identifier/GameExecutable).lexically_normal())
                throw std::runtime_error("Installation result belongs to another source or executable store");
            installation_=std::move(event.installation);
        }
        if (event.stage==operation_ && (event.state=="complete" || event.state=="failed" || event.state=="cancelled")) {
            terminal_=event.state;terminalError_=std::move(event.error);
        }
    } catch (const std::exception& errorMessage) { Abort("Invalid setup response: "+std::string(errorMessage.what())); }
}
void Job::Drain(Pipe fd,bool& eof,bool protocol) {
    if (eof) return;
    std::array<char,4096> data;
    for (unsigned reads=0;reads<(protocol ? 16u : 4u);++reads) {
#ifdef _WIN32
        DWORD available=0,count=0;
        if (!PeekNamedPipe(fd,nullptr,0,nullptr,&available,nullptr)) {
            eof=true;if (GetLastError()!=ERROR_BROKEN_PIPE) Abort("Cannot read setup process output");return;
        }
        if (!available) return;
        if (!ReadFile(fd,data.data(),std::min<DWORD>(available,data.size()),&count,nullptr)) {
            eof=true;if (GetLastError()!=ERROR_BROKEN_PIPE) Abort("Cannot read setup process output");return;
        }
        const auto size=static_cast<int>(count);
#else
        const auto size=read(fd,data.data(),data.size());
#endif
        if (size>0) {
            if (!protocol) {
                stderr_.append(data.data(),size);
                if (stderr_.size()>8192) stderr_.erase(0,stderr_.size()-8192);
                continue;
            }
            buffer_.append(data.data(),size);
            for (auto end=buffer_.find('\n');end!=std::string::npos;end=buffer_.find('\n')) {
                Consume(std::string_view(buffer_).substr(0,end));buffer_.erase(0,end+1);
            }
            if (buffer_.size()>65536) { Abort("Setup response line exceeds 64 KiB");buffer_.clear(); }
        } else if (!size) { eof=true;return; }
        else if (errno==EINTR) continue;
        else if (errno==EAGAIN || errno==EWOULDBLOCK) return;
        else { eof=true;Abort("Cannot read setup process output");return; }
    }
}
void Job::Poll() {
    if (!view_.Busy()) return;
    if (!exited_ && !pending_.empty()) {
#ifdef _WIN32
        DWORD count=0;
        const bool sent=WriteFile(input_,pending_.data(),static_cast<DWORD>(pending_.size()),&count,nullptr);
        const auto written=sent ? static_cast<int>(count) : -1;
        if (!sent) errno=GetLastError()==ERROR_NO_DATA ? EAGAIN : EPIPE;
#else
#ifdef __APPLE__
        const auto written=send(input_,pending_.data(),pending_.size(),MSG_DONTWAIT);
#else
        const auto written=send(input_,pending_.data(),pending_.size(),MSG_NOSIGNAL|MSG_DONTWAIT);
#endif
#endif
        if (written>0) pending_.erase(0,written);
        else if (written<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) {
            pending_.clear();if (!cancelled_) Abort("Cannot send setup request: "+std::string(std::strerror(errno)));
        }
    }
    Drain(output_,outputEof_,true);Drain(error_,errorEof_,false);
    if (!exited_) {
#ifdef _WIN32
        const auto waited=WaitForSingleObject(process_,0);
        if (waited==WAIT_OBJECT_0) {
            DWORD result=1;GetExitCodeProcess(process_,&result);
            exited_=true;exitAt_=Clock::now();exitCode_=static_cast<int>(result);
        } else if (waited==WAIT_FAILED) {
            exited_=true;exitAt_=Clock::now();exitCode_=1;Abort("Cannot collect setup worker exit status");
        }
#else
        int result=0;const auto waited=waitpid(pid_,&result,WNOHANG);
        if (waited==pid_) {
            exited_=true;exitAt_=Clock::now();
            exitCode_=WIFEXITED(result) ? WEXITSTATUS(result) : 128+(WIFSIGNALED(result) ? WTERMSIG(result) : 0);
        } else if (waited<0 && errno!=EINTR) { exited_=true;exitAt_=Clock::now();exitCode_=1;Abort("Cannot collect setup worker exit status"); }
#endif
    }
    if (exited_ && outputEof_ && errorEof_) { Finish();return; }
    if (exited_ && Clock::now()-exitAt_>std::chrono::milliseconds(500)) Abort("Setup worker exited with open output pipes");
    if (cancelled_) {
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-cancelAt_).count();
        #ifndef _WIN32
        if (!termSent_ && elapsed>=static_cast<int64_t>(graceMs_)) { kill(-pid_,SIGTERM);termSent_=true; }
        if (!killSent_ && elapsed>=static_cast<int64_t>(graceMs_+3000)) { kill(-pid_,SIGKILL);killSent_=true; }
        #else
        // The worker gets the same protocol grace period as on POSIX. A Job Object
        // kills all its descendants, including children that inherited output pipes.
        if (!killSent_ && elapsed>=static_cast<int64_t>(graceMs_)) {
            TerminateJobObject(processGroup_,2);termSent_=killSent_=true;
        }
        #endif
        if (exited_ && killSent_) { outputEof_=errorEof_=true;Finish(); }
    }
}
void Job::Finish() {
    if (!abort_.empty()) { view_.status=Status::Failed;view_.message=abort_; }
    else if (terminal_=="complete" && exitCode_==0 && installation_ && installation_->committed && buffer_.empty()) {
        view_.status=Status::Ready;view_.message="Verified native installation selected";
        view_.installation=std::move(installation_);
    }
    else if (cancelled_ || (terminal_=="cancelled" && exitCode_==2)) {
        view_.status=Status::Cancelled;view_.message="Setup cancelled. Previous native build and game data kept.";
    } else if (terminal_=="complete" && exitCode_==0 && result_ && buffer_.empty()) {
        view_.status=Status::Ready;view_.message="RK5E01 Rev 0 game data verified";view_.result=std::move(result_);
    } else {
        view_.status=Status::Failed;
        view_.message=terminal_=="failed" && exitCode_==1 ? terminalError_ :
            "Setup worker exited without valid completion (code "+std::to_string(exitCode_)+")";
        if (!stderr_.empty()) view_.message+="\n"+stderr_.substr(0,2048);
    }
    if (view_.status!=Status::Ready) { view_.result.reset();view_.installation.reset(); }
    Close();
}
void Job::Close() {
#ifdef _WIN32
    for (auto* handle : {&input_,&output_,&error_,&process_,&processGroup_})
        if (*handle) { CloseHandle(*handle);*handle=nullptr; }
#else
    for (int* fd : {&input_,&output_,&error_}) if (*fd>=0) { close(*fd);*fd=-1; }
    pid_=-1;
#endif
}
} // namespace Launcher::Setup
