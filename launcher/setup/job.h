// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "protocol.h"
#include <chrono>
#include <sys/types.h>

namespace Launcher::Setup {
enum class Status { Idle,Running,Cancelling,Ready,Failed,Cancelled };
struct View {
    Operation operation=Operation::Inspect;
    Status status=Status::Idle;
    std::string stage,message;
    std::filesystem::path source,logPath;
    std::optional<uint64_t> completed,total;
    std::optional<Inspection> result;
    std::optional<Installation> installation;
    bool Busy() const { return status==Status::Running || status==Status::Cancelling; }
};
class Job {
public:
    explicit Job(uint64_t cancellationGraceMs=20000) : graceMs_(cancellationGraceMs) {}
    ~Job();
    Job(const Job&)=delete;
    Job& operator=(const Job&)=delete;
    bool Start(const std::filesystem::path& worker,const std::filesystem::path& source,
               const std::filesystem::path& workspace,const std::string& python="python3",
               Operation operation=Operation::Inspect);
    void Poll();
    void Cancel();
    const View& Current() const { return view_; }
private:
    using Clock=std::chrono::steady_clock;
    void Consume(std::string_view line);
    void Abort(std::string message);
    void Finish();
    void Close();
    void Drain(int fd,bool& eof,bool protocol);
    View view_;
    pid_t pid_=-1;
    int input_=-1,output_=-1,error_=-1,exitCode_=0;
    uint64_t graceMs_,sequence_=1;
    bool exited_=false,outputEof_=false,errorEof_=false,cancelled_=false,termSent_=false,killSent_=false;
    Clock::time_point cancelAt_,exitAt_;
    std::string identifier_,pending_,buffer_,stderr_,abort_,terminal_,terminalError_;
    std::optional<Inspection> result_;
    std::optional<Installation> installation_;
    std::filesystem::path workspace_;
    std::string operation_;
};
} // namespace Launcher::Setup
