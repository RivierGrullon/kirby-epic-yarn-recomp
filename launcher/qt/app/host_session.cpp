// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>

namespace Yarn {
void Window::startService(const QString& host) {
    connect(&service,&QProcess::readyReadStandardOutput,this,&Window::receive);
    connect(&service,&QProcess::readyReadStandardError,this,[this]{service.readAllStandardError();});
    connect(&service,&QProcess::errorOccurred,this,[this](auto){if(!finishing)notice(service.errorString());});
    connect(&service,&QProcess::finished,this,[this]{ticks.stop();ready=false;if(!finishing){refresh();notice(T("Launcher connection closed"));}});
    connect(&service,&QProcess::started,this,[this]{request("hello");ticks.start(65);});
    connect(&ticks,&QTimer::timeout,this,&Window::poll);
    service.setWorkingDirectory(root);service.start(host,{"--ui-service"});
}

void Window::request(QString cmd,QJsonObject args) {
    if(test || service.state()!=QProcess::Running)return;
    args["cmd"]=cmd;args["focused"]=isActiveWindow();args["controls"]=page==Controls;
    service.write(QJsonDocument(args).toJson(QJsonDocument::Compact)+'\n');
}

void Window::poll() {
    if(!ready || waitingPoll || finishing)return;
    const int interval=page==Controls && isActiveWindow()?20:200;
    if(ticks.interval()!=interval)ticks.setInterval(interval);
    waitingPoll=true;request("poll");
}

void Window::receive() {
    while(service.canReadLine()) {
        const auto reply=QJsonDocument::fromJson(service.readLine()).object();
        const auto cmd=reply["cmd"].toString();
        if(cmd=="poll")waitingPoll=false;
        if(reply.contains("error")) {
            notice(status(reply["error"].toString()));
            if(cmd=="save" && closing) {
                closing=false;
                if(QMessageBox::question(this,T("Save settings"),T("Close without saving?"))==QMessageBox::Yes) {
                    discardOnClose=true;finishing=true;close();
                }
            }
            continue;
        }
        const bool first=!ready;
        const bool wasRunning=model["running"].toBool();
        input=reply["input"].toObject();
        if(reply.contains("state")) {model=reply["state"].toObject();config=model["config"].toObject();ready=true;}
        if(first || cmd=="reload" || cmd=="resetControls" || cmd=="resetMotion" || cmd=="data" ||
           (page==Diagnostics && reply.contains("state") && cmd!="change")) navigate(page,true);
        if(page==Saves && (cmd=="importSave" || cmd=="exportSave" || cmd=="restoreBackup" || cmd=="deleteBackup"))
            navigate(page,true);
        if(cmd=="save") {needsSave=false;if(closing){finishing=true;close();}}
        if(cmd!="poll" && cmd!="hello")notice({});
        refresh();
        if(!wasRunning && model["running"].toBool())showMinimized();
        if(wasRunning && !model["running"].toBool()){showNormal();raise();activateWindow();}
        if(isActiveWindow() && !capturing) for(auto event:input["nav"].toArray()) {
            const auto key=event.toString();
            if(key=="dpdown" || key=="dpright")focusNextChild();
            else if(key=="dpup" || key=="dpleft")focusPreviousChild();
            else if(key=="south" && focusWidget()) {
                QKeyEvent press(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier),release(QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);
                QApplication::sendEvent(focusWidget(),&press);
                if(focusWidget())QApplication::sendEvent(focusWidget(),&release);
            }
        }
    }
}

void Window::patch(const char* key,QJsonValue value) {
    needsSave=true;config[key]=value;request("change",{{"key",key},{"value",value}});
    if(page==Basic)autoSave.start();
}
} // namespace Yarn
