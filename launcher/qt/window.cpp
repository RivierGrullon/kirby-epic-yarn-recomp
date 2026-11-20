// SPDX-License-Identifier: GPL-3.0-only
#include "window.h"
#include "../language_preference.h"
#include <QJsonDocument>
#include <QDesktopServices>
#include <QDir>
#include <QLibraryInfo>
#include <algorithm>

namespace Yarn {
namespace {
QLabel* label(const QString& value,const char* style=nullptr) {
    auto* w=new QLabel(value); w->setTextFormat(Qt::PlainText);
    if(style) w->setObjectName(style);
    w->setWordWrap(true); return w;
}
void help(QWidget* target,const char* key) {
    if(!key)return;
    target->setToolTip(T(key)); target->setAccessibleDescription(T(key));
}
QPushButton* helpButton(const char* key) {
    auto* b=new QPushButton("?");b->setObjectName("help");b->setFixedSize(23,23);
    help(b,key);b->setAccessibleName(T("Help"));
    QObject::connect(b,&QPushButton::clicked,b,[b]{QToolTip::showText(b->mapToGlobal(QPoint(0,b->height())),b->toolTip(),b);});
    return b;
}
}

Window::Window(QString rootPath,QString hostPath,bool selfTest):root(std::move(rootPath)),host(std::move(hostPath)),test(selfTest) {
    lang=static_cast<int>(test ? Launcher::DefaultLanguage :
                          Launcher::ReadLanguage(QDir(root).filesystemPath() / "local/user"));
    language(lang);
    loadStandardText();
    setWindowTitle("Kirby's Epic Yarn");setMinimumSize(640,520);resize(1120,685);
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    auto* linen=new Linen;outer->addWidget(linen);
    auto* body=new QVBoxLayout(linen);body->setContentsMargins(32,12,32,22);body->setSpacing(13);
    auto* bar=new QWidget;bar->setFixedHeight(52);body->addWidget(bar);
    auto* top=new QHBoxLayout(bar);top->setContentsMargins(0,0,0,0);top->setSpacing(10);
    auto* emblem=new QLabel;emblem->setPixmap(brandButton());top->addWidget(emblem);
    brand=label("Kirby’s Epic Yarn","brand");brand->setWordWrap(false);top->addWidget(brand);
    top->addStretch();
    for(int i=0;i<3;++i) {
        const Page dest=std::array{Home,Basic,Display}[i];
        auto* b=new QPushButton;b->setObjectName("nav");b->setCheckable(true);b->setMinimumHeight(36);
        b->setCursor(Qt::PointingHandCursor);
        connect(b,&QPushButton::clicked,this,[this,dest]{navigate(dest);});nav[i]=b;top->addWidget(b);
    }
    auto* content=new QVBoxLayout;content->setSpacing(8);body->addLayout(content,1);
    message=label({},"notice");message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    message->hide();content->addWidget(message);
    scroll=new QScrollArea;scroll->viewport()->setObjectName("viewport");scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);content->addWidget(scroll,1);
    footer=new QWidget;auto* foot=new QHBoxLayout(footer);foot->setContentsMargins(0,0,0,0);content->addWidget(footer);
    saved=label({},"soft");foot->addWidget(saved,1);
    save=action(foot,"Save settings",[this]{request("save");});
    autoSave.setSingleShot(true);autoSave.setInterval(400);
    connect(&autoSave,&QTimer::timeout,this,[this]{if(needsSave)request("save");});
    headerText();navigate(Home);
    qApp->installEventFilter(this);
    if(test)return;
    connect(&service,&QProcess::readyReadStandardOutput,this,&Window::receive);
    connect(&service,&QProcess::readyReadStandardError,this,[this]{service.readAllStandardError();});
    connect(&service,&QProcess::errorOccurred,this,[this](auto){if(!finishing)notice(service.errorString());});
    connect(&service,&QProcess::finished,this,[this]{ticks.stop();ready=false;if(!finishing){refresh();notice(T("Launcher connection closed"));}});
    connect(&service,&QProcess::started,this,[this]{request("hello");ticks.start(65);});
    connect(&ticks,&QTimer::timeout,this,&Window::poll);
    service.setWorkingDirectory(root);service.start(host,{"--ui-service"});
}
Window::~Window() {
    qApp->removeEventFilter(this);finishing=true;ticks.stop();
    if(service.state()==QProcess::Running) {
        request("quit",{{"discard",discardOnClose}});service.closeWriteChannel();service.waitForFinished(3000);
    }
}
void Window::closeEvent(QCloseEvent* e) {
    if(!test && !finishing && !closing && ready && (needsSave || model["dirty"].toBool())) {
        closing=true;request("save");e->ignore();return;
    }
    if(closing && !finishing) {e->ignore();return;}
    finishing=true;QWidget::closeEvent(e);
}
void Window::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    const bool slim=e->size().width()<860;
    if(slim!=narrow) {
        narrow=slim;
        if(scroll && sheet)navigate(page,true);
    }
}
void Window::headerText() {
    const bool advanced=page==Display || page==Controls || page==Diagnostics;
    const int active=page==Home?0:advanced?2:1;
    const std::array names{"Home page","Basic settings","Advanced settings"};
    for(int i=0;i<3;++i) {
        nav[i]->setText(T(names[i]));nav[i]->setAccessibleName(T(names[i]));nav[i]->setChecked(i==active);
    }
    brand->setText(narrow?"Epic Yarn":"Kirby’s Epic Yarn");
    if(languages)languages->setCurrentIndex(lang);
    save->setText(T("Save settings"));
}
void Window::setLanguage(int i) {
    lang=i;language(i);loadStandardText();headerText();navigate(page,true);
    if(!test)request("lang",{{"value",i}});
}
void Window::loadStandardText() {
    qApp->removeTranslator(&standardText);
    if(lang==1)return;
    const QString locale=lang==0?"zh_CN":"es";
    const auto bundle=QCoreApplication::applicationDirPath()+"/translations";
    if(standardText.load("qt_"+locale,bundle) ||
       standardText.load("qtbase_"+locale,QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        qApp->installTranslator(&standardText);
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
void Window::notice(const QString& error) {message->setText(error);message->setVisible(!error.isEmpty());}
void Window::navigate(Page dest,bool keepScroll) {
    const int position=keepScroll?scroll->verticalScrollBar()->value():0;
    if(page==Controls && dest!=Controls && capturing)request("cancel");
    page=dest; bindings.clear();edits.clear(); play=nullptr;gameState=nullptr;dataState=nullptr;
    // Disconnect the old page before queued callbacks can target its controls.
    if(auto* old=scroll->takeWidget()){old->hide();old->deleteLater();}
    captureText=nullptr;motionText=nullptr;devices=nullptr;swap=nullptr;escape=nullptr;cancel=nullptr;
    calibrate=nullptr;motionMap=nullptr;pointerOptions=nullptr;bindingRows=nullptr;languages=nullptr;
    sheet=new QWidget;sheet->setObjectName("paper");pages=new QVBoxLayout(sheet);
    pages->setContentsMargins(0,2,0,2);pages->setSpacing(14);
    if(page!=Home) {
        const bool advanced=page==Display || page==Controls || page==Diagnostics;
        auto* banner=new Banner(page,root);banner->setMinimumHeight(narrow?86:99);
        auto* l=new QVBoxLayout(banner);l->setContentsMargins(25,9,135,9);
        l->addWidget(label(T(advanced?"Advanced settings":Titles[page]),"title"));pages->addWidget(banner);
        if(advanced) {
            auto* sections=new QHBoxLayout;pages->addLayout(sections);
            const std::array names{"Graphics and performance","Button mapping","Diagnostics"};
            for(int i=0;i<3;++i) {
                auto dest=std::array{Display,Controls,Diagnostics}[i];
                auto* b=action(sections,names[i],[this,dest]{navigate(dest);});
                b->setCheckable(true);b->setChecked(dest==page);
            }
        }
    }
    switch(page) {
    case Home:buildHome();break;case Basic:buildBasic();break;case ControllerGuide:buildControllerGuide();break;case Install:buildInstall();break;case Display:buildDisplay();break;
    case Controls:buildControls();break;case Saves:buildSaves();break;
    case Diagnostics:buildDiagnostics();break;default:break;
    }
    pages->addStretch();scroll->setWidget(sheet);
    footer->setVisible(page==Display || page==Controls || page==Diagnostics);
    headerText();refresh();
    QTimer::singleShot(0,sheet,[this,position]{scroll->verticalScrollBar()->setValue(position);});
}
void Window::refresh() {
    const bool editable=ready && model["settingsError"].toString().isEmpty();
    for(auto* w:edits)w->setEnabled(editable);
    save->setEnabled(editable && model["dirty"].toBool());
    saved->setText(model["settingsError"].toString().isEmpty()?T(model["dirty"].toBool()?"Unsaved changes":"No unsaved changes"):
        T("Settings need attention. Open Diagnostics to fix them."));
    const bool verified=model["verified"].toBool() && model["installed"].toBool() && model["installationError"].toString().isEmpty();
    if(play) {
        play->setText(T(model["running"].toBool()?"Game running":verified?"Start game":"Choose game data"));
        play->setEnabled(ready && !model["running"].toBool() && (!verified || editable));
    }
    if(dataState) {
        dataState->setText(ready?T(!model["installed"].toBool()?"Install the game to play":verified?"Game data verified":"Game data needs attention"):T("Checking native executable..."));
        dataState->setToolTip(status(model["dataStatus"].toString()));
    }
    if(gameState){gameState->setText(status(model["gameStatus"].toString()));gameState->setVisible(!gameState->text().isEmpty());}
    if(page==Controls)updateControls();
}
void Window::tracked(QWidget* w) {edits.append(w);}
void Window::patch(const char* key,QJsonValue value) {
    needsSave=true;config[key]=value;request("change",{{"key",key},{"value",value}});
    if(page==Basic)autoSave.start();
}
QVBoxLayout* Window::card(const char* title,const char* tip) {
    auto* surface=new QFrame;surface->setObjectName("card");
    auto* lay=new QVBoxLayout(surface);lay->setContentsMargins(19,14,19,16);lay->setSpacing(10);
    auto* heading=new QHBoxLayout;heading->addWidget(label(T(title),"section"),1);
    if(tip)heading->addWidget(helpButton(tip));lay->addLayout(heading);pages->addWidget(surface);return lay;
}
QWidget* Window::row(QVBoxLayout* owner,QString title,QWidget* control,const char* tip) {
    auto* w=new QWidget;auto* h=new QHBoxLayout(w);h->setContentsMargins(0,2,0,2);
    auto* name=label(title);name->setBuddy(control);h->addWidget(name,1);
    control->setAccessibleName(title);help(w,tip);help(name,tip);help(control,tip);
    h->addWidget(control);owner->addWidget(w);return w;
}
QPushButton* Window::action(QLayout* owner,const char* title,std::function<void()> fn,const char* tip) {
    auto* b=new FabricButton(T(title));help(b,tip);connect(b,&QPushButton::clicked,this,std::move(fn));
    owner->addWidget(b);return b;
}
QCheckBox* Window::check(QVBoxLayout* owner,const char* key,const char* title,const char* tip) {
    auto* c=new SewnSwitch;c->setChecked(config[key].toBool());
    row(owner,T(title),c,tip);tracked(c);
    connect(c,&QCheckBox::toggled,this,[this,key](bool on){patch(key,on);});return c;
}
QComboBox* Window::select(QVBoxLayout* owner,const char* key,const char* title,const QStringList& names,
                          const QList<int>& values,const char* tip) {
    auto* c=new FabricCombo;c->addItems(names);c->setCurrentIndex(std::max(0,int(values.indexOf(config[key].toInt()))));
    row(owner,T(title),c,tip);tracked(c);
    connect(c,&QComboBox::activated,this,[this,key,values](int i){patch(key,values.at(i));});return c;
}
void Window::number(QVBoxLayout* owner,const char* key,const char* title,double low,double high,double step,const char* tip) {
    auto* n=new FabricSpinBox;n->setDecimals(step<.01?3:2);n->setRange(low,high);n->setSingleStep(step);
    n->setValue(config[key].toDouble());n->setKeyboardTracking(false);
    row(owner,T(title),n,tip);tracked(n);
    connect(n,&QDoubleSpinBox::valueChanged,this,[this,key](double v){patch(key,v);});
}
void Window::field(QVBoxLayout* owner,const char* name,const QString& text) {
    auto* l=label(text);l->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);
    l->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);owner->addWidget(label(T(name),"soft"));owner->addWidget(l);
}
void Window::openFolder(const QString& path) {
    if(path.isEmpty() || !QDir().mkpath(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))notice(T("Could not open folder"));
}
void Window::buildHome() {
    auto* banner=new Banner(Home,root);banner->setMinimumHeight(narrow?302:405);
    auto* l=new QVBoxLayout(banner);l->setContentsMargins(narrow?23:48,20,14,22);l->setSpacing(0);
    l->addStretch();l->addWidget(label(lang==0?"Kirby’s Epic Yarn":"Kirby’s","kicker"));
    auto* title=label(lang==0?T("Kirby's Epic Yarn"):"Epic Yarn","homeTitle");
    if(narrow)title->setStyleSheet("font-size:47px;font-weight:bold;color:#314c40;");
    l->addWidget(title);l->addSpacing(narrow?5:13);
    auto* statusRow=new QHBoxLayout;statusRow->setSpacing(8);
    auto* dot=new QLabel;dot->setPixmap(brandButton(true));
    statusRow->addWidget(dot);dataState=label({});statusRow->addWidget(dataState);statusRow->addStretch();
    l->addLayout(statusRow);l->addSpacing(11);
    l->addWidget(label("RK5E01    /    USA    /    Rev. 0","edition"));
    l->addSpacing(narrow?13:24);play=new FabricButton({},true);play->setMinimumWidth(narrow?165:210);
    l->addWidget(play,0,Qt::AlignLeft);l->addStretch();pages->addWidget(banner);
    connect(play,&QPushButton::clicked,this,[this]{if(model["verified"].toBool() && model["installed"].toBool() && model["installationError"].toString().isEmpty())request("play");else navigate(Install);});
    gameState=label({});pages->addWidget(gameState);
    pages->addSpacing(narrow?0:14);pages->addWidget(label(T("Game library"),"libraryTitle"));
    auto* quick=new QHBoxLayout;quick->setSpacing(16);pages->addLayout(quick);
    for(auto dest:{Basic,ControllerGuide,Saves}) {
        const char* name=dest==Basic?"Display and sound":Titles[dest];
        const char* sub=dest==Basic?"Picture and volume":dest==ControllerGuide?"Buttons and motion":"Open your save folder";
        auto* b=new LibraryCard(dest==Basic?Display:dest,T(name),T(sub));quick->addWidget(b,1);
        connect(b,&QPushButton::clicked,this,[this,dest]{navigate(dest);});
    }
}
void Window::buildBasic() {
    auto* simple=card("Display and sound","Basic settings are saved automatically.");
    check(simple,"fullscreen","Start in fullscreen","Open the game in borderless fullscreen on the next launch. Press Alt+Enter while playing to switch between fullscreen and windowed mode for that session.");
    auto* audio=new QWidget;auto* mix=new QHBoxLayout(audio);mix->setContentsMargins(0,0,0,0);
    auto* slider=new QSlider(Qt::Horizontal);slider->setRange(0,100);slider->setMinimumWidth(narrow?95:175);
    slider->setValue(qRound(config["volume"].toDouble()));auto* percent=label(QString::number(slider->value())+"%");
    percent->setMinimumWidth(45);mix->addWidget(slider,1);mix->addWidget(percent);
    row(simple,T("Master volume"),audio,"Adjusts the complete game mix. Set to 0% to mute it.");tracked(slider);
    slider->setAccessibleName(T("Master volume"));
    connect(slider,&QSlider::valueChanged,this,[this,percent](int v){percent->setText(QString::number(v)+"%");patch("volume",v);});
    auto* ui=card("Launcher");
    languages=new FabricCombo;languages->addItems({"简体中文","English","Español"});languages->setCurrentIndex(lang);
    row(ui,T("Language"),languages);connect(languages,&QComboBox::activated,this,&Window::setLanguage);
    auto* manage=card("Game library");auto* buttons=new QHBoxLayout;manage->addLayout(buttons);
    for(auto dest:{ControllerGuide,Saves,Install})action(buttons,Titles[dest],[this,dest]{navigate(dest);});
}
void Window::buildControllerGuide() {
    auto* box=card("How to play");
    auto* grid=new QGridLayout;box->addLayout(grid);
    for(int col=0;col<3;++col)grid->addWidget(label(T(std::array{"Action","Gamepad","Keyboard"}[col]),"soft"),0,col);
    const auto rows=model["bindings"].toArray();
    for(int i=0;i<rows.size();++i) {
        const auto r=rows[i].toArray();
        for(int col=0;col<3;++col)grid->addWidget(label(status(r.at(col).toString())),i+1,col);
    }
    action(box,"Customize controls",[this]{controlTab=0;navigate(Controls);});
}
void Window::buildInstall() {
    auto* data=card("Game data","Choose the extracted DATA folder from your own RK5E01 Rev 0 (USA) copy, then open Play.");
    dataState=label({});data->addWidget(dataState);field(data,"Game data folder",model["data"].toString());
    if(ready && !model["verified"].toBool())data->addWidget(label(status(model["dataStatus"].toString()),"notice"));
    auto* buttons=new QHBoxLayout;data->addLayout(buttons);
    action(buttons,"Choose DATA folder",[this]{
        const auto folder=QFileDialog::getExistingDirectory(this,T("Choose DATA folder"),model["data"].toString());
        if(!folder.isEmpty())request("data",{{"path",folder}});
    })->setEnabled(!model["running"].toBool());
    action(buttons,"Verify selected folder",[this]{request("verify");});
    auto* setup=card("Installed game","Kirby Setup handles installation and updates in a separate window.");
    if(!model["installationError"].toString().isEmpty())setup->addWidget(label(model["installationError"].toString(),"notice"));
    if(!model["setupAvailable"].toBool())setup->addWidget(label(T("Open your Kirby Setup AppImage or Windows setup application to install the game."),"soft"));
    action(setup,"Open Kirby Setup",[this]{request("setup");})->setEnabled(model["setupAvailable"].toBool() && !model["running"].toBool());
}
void Window::buildDisplay() {
    auto* quality=card("Image quality");
    select(quality,"scale","Internal resolution",{T("1x (native)"),"2×","3×","4×","5×","6×","8×"},{1,2,3,4,5,6,8},
        "Renders geometry at a higher resolution for sharper edges. Higher multipliers use more GPU time and memory; original texture detail is unchanged.");
    select(quality,"aa","Anti-aliasing",{T("Off"),"FXAA","4× MSAA","4× MSAA + FXAA"},{0,1,2,3},
        "FXAA smooths edges in the final image at a low GPU cost, but can soften fine details. MSAA smooths geometry edges; the combined mode applies both.");
    auto* video=card("Window and presentation");
    check(video,"vsync","VSync","Synchronize presentation with the display. Turning this off can reduce stalls on some drivers.");
    check(video,"performance","Experimental performance mode","Reduces GPU copy waits and repeated texture processing. Compatibility testing is ongoing.");
}
void Window::buildSaves() {
    auto* box=card(model["saveFound"].toBool()?"Game save found":"No game save yet",
                   "The game manages its own saves. Choosing another DATA folder does not move them.");
    field(box,"Save files",model["saves"].toString());
    action(box,"Open Saves",[this]{openFolder(model["saves"].toString());});
}
void Window::buildDiagnostics() {
    auto* d=card("Build information");QJsonObject runtime=model["runtime"].toObject();
    for(auto name:{"build","runtime"}) {
        const auto b=model[name].toObject();
        if(!b.isEmpty())field(d,QString(name)=="build"?"Launcher":"Native game",
            b["revision"].toString().left(12)+"   ·   "+b["compiler"].toString());
    }
    if(!runtime.isEmpty())d->addWidget(label(T(model["match"].toBool()?"Launcher and runtime build records match.":"Launcher and runtime build records differ."),"soft"));
    if(!model["buildError"].toString().isEmpty())d->addWidget(label(model["buildError"].toString(),"notice"));
    auto* detail=new QPlainTextEdit;detail->setReadOnly(true);detail->setMaximumHeight(180);
    detail->setPlainText(QString::fromUtf8(QJsonDocument(QJsonObject{{"launcher",model["build"]},{"runtime",model["runtime"]}}).toJson()));
    auto* details=action(d,"Source and dependency details",[detail]{detail->setVisible(!detail->isVisible());});
    d->addWidget(detail);detail->hide();details->setCheckable(true);
    action(d,"Refresh build information",[this]{request("refresh");})->setEnabled(!model["checking"].toBool()&&!model["running"].toBool());
    auto* files=card("Shared settings");field(files,"Native executable",model["executable"].toString());
    field(files,"Shared settings",model["settings"].toString());
    if(!model["settingsError"].toString().isEmpty())files->addWidget(label(model["settingsError"].toString(),"notice"));
    action(files,model["dirty"].toBool()?"Discard changes and reload":"Reload settings",[this]{request("reload");});
    auto* shortcuts=new QVBoxLayout;files->addLayout(shortcuts);
    action(shortcuts,"Open Settings Folder",[this]{openFolder(QFileInfo(model["settings"].toString()).absolutePath());});
    action(shortcuts,"Open Logs",[this]{openFolder(QDir(root).filePath("build-logs"));});
    action(shortcuts,"Open Build Guide",[this]{
        const auto file=QDir(root).filePath("docs/building.md");
        const auto url=QFileInfo::exists(file)?QUrl::fromLocalFile(file):QUrl("https://github.com/RivierGrullon/kirby-epic-yarn-recomp/blob/dev/docs/building.md");
        if(!QDesktopServices::openUrl(url))notice(T("Could not open folder"));
    });
}
}
