// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include "theme/patchwork.h"
#include "../../language_preference.h"
#include <QApplication>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLibraryInfo>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <utility>

namespace Yarn {
Window::Window(QString rootPath,QString hostPath,bool selfTest):root(std::move(rootPath)),test(selfTest) {
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
    if (!test) startService(hostPath);
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
    if(standardText.load("qt_"+locale,bundle) || standardText.load("qtbase_"+locale,bundle) ||
       standardText.load("qtbase_"+locale,QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        qApp->installTranslator(&standardText);
}

void Window::notice(const QString& error) {message->setText(error);message->setVisible(!error.isEmpty());}

void Window::navigate(Page dest,bool keepScroll) {
    const int position=keepScroll?scroll->verticalScrollBar()->value():0;
    if(page==Controls && dest!=Controls && capturing)request("cancel");
    page=dest; edits.clear(); play=nullptr;gameState=nullptr;dataState=nullptr;importSaveState=nullptr;
    // Disconnect the old page before queued callbacks can target its controls.
    if(auto* old=scroll->takeWidget()){old->hide();old->deleteLater();}
    bindingUi={};
    motionUi={};
    languages=nullptr;
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
    if(importSaveState){importSaveState->setText(status(model["saveImportStatus"].toString()));importSaveState->setVisible(!importSaveState->text().isEmpty());}
    if(page==Controls)updateControls();
}

} // namespace Yarn
