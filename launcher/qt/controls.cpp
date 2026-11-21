// SPDX-License-Identifier: GPL-3.0-only
#include "window.h"
#include <QPainter>

namespace Yarn {
namespace {
class PointerView final : public QWidget {
public:
    QPointF point;
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        QRectF bounds=QRectF(rect()).adjusted(4,4,-4,-4);
        p.setBrush(QColor("#edf2e8"));p.setPen(QPen(QColor("#b6c6b1"),1,Qt::DashLine));p.drawRoundedRect(bounds,11,11);
        p.drawLine(QPointF(bounds.center().x(),bounds.top()),QPointF(bounds.center().x(),bounds.bottom()));
        p.drawLine(QPointF(bounds.left(),bounds.center().y()),QPointF(bounds.right(),bounds.center().y()));
        p.setBrush(QColor("#c77994"));p.setPen(QPen(QColor("#985775"),1));
        p.drawEllipse(QPointF(bounds.center().x()+point.x()*(bounds.width()/2-7),
                              bounds.center().y()+point.y()*(bounds.height()/2-7)),6,6);
    }
};
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
void Window::buildControls() {
    auto* tabs=new QTabBar;tabs->setExpanding(false);tabs->addTab(T("Button bindings"));tabs->addTab(T("Motion"));
    tabs->setCurrentIndex(controlTab);pages->addWidget(tabs);
    connect(tabs,&QTabBar::currentChanged,this,[this](int i){controlTab=i;navigate(Controls);});
    if(controlTab==0) {
        auto* keys=card("Button bindings","Select a binding, then press the new input. Gamepad bindings apply to all players; keyboard bindings apply to player 1. Launcher navigation stays unchanged.");
        captureText=new QLabel;captureText->setWordWrap(true);keys->addWidget(captureText);
        auto* actions=new QHBoxLayout;keys->addLayout(actions);
        swap=action(actions,"Swap bindings",[this]{request("swap");});
        escape=action(actions,"Use Escape",[this]{request("escape");});
        cancel=action(actions,"Cancel capture",[this]{request("cancel");});
        auto* table=new QWidget;auto* grid=new QGridLayout(table);grid->setContentsMargins(0,0,0,0);
        keys->addWidget(table);bindingRows=table;
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
            bindings.append(pair);
        }
        auto* levels=card("Activation thresholds","For actions assigned to an axis, choose how far to move the stick or trigger before the action activates.");
        number(levels,"stick","Stick activation",.05,.95,.05);number(levels,"trigger","Trigger activation",.05,.95,.05);
        action(levels,"Reset controls",[this]{request("resetControls");});
    } else {
        auto* panel=card("Gyroscope & gravity / tilt","Use a controller with motion sensors exposed to SDL. Xbox-emulated devices may hide them.");
        devices=new FabricCombo;row(panel,T("Preview controller"),devices);
        connect(devices,&QComboBox::activated,this,[this](int i){request("pad",{{"id",devices->itemData(i).toInt()}});});
        auto* preview=new PointerView;preview->setMinimumHeight(82);panel->addWidget(preview);motionMap=preview;
        motionText=new QLabel;motionText->setWordWrap(true);panel->addWidget(motionText);
        auto* buttons=new QHBoxLayout;panel->addLayout(buttons);
        action(buttons,"Recenter preview",[this]{request("recenter");});
        calibrate=action(buttons,"Calibrate gyro",[this]{request("calibrate");},"Hold the controller still for 2 seconds.");
        auto* motion=card("Motion");
        check(motion,"tilt","Use controller tilt (gravity)");check(motion,"invertTilt","Reverse tilt");
        number(motion,"gain","Tilt sensitivity",.25,3,.05);
        number(motion,"deadzone","Gyro deadzone (rad/s)",0,.1,.001);
        check(motion,"keyboardMotion","Keyboard motion: Q / E tilt, R recenter","Press the right stick to recenter in game. Motion settings apply to all players; keyboard motion controls player 1.");
        auto* pointer=card("Pointer source");
        auto* source=select(pointer,"pointer","Pointer source",{T("Mouse"),T("Gyroscope (mouse fallback)"),T("Right stick"),T("Disabled")},{0,1,2,3});
        auto* options=new QWidget;auto* fields=new QVBoxLayout(options);fields->setContentsMargins(0,0,0,0);pointer->addWidget(options);
        pointerOptions=options;number(fields,"sensitivity","Pointer sensitivity",.1,5,.1);
        check(fields,"invertX","Invert pointer X");check(fields,"invertY","Invert pointer Y");
        number(pointer,"stickDeadzone","Pointer stick deadzone",0,.9,.01);
        options->setEnabled(source->currentIndex()==1 || source->currentIndex()==2);
        connect(source,&QComboBox::activated,options,[options](int i){options->setEnabled(i==1 || i==2);});
        action(pointer,"Reset motion settings",[this]{request("resetMotion");});
    }
}
void Window::updateControls() {
    if(!ready)return;
    const int phase=input["phase"].toInt();capturing=phase!=0;
    const bool editable=model["settingsError"].toString().isEmpty();
    if(bindingRows)bindingRows->setEnabled(!capturing && editable);
    if(swap) {swap->setVisible(phase==3);swap->setEnabled(!input["release"].toBool());}
    if(cancel)cancel->setVisible(capturing);
    if(escape)escape->setVisible(phase==1 && input["keyboardCapture"].toBool());
    if(captureText) {
        QString caption=status(input["message"].toString());
        if(input["release"].toBool())caption=T("Release the captured input to continue.");
        else if(phase==1) caption=T(input["keyboardCapture"].toBool()?
              "Press a keyboard key. Escape cancels capture; Use Escape assigns that key.":
              "Press a gamepad button or move a centered stick/trigger. Escape or Cancel stops capture.");
        captureText->setText(caption);captureText->setVisible(!caption.isEmpty());
    }
    const auto rows=model["bindings"].toArray();
    for(int i=0;i<std::min(bindings.size(),rows.size());++i) {
        const auto vals=rows[i].toArray();
        auto update=[&](QPushButton* b,int col) {
            const auto name=status(vals.at(col).toString());b->setText(name);b->setToolTip(name+" · "+T("Change binding"));
        };
        update(bindings[i].first,1);update(bindings[i].second,2);
    }
    if(devices) {
        const auto pads=input["devices"].toArray();
        const QByteArray signature=QJsonDocument(pads).toJson(QJsonDocument::Compact);
        if(devices->property("devices").toByteArray()!=signature) {
            devices->setProperty("devices",signature);devices->clear();
            for(auto pad:pads) {const auto p=pad.toObject();devices->addItem(status(p["name"].toString()),p["id"].toInt());}
            if(pads.isEmpty())devices->addItem(T("No controller connected"),0);
        }
        devices->setEnabled(!pads.isEmpty());devices->setCurrentIndex(std::max(0,devices->findData(input["selected"].toInt())));
        calibrate->setEnabled(editable && input["gyro"].toBool() && input["accel"].toBool() && !input["calibrating"].toBool());
        motionText->setText(input["calibrating"].toBool()?T("Hold the controller still for 2 seconds."):
            (input["motionMessage"].toString().isEmpty()?QString::asprintf(T("Gyroscope: %s    Accelerometer: %s").toUtf8().constData(),
                qUtf8Printable(T(input["gyro"].toBool()?"available":"unavailable")),qUtf8Printable(T(input["accel"].toBool()?"available":"unavailable"))) +
                QString("   ·   %1°").arg(input["tiltAngle"].toDouble(),0,'f',1):status(input["motionMessage"].toString())));
        auto* view=static_cast<PointerView*>(motionMap.data());view->point={input["x"].toDouble(),input["y"].toDouble()};view->update();
    }
}
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
            if(!motionMap || !calibrate || calibrate->toolTip().isEmpty())return false;
            for (auto* spin:sheet->findChildren<QDoubleSpinBox*>()) {
                auto* edit=spin->findChild<QLineEdit*>();
                if (!edit || edit->contentsRect().marginsRemoved(edit->textMargins()).width()
                        < edit->fontMetrics().horizontalAdvance(edit->text())+4) return false;
            }
            controlTab=0;
        }
    }
    return true;
}
}
