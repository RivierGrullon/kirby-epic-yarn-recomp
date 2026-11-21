// SPDX-License-Identifier: GPL-3.0-only
#include "theme/form_controls.h"
#include "theme/patchwork.h"
#include <QPainter>
#include <QListView>
#include <QStyledItemDelegate>
#include <QStyleOptionSpinBox>
#include <algorithm>

namespace Yarn {
SewnSwitch::SewnSwitch() {
    setFixedSize(58,30);setCursor(Qt::PointingHandCursor);setFocusPolicy(Qt::StrongFocus);
}
void SewnSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    if(!isEnabled())p.setOpacity(.55);
    const QColor cloth=isChecked()?QColor("#bcdac5"):QColor("#e7e5d8");
    p.setPen(QPen(hasFocus()?QColor("#a35e7c"):cloth.darker(119),hasFocus()?2:1));
    p.setBrush(cloth);p.drawRoundedRect(QRectF(1,1,56,28),14,14);
    drawStitch(p,QRectF(4,4,50,22),cloth.darker(123),11);
    drawButtonPatch(p,QPointF(isChecked()?42:16,15),10,isChecked()?QColor("#77ad8a"):QColor("#bcbaaa"));
}

FabricButton::FabricButton(const QString& name,bool main):QPushButton(name),primary(main) {
    setCursor(Qt::PointingHandCursor);setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
    setMinimumHeight(main?64:39);
}
QSize FabricButton::sizeHint() const {
    return {fontMetrics().horizontalAdvance(text())+(primary?85:36),primary?66:40};
}
void FabricButton::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    const bool enabled=isEnabled();
    QColor fill=primary?QColor("#bf607f"):QColor("#f5efdf");
    QColor edge=primary?QColor("#a75470"):QColor("#cac2aa");
    if(!enabled){fill=QColor("#eeeae0");edge=QColor("#d4d0c4");}
    else if(isDown() || isChecked()){fill=primary?QColor("#a34d68"):QColor("#e3ebdc");}
    else if(underMouse()){fill=primary?QColor("#b55575"):QColor("#f6e5e6");}
    const int radius=primary?17:10;
    QRectF r=QRectF(rect()).adjusted(1,isDown()?3:1,-1,primary?-5:-3);
    p.setPen(Qt::NoPen);p.setBrush(edge);p.drawRoundedRect(r.translated(0,primary?4:2),radius,radius);
    p.setPen(QPen(hasFocus()?QColor("#9b5670"):edge,hasFocus()?2:1));p.setBrush(fill);p.drawRoundedRect(r,radius,radius);
    drawStitch(p,r.adjusted(5,5,-5,-5),primary?QColor("#f2c1d0"):QColor("#c5bca7"),radius-4);
    QFont f=font();if(primary){f.setPixelSize(22);f.setBold(true);}p.setFont(f);
    const QColor ink=enabled?(primary?QColor("#fff9ef"):QColor("#3d5143")):QColor("#888d80");
    auto area=r.adjusted(10,0,-10,0);
    if(primary){
        const auto center=r.center();const qreal labelWidth=p.fontMetrics().horizontalAdvance(text());
        const qreal x=center.x()-(labelWidth+27)/2;
        p.setPen(Qt::NoPen);p.setBrush(ink);
        p.drawPolygon(QPolygonF{{x,center.y()-8},{x,center.y()+8},{x+12,center.y()}});
        area.setLeft(x+25);p.setPen(ink);p.drawText(area,Qt::AlignLeft|Qt::AlignVCenter,text());
    } else {p.setPen(ink);p.drawText(area,Qt::AlignCenter,p.fontMetrics().elidedText(text(),Qt::ElideRight,int(area.width())));}
}

