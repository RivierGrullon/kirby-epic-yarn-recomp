// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include "theme/form_controls.h"
#include <QApplication>
#include <QBoxLayout>
#include <QGridLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QTabBar>
#include <algorithm>

namespace Yarn {
namespace {
QString scanName(QKeyEvent* e) {
    switch(e->key()) {
    case Qt::Key_Escape:return "Escape";case Qt::Key_Return:return "Return";
    case Qt::Key_Enter:return "Keypad Enter";case Qt::Key_Space:return "Space";
    case Qt::Key_Control:return (e->nativeScanCode()&0x100)?"Right Ctrl":"Left Ctrl";
    case Qt::Key_Shift:return e->nativeScanCode()==54?"Right Shift":"Left Shift";
    case Qt::Key_Alt:return (e->nativeScanCode()&0x100)?"Right Alt":"Left Alt";
    case Qt::Key_AltGr:return "Right Alt";case Qt::Key_Backtab:return "Tab";
    case Qt::Key_PageUp:return "PageUp";case Qt::Key_PageDown:return "PageDown";
    case Qt::Key_Delete:return "Delete";case Qt::Key_Insert:return "Insert";
    case Qt::Key_Backspace:return "Backspace";case Qt::Key_Meta:return "Left GUI";
    default:return QKeySequence(e->key()).toString(QKeySequence::PortableText);
    }
}
}

void Window::bind(int i,bool keyboard) {
    capturing=true;
    request("bind",{{"action",i},{"keyboard",keyboard}});
}

bool Window::eventFilter(QObject* object,QEvent* event) {
    if(page==Controls && capturing && isActiveWindow() &&
       (event->type()==QEvent::KeyPress || event->type()==QEvent::KeyRelease)) {
        auto* key=static_cast<QKeyEvent*>(event);
        if(input["phase"].toInt()==3 && !input["release"].toBool() && key->key()!=Qt::Key_Escape)
            return QWidget::eventFilter(object,event);
        if(!key->isAutoRepeat())request("key",{{"key",scanName(key)},{"down",event->type()==QEvent::KeyPress}});
        return true;
    }
    return QWidget::eventFilter(object,event);
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

void Window::buildControls() {
    auto* tabs=new QTabBar;tabs->setExpanding(false);tabs->addTab(T("Input settings"));tabs->addTab(T("Motion"));
    tabs->setCurrentIndex(controlTab);pages->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int i){controlTab=i;navigate(Controls);});
    if (controlTab==0) buildBindings();
    else buildMotion();
}

void Window::buildBindings() {
    auto* keys=card("Button bindings","Select a binding, then press the new input. Gamepad bindings apply to all players; keyboard bindings apply to player 1. Launcher navigation stays unchanged.");
    bindingUi.message=new QLabel;bindingUi.message->setWordWrap(true);keys->addWidget(bindingUi.message);
    auto* actions=new QHBoxLayout;keys->addLayout(actions);
    bindingUi.swap=action(actions,"Swap bindings",[this]{request("swap");});
    bindingUi.escape=action(actions,"Use Escape",[this]{request("escape");});
    bindingUi.cancel=action(actions,"Cancel capture",[this]{request("cancel");});
    auto* table=new QWidget;auto* grid=new QGridLayout(table);grid->setContentsMargins(0,0,0,0);
    keys->addWidget(table);bindingUi.table=table;
    grid->addWidget(new QLabel(T("Action")),0,0);grid->addWidget(new QLabel(T("Gamepad")),0,1);
    grid->addWidget(new QLabel(T("Keyboard")),0,2);grid->setColumnStretch(1,2);grid->setColumnStretch(2,2);
    const auto rows=model["bindings"].toArray();
    for(int i=0;i<rows.size();++i) {
        const auto binding=rows[i].toArray();auto* title=new QLabel(status(binding.at(0).toString()));
        title->setWordWrap(true);grid->addWidget(title,i+1,0);
        QPair<QPushButton*,QPushButton*> pair;
        for(int col=1;col<=2;++col) {
            const bool keyboard=col==2;auto* cell=new QHBoxLayout;cell->setSpacing(2);
            auto* b=new FabricButton(status(binding.at(col).toString()));
            b->setMinimumWidth(30);b->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
            b->setAccessibleName(title->text()+" · "+T(keyboard?"Keyboard":"Gamepad"));
            cell->addWidget(b,1);connect(b,&QPushButton::clicked,this,[this,i,keyboard]{bind(i,keyboard);});
            auto* clear=new QPushButton("×");clear->setObjectName("help");clear->setFixedSize(23,23);
            clear->setToolTip(T("Clear"));clear->setAccessibleName(T("Clear")+" "+b->accessibleName());
            cell->addWidget(clear);connect(clear,&QPushButton::clicked,this,[this,i,keyboard]{request("clear",{{"action",i},{"keyboard",keyboard}});});
            grid->addLayout(cell,i+1,col);if(keyboard)pair.second=b;else pair.first=b;
        }
        bindingUi.buttons.append(pair);
    }
    auto* levels=card("Sticks and triggers","For actions assigned to an axis, choose how far to move the stick or trigger before the action activates.");
    number(levels,"stick","Stick activation",.05,.95,.05);number(levels,"trigger","Trigger activation",.05,.95,.05);
    auto* resetRow=new QHBoxLayout;pages->addLayout(resetRow);
    action(resetRow,"Reset controls",[this]{request("resetControls");});
    resetRow->addStretch();
}

void Window::updateControls() {
    if(!ready)return;
    const int phase=input["phase"].toInt();capturing=phase!=0;
    const bool editable=model["settingsError"].toString().isEmpty();
    if(bindingUi.table)bindingUi.table->setEnabled(!capturing && editable);
    if(bindingUi.swap) {bindingUi.swap->setVisible(phase==3);bindingUi.swap->setEnabled(!input["release"].toBool());}
    if(bindingUi.cancel)bindingUi.cancel->setVisible(capturing);
    if(bindingUi.escape)bindingUi.escape->setVisible(phase==1 && input["keyboardCapture"].toBool());
    if(bindingUi.message) {
        QString caption=status(input["message"].toString());
        if(input["release"].toBool())caption=T("Release the captured input to continue.");
        else if(phase==1) caption=T(input["keyboardCapture"].toBool()?
              "Press a keyboard key. Escape cancels capture; Use Escape assigns that key.":
              "Press a gamepad button or move a centered stick/trigger. Escape or Cancel stops capture.");
        bindingUi.message->setText(caption);bindingUi.message->setVisible(!caption.isEmpty());
    }
    const auto rows=model["bindings"].toArray();
    for(int i=0;i<std::min(bindingUi.buttons.size(),rows.size());++i) {
        const auto vals=rows[i].toArray();
        auto update=[&](QPushButton* b,int col) {
            const auto name=status(vals.at(col).toString());b->setText(name);b->setToolTip(name+" · "+T("Change binding"));
        };
        update(bindingUi.buttons[i].first,1);update(bindingUi.buttons[i].second,2);
    }
    if (motionUi.devices) updateMotion(editable);
}
} // namespace Yarn
