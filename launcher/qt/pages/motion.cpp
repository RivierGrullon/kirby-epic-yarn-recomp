// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include "theme/form_controls.h"
#include <QBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <algorithm>

namespace Yarn {
namespace {
class PointerView final : public QWidget {
public:
    QPointF point;
    bool showPoint=false,showTilt=false;
    double tiltDegrees=0;
    QString placeholder;
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        QRectF bounds=QRectF(rect()).adjusted(4,4,-4,-4);
        p.setBrush(QColor("#edf2e8"));p.setPen(QPen(QColor("#b6c6b1"),1,Qt::DashLine));p.drawRoundedRect(bounds,11,11);
        if (!placeholder.isEmpty()) {
            p.setPen(QColor("#59695e"));
            p.drawText(bounds,Qt::AlignCenter,placeholder);
            return;
        }
        p.drawLine(QPointF(bounds.center().x(),bounds.top()),QPointF(bounds.center().x(),bounds.bottom()));
        p.drawLine(QPointF(bounds.left(),bounds.center().y()),QPointF(bounds.right(),bounds.center().y()));
        if (showTilt) {
            p.save();
            p.translate(bounds.center());
            p.rotate(tiltDegrees);
            const qreal radius=std::min(bounds.width(),bounds.height())*.40;
            p.setPen(QPen(QColor("#62896e"),3,Qt::SolidLine,Qt::RoundCap));
            p.drawLine(QPointF(-radius,0),QPointF(radius,0));
            p.setBrush(QColor("#aac7a9"));
            p.drawEllipse(QPointF(radius,0),3,3);
            p.restore();
        }
        if (showPoint) {
            p.setBrush(QColor("#c77994"));p.setPen(QPen(QColor("#985775"),1));
            p.drawEllipse(QPointF(bounds.center().x()+point.x()*(bounds.width()/2-7),
                                  bounds.center().y()+point.y()*(bounds.height()/2-7)),6,6);
        }
    }
};
}

void Window::buildMotion() {
    auto* panel=card("Gyroscope & gravity / tilt","Use a controller with motion sensors exposed to SDL. Xbox-emulated devices may hide them.");
    motionUi.devices=new FabricCombo;row(panel,T("Preview controller"),motionUi.devices);
    connect(motionUi.devices,&QComboBox::activated,this,[this](int i){request("pad",{{"id",motionUi.devices->itemData(i).toInt()}});});
    auto* preview=new PointerView;preview->setMinimumHeight(82);panel->addWidget(preview);motionUi.map=preview;
    preview->setToolTip(T("The line shows tilt. The dot follows gyro motion, or the right stick when selected."));
    preview->setAccessibleDescription(preview->toolTip());
    motionUi.message=new QLabel;motionUi.message->setWordWrap(true);panel->addWidget(motionUi.message);
    auto* buttons=new QHBoxLayout;panel->addLayout(buttons);
    action(buttons,"Recenter preview",[this]{request("recenter");});
    motionUi.calibrate=action(buttons,"Calibrate gyro",[this]{request("calibrate");},"Hold the controller still for 2 seconds.");
    auto* motion=card("Motion");
    check(motion,"tilt","Use controller tilt (gravity)");check(motion,"invertTilt","Reverse tilt");
    number(motion,"gain","Tilt sensitivity",.25,3,.05);
    number(motion,"deadzone","Gyro deadzone (rad/s)",0,.1,.001);
    check(motion,"keyboardMotion","Keyboard motion: Q / E tilt, R recenter","Press the right stick to recenter in game. Motion settings apply to all players; keyboard motion controls player 1.");
    auto* pointer=card("Pointer source");
    auto* source=select(pointer,"pointer","Pointer source",{T("Mouse"),T("Gyroscope (mouse fallback)"),T("Right stick"),T("Disabled")},{0,1,2,3});
    auto* options=new QWidget;auto* fields=new QVBoxLayout(options);fields->setContentsMargins(0,0,0,0);pointer->addWidget(options);
    number(fields,"sensitivity","Pointer sensitivity",.1,5,.1);
    check(fields,"invertX","Invert pointer X");check(fields,"invertY","Invert pointer Y");
    number(pointer,"stickDeadzone","Pointer stick deadzone",0,.9,.01);
    options->setEnabled(source->currentIndex()==1 || source->currentIndex()==2);
    connect(source,&QComboBox::activated,options,[options](int i){options->setEnabled(i==1 || i==2);});
    action(pointer,"Reset motion settings",[this]{request("resetMotion");});
}

void Window::updateMotion(bool editable) {
    const auto pads=input["devices"].toArray();
    const QByteArray signature=QJsonDocument(pads).toJson(QJsonDocument::Compact);
    if(motionUi.devices->property("devices").toByteArray()!=signature) {
        motionUi.devices->setProperty("devices",signature);motionUi.devices->clear();
        for(auto pad:pads) {const auto p=pad.toObject();motionUi.devices->addItem(status(p["name"].toString()),p["id"].toInt());}
        if(pads.isEmpty())motionUi.devices->addItem(T("No controller connected"),0);
    }
    motionUi.devices->setEnabled(!pads.isEmpty());motionUi.devices->setCurrentIndex(std::max(0,motionUi.devices->findData(input["selected"].toInt())));
    motionUi.calibrate->setEnabled(editable && input["gyro"].toBool() && input["accel"].toBool() && !input["calibrating"].toBool());
    auto* view=static_cast<PointerView*>(motionUi.map.data());
    const bool gyro=input["gyro"].toBool(), accel=input["accel"].toBool();
    const bool paused=input["previewPaused"].toBool();
    const bool rightStick=config["pointer"].toInt()==2;
    const QPointF axes(input["x"].toDouble(),input["y"].toDouble());
    // Positive gyro yaw/pitch aims left/up. Qt screen and SDL stick axes grow right/down.
    // The host has already applied the user's per-axis inversion.
    view->point=rightStick ? axes : -axes;
    view->tiltDegrees=input["tiltAngle"].toDouble();
    view->showPoint=gyro || (!pads.isEmpty() && rightStick);
    view->showTilt=accel;
    view->placeholder.clear();
    if (pads.isEmpty()) view->placeholder=T("No controller connected");
    else if (paused) view->placeholder=T("Motion preview paused");
    else if (!view->showPoint && !accel) view->placeholder=T("No motion sensor data");
    view->update();

    QString live=QString::asprintf(T("Gyroscope: %s    Accelerometer: %s").toUtf8().constData(),
        qUtf8Printable(T(gyro?"available":"unavailable")),qUtf8Printable(T(accel?"available":"unavailable")));
    if (accel) live+=QString("   ·   %1°").arg(view->tiltDegrees,0,'f',1);
    if (input["calibrating"].toBool()) live+="\n"+T("Hold the controller still for 2 seconds.");
    else if (!input["motionMessage"].toString().isEmpty()) live+="\n"+status(input["motionMessage"].toString());
    motionUi.message->setText(live);
    motionUi.message->setVisible(!paused && !pads.isEmpty());
}
} // namespace Yarn