namespace {
class FabricChoice final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& o,const QModelIndex& i) const override {
        auto s=QStyledItemDelegate::sizeHint(o,i);return {s.width()+34,std::max(s.height()+10,34)};
    }
    void paint(QPainter* p,const QStyleOptionViewItem& o,const QModelIndex& i) const override {
        p->save();p->setRenderHint(QPainter::Antialiasing);
        QRect r=o.rect.adjusted(3,1,-3,-1);
        if(o.state & QStyle::State_Selected) {
            p->setPen(Qt::NoPen);p->setBrush(QColor("#f4dfe4"));p->drawRoundedRect(r,7,7);
            p->setBrush(QColor("#b76c88"));p->drawEllipse(QPointF(r.left()+10,r.center().y()),3,3);
        }
        p->setPen(QColor("#3e5145"));p->drawText(r.adjusted(20,0,-8,0),Qt::AlignVCenter,i.data().toString());
        p->restore();
    }
};
}
FabricCombo::FabricCombo() {
    setCursor(Qt::PointingHandCursor);setMinimumHeight(39);
    auto* list=new QListView;list->setMouseTracking(true);
    list->setStyleSheet("QListView {background:#fffbf1;border:1px solid #cfc6ae;padding:3px;outline:0;}");
    setView(list);setItemDelegate(new FabricChoice(this));
}
QSize FabricCombo::sizeHint() const {
    auto s=QComboBox::sizeHint();return {s.width()+12,std::max(40,s.height())};
}
void FabricCombo::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    QRectF r=QRectF(rect()).adjusted(1,1,-1,-2);
    p.setBrush(isEnabled()?QColor("#fffcf1"):QColor("#eeeae0"));
    p.setPen(QPen(hasFocus()?QColor("#b4778d"):QColor("#cbc2aa"),hasFocus()?2:1));
    p.drawRoundedRect(r,10,10);
    drawStitch(p,r.adjusted(4,4,-4,-4),QColor("#d9cfb8"),7);
    const QPointF c(r.right()-17,r.center().y());
    p.setBrush(QColor("#dcead9"));p.setPen(Qt::NoPen);p.drawEllipse(c,10,10);
    p.setPen(QPen(QColor("#5b7160"),1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p.drawPolyline(QPolygonF{{c.x()-4,c.y()-1.5},{c.x(),c.y()+2.5},{c.x()+4,c.y()-1.5}});
    auto space=r.adjusted(13,0,-32,0);p.setPen(QColor("#405347"));
    p.drawText(space,Qt::AlignVCenter,p.fontMetrics().elidedText(currentText(),Qt::ElideRight,int(space.width())));
}
FabricSpinBox::FabricSpinBox() {
    setObjectName("fabricNumber");
    setAlignment(Qt::AlignCenter);
    setButtonSymbols(QAbstractSpinBox::PlusMinus);
    setMinimumWidth(136);
    setAttribute(Qt::WA_Hover);
}

void FabricSpinBox::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF cloth=QRectF(rect()).adjusted(1,1,-1,-2);
    p.setBrush(isEnabled()?QColor("#fffcf1"):QColor("#eeeae0"));
    p.setPen(QPen(hasFocus()?QColor("#b4778d"):QColor("#cbc2aa"),hasFocus()?2:1));
    p.drawRoundedRect(cloth,10,10);
    drawStitch(p,cloth.adjusted(4,4,-4,-4),QColor("#d9cfb8"),7);

    // Draw over Qt's native step-button regions; Qt handles clicks, repeats and the editor.
    QStyleOptionSpinBox opt;
    initStyleOption(&opt);
    for (auto side : {QStyle::SC_SpinBoxDown,QStyle::SC_SpinBoxUp}) {
        const QRect hit=style()->subControlRect(QStyle::CC_SpinBox,&opt,side,this);
        const bool plus=side==QStyle::SC_SpinBoxUp;
        const bool enabled=isEnabled() && opt.stepEnabled.testFlag(plus?StepUpEnabled:StepDownEnabled);
        QColor fill(plus?"#d4e7d7":"#f1d6de");
        if (!enabled) fill=QColor("#e6e4d9");
        else if (opt.activeSubControls.testFlag(side) && opt.state.testFlag(QStyle::State_Sunken))
            fill=fill.darker(112);
        else if (underMouse() && hit.contains(mapFromGlobal(QCursor::pos()))) fill=fill.darker(105);
        const QPointF c=QRectF(hit).center();
        p.setPen(QPen(fill.darker(120),1));
        p.setBrush(fill);
        p.drawEllipse(c,10.5,10.5);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c,8,8);
        const QColor ink=enabled?QColor(plus?"#41674c":"#81485d"):QColor("#a4a397");
        p.setPen(QPen(ink,1.8,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(c+QPointF(-3.5,0),c+QPointF(3.5,0));
        if (plus) p.drawLine(c+QPointF(0,-3.5),c+QPointF(0,3.5));
    }
}
} // namespace Yarn
