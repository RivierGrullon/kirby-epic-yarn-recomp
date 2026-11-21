// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>

namespace Yarn {
bool Window::preview(const QString& directory) {
    if(!directory.isEmpty() && !QDir().mkpath(directory))return false;
    ready=true;model={{"installed",true},{"verified",true},{"data","disc/DATA"},{"saves","local/user/nand/title/00010000/524b3545/data"},
        {"executable","WiiCompiled"},{"settings","local/user/settings.toml"},{"build",QJsonObject{{"revision","preview"},{"compiler","C++"}}}};
    config={{"scale",1},{"aa",0},{"volume",80},{"pointer",1},{"tilt",true},{"gain",1},{"deadzone",.008},{"sensitivity",1.5},
            {"stick",.5},{"trigger",.5}};
    QJsonArray rows;
    rows.append(QJsonArray{"Move up","D-pad Up","Up"});rows.append(QJsonArray{"Jump / Confirm","South (A / Cross)","Space"});
    model["bindings"]=rows;show();
    for(int i=0;i<3;++i) {
        setLanguage(i);
        for(int compact=0;compact<2;++compact) {
            resize(compact?640:1120,compact?520:685);
            for(int j=0;j<Count;++j) {
                navigate(static_cast<Page>(j));QApplication::processEvents();
                if(j==Display && sheet->findChildren<QComboBox*>().size()!=2)return false;
                const auto snapshot=grab();
                if(snapshot.isNull())return false;
                constexpr std::array fileNames{"home","install","graphics","bindings","saves","diagnostics","basic","controller"};
                if(!directory.isEmpty()) {
                    const auto name=QString("%1-%2%3.png").arg(QStringList{"zh","en","es"}[i],fileNames[j],compact?"-compact":"");
                    if(!snapshot.save(QDir(directory).filePath(name)))return false;
                }
            }
            controlTab=1;navigate(Controls);QApplication::processEvents();
            if(!motionUi.map || !motionUi.calibrate || motionUi.calibrate->toolTip().isEmpty())return false;
            for (auto* spin:sheet->findChildren<QDoubleSpinBox*>()) {
                auto* edit=spin->findChild<QLineEdit*>();
                if (!edit || edit->contentsRect().marginsRemoved(edit->textMargins()).width()
                        < edit->fontMetrics().horizontalAdvance(edit->text())+4) return false;
            }
            input={{"devices",QJsonArray{QJsonObject{{"id",1},{"name","Preview controller"}}}},
                   {"selected",1},{"gyro",true},{"accel",true}};
            updateControls();
            const auto neutral=motionUi.map->grab().toImage();
            input["tiltAngle"]=35.;updateControls();
            const auto tilted=motionUi.map->grab().toImage();
            if (tilted==neutral) return false;
            input["x"]=.5;input["y"]=-.4;updateControls();
            if (motionUi.map->grab().toImage()==tilted) return false;
            if(!directory.isEmpty()) {
                const auto name=QString("%1-motion%2.png").arg(QStringList{"zh","en","es"}[i],compact?"-compact":"");
                if(!grab().save(QDir(directory).filePath(name)))return false;
            }
            input={};
            controlTab=0;
        }
    }
    return true;
}
} // namespace Yarn
