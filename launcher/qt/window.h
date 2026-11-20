// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "theme.h"
#include <QtWidgets>
#include <QProcess>
#include <functional>

namespace Yarn {
class Window : public QWidget {
public:
    Window(QString root, QString host, bool selfTest);
    ~Window() override;
    bool preview(const QString& directory);
protected:
    void resizeEvent(QResizeEvent*) override;
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
private:
    QString root,host;
    bool test,ready=false,waitingPoll=false,closing=false,finishing=false;
    bool narrow=false,needsSave=false,discardOnClose=false;
    int lang=0,controlTab=0;
    Page page=Home;
    QProcess service;
    QTranslator standardText;
    QTimer ticks,autoSave;
    QJsonObject model,input,config;
    QWidget *footer{},*sheet{};
    QVBoxLayout *pages{};
    QScrollArea* scroll{};
    QLabel *brand{},*message{},*saved{},*gameState{},*dataState{};
    QPointer<QComboBox> languages;
    QPushButton *save{},*play{};
    std::array<QPushButton*,3> nav{};
    QList<QPair<QPushButton*,QPushButton*>> bindings;
    QPointer<QLabel> captureText,motionText;
    QPointer<QComboBox> devices;
    QPointer<QPushButton> swap,escape,cancel,calibrate;
    QPointer<QWidget> motionMap,pointerOptions,bindingRows;
    bool capturing=false;
    void request(QString cmd,QJsonObject args={});
    void receive();
    void poll();
    void navigate(Page dest,bool keepScroll=false);
    void refresh();
    void headerText();
    void setLanguage(int idx);
    void loadStandardText();
    void notice(const QString& error);
    void buildHome();
    void buildBasic();
    void buildControllerGuide();
    void buildInstall();
    void buildDisplay();
    void buildControls();
    void buildSaves();
    void buildDiagnostics();
    void updateControls();
    void bind(int action,bool keyboard);
    void patch(const char* key,QJsonValue value);
    void openFolder(const QString& path);
    QWidget* row(QVBoxLayout* parent,QString title,QWidget* control,const char* help=nullptr);
    QVBoxLayout* card(const char* title,const char* help=nullptr);
    QPushButton* action(QLayout* owner,const char* title,std::function<void()> fn,const char* hint=nullptr);
    QCheckBox* check(QVBoxLayout*,const char* key,const char* title,const char* help=nullptr);
    QComboBox* select(QVBoxLayout*,const char* key,const char* title,const QStringList& items,
                      const QList<int>& values,const char* help=nullptr);
    void number(QVBoxLayout*,const char* key,const char* title,double low,double high,
                double step,const char* help=nullptr);
    void field(QVBoxLayout*,const char* key,const QString& value);
    void tracked(QWidget* w);
    QList<QWidget*> edits;
};
}
