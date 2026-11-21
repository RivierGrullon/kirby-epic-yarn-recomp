// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include "theme/form_controls.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QToolTip>
#include <algorithm>
#include <utility>

namespace Yarn {
namespace {
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

QLabel* Window::label(const QString& value,const char* style) {
    auto* w=new QLabel(value); w->setTextFormat(Qt::PlainText);
    if(style) w->setObjectName(style);
    w->setWordWrap(true); return w;
}

void Window::tracked(QWidget* w) {edits.append(w);}

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
} // namespace Yarn
