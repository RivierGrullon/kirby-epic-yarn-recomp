// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include "theme/form_controls.h"
#include <QBoxLayout>
#include <QLabel>
#include <QSlider>

namespace Yarn {
void Window::buildBasic() {
    auto* simple=card("Display and sound","Basic settings are saved automatically.");
    select(simple,"gameLanguage","Game language",{"English","Français","Español"},{1,3,4},
           "Applies the next time you start the game.");
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
} // namespace Yarn
