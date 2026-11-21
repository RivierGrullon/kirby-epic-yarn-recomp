// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include "theme/patchwork.h"
#include "theme/form_controls.h"
#include <QBoxLayout>
#include <QLabel>

namespace Yarn {
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
} // namespace Yarn
