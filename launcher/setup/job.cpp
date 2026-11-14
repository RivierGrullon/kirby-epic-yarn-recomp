// SPDX-License-Identifier: GPL-3.0-only
#include "job.h"
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;
namespace Launcher::Setup {
namespace {
void Nonblocking(int fd) {
    if (fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)<0) throw std::runtime_error("Cannot configure setup pipe");
}
}
Job::~Job() {
    Cancel();
    while (view_.Busy()) { Poll();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    Close();
}
bool Job::Start(const std::filesystem::path& worker,const std::filesystem::path& source,
                const std::filesystem::path& workspace,const std::string& python) {
    if (view_.Busy()) return false;
    Close();view_=View{};sequence_=1;exited_=outputEof_=errorEof_=cancelled_=termSent_=killSent_=false;
    pending_.clear();buffer_.clear();stderr_.clear();abort_.clear();terminal_.clear();terminalError_.clear();result_.reset();
    int input[2]{-1,-1},output[2]{-1,-1},error[2]{-1,-1};
    struct Pipes {
        int *input,*output,*error;
        ~Pipes() { for (auto pair : {input,output,error}) for (unsigned i=0;i<2;++i) if (pair[i]>=0) close(pair[i]); }
    } pipes{input,output,error};
    try {
        view_.source=std::filesystem::absolute(source);
        if (view_.source.string().size()>8192 || view_.source.string().find('\0')!=std::string::npos)
            throw std::runtime_error("Invalid selected source path");
        identifier_="launcher-"+std::to_string(getpid())+"-"+std::to_string(Clock::now().time_since_epoch().count());
        pending_=InspectRequest(identifier_,view_.source);
        if (socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,input) || pipe2(output,O_CLOEXEC) || pipe2(error,O_CLOEXEC))
            throw std::runtime_error("Cannot open setup process pipes");
        // Desktop launchers may start with one of the standard descriptors closed.
        // Keep our close actions separate from the child's three dup2 targets.
        for (auto pair : {input,output,error}) for (unsigned i=0;i<2;++i) if (pair[i]<3) {
            const auto replacement=fcntl(pair[i],F_DUPFD_CLOEXEC,3);
            if (replacement<0) throw std::runtime_error("Cannot reserve setup pipe descriptor");
            close(pair[i]);pair[i]=replacement;
        }
        Nonblocking(input[0]);Nonblocking(output[0]);Nonblocking(error[0]);
        std::vector<std::string> arguments{python,std::filesystem::absolute(worker).string(),"--workspace",
                                            std::filesystem::absolute(workspace).string()};
        std::vector<char*> argv;for (auto& argument : arguments) argv.push_back(argument.data());argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,input[1],STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions,output[1],STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions,error[1],STDERR_FILENO);
        for (auto pair : {input,output,error}) for (unsigned i=0;i<2;++i) posix_spawn_file_actions_addclose(&actions,pair[i]);
        posix_spawnattr_t attributes;posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attributes,0);
        const int code=posix_spawnp(&pid_,python.c_str(),&actions,&attributes,argv.data(),environ);
        posix_spawnattr_destroy(&attributes);posix_spawn_file_actions_destroy(&actions);
        if (code) { pid_=-1;throw std::runtime_error("Cannot start setup worker: "+std::string(std::strerror(code))); }
        input_=input[0];output_=output[0];error_=error[0];input[0]=output[0]=error[0]=-1;
        view_.status=Status::Running;view_.stage="inspect";view_.message="Checking selected game data...";
        return true;
    } catch (const std::exception& errorMessage) {
        view_.status=Status::Failed;view_.message=errorMessage.what();return false;
    }
}
void Job::Cancel() {
    if (!view_.Busy() || cancelled_) return;
    cancelled_=true;cancelAt_=Clock::now();pending_+=CancelRequest(identifier_);
    view_.status=Status::Cancelling;view_.message="Cancelling import...";view_.result.reset();
}
void Job::Abort(std::string message) {
    if (abort_.empty()) abort_=message.substr(0,1024);
    Cancel();view_.message="Stopping failed import...";
}
void Job::Consume(std::string_view line) {
    if (!abort_.empty()) return;
    try {
        if (!terminal_.empty()) throw std::runtime_error("Setup worker sent data after its final event");
        auto event=ParseEvent(line,identifier_,sequence_++);
        if (event.stage=="request") return;
        view_.stage=event.stage;view_.completed=event.completed;view_.total=event.total;
        if (!event.logPath.empty()) view_.logPath=std::move(event.logPath);
        if (event.result) {
            if (std::filesystem::weakly_canonical(event.result->source)!=std::filesystem::weakly_canonical(view_.source))
                throw std::runtime_error("Setup result belongs to another selected source");
            result_=std::move(event.result);
        }
        if (event.stage=="inspect" && (event.state=="complete" || event.state=="failed" || event.state=="cancelled")) {
            terminal_=event.state;terminalError_=std::move(event.error);
        }
    } catch (const std::exception& errorMessage) { Abort("Invalid setup response: "+std::string(errorMessage.what())); }
}
void Job::Drain(int fd,bool& eof,bool protocol) {
    if (eof) return;
    std::array<char,4096> data;
    for (unsigned reads=0;reads<(protocol ? 16u : 4u);++reads) {
        const auto size=read(fd,data.data(),data.size());
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
        const auto written=send(input_,pending_.data(),pending_.size(),MSG_NOSIGNAL|MSG_DONTWAIT);
        if (written>0) pending_.erase(0,written);
        else if (written<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) {
            pending_.clear();if (!cancelled_) Abort("Cannot send setup request");
        }
    }
    Drain(output_,outputEof_,true);Drain(error_,errorEof_,false);
    if (!exited_) {
        int result=0;const auto waited=waitpid(pid_,&result,WNOHANG);
        if (waited==pid_) {
            exited_=true;exitAt_=Clock::now();
            exitCode_=WIFEXITED(result) ? WEXITSTATUS(result) : 128+(WIFSIGNALED(result) ? WTERMSIG(result) : 0);
        } else if (waited<0 && errno!=EINTR) { exited_=true;exitAt_=Clock::now();exitCode_=1;Abort("Cannot collect setup worker exit status"); }
    }
    if (exited_ && outputEof_ && errorEof_) { Finish();return; }
    if (exited_ && Clock::now()-exitAt_>std::chrono::milliseconds(500)) Abort("Setup worker exited with open output pipes");
    if (cancelled_) {
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-cancelAt_).count();
        if (!termSent_ && elapsed>=static_cast<int64_t>(graceMs_)) { kill(-pid_,SIGTERM);termSent_=true; }
        if (!killSent_ && elapsed>=static_cast<int64_t>(graceMs_+3000)) { kill(-pid_,SIGKILL);killSent_=true; }
        if (exited_ && killSent_) { outputEof_=errorEof_=true;Finish(); }
    }
}
void Job::Finish() {
    if (!abort_.empty()) { view_.status=Status::Failed;view_.message=abort_; }
    else if (cancelled_ || (terminal_=="cancelled" && exitCode_==2)) {
        view_.status=Status::Cancelled;view_.message="Import cancelled. Previous game data kept.";
    } else if (terminal_=="complete" && exitCode_==0 && result_ && buffer_.empty()) {
        view_.status=Status::Ready;view_.message="RK5E01 Rev 0 game data verified";view_.result=std::move(result_);
    } else {
        view_.status=Status::Failed;
        view_.message=terminal_=="failed" && exitCode_==1 ? terminalError_ :
            "Setup worker exited without valid completion (code "+std::to_string(exitCode_)+")";
        if (!stderr_.empty()) view_.message+="\n"+stderr_.substr(0,2048);
    }
    if (view_.status!=Status::Ready) view_.result.reset();
    Close();
}
void Job::Close() {
    for (int* fd : {&input_,&output_,&error_}) if (*fd>=0) { close(*fd);*fd=-1; }
    pid_=-1;
}
} // namespace Launcher::Setup
