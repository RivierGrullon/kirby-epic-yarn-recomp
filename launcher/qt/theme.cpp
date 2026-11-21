// SPDX-License-Identifier: GPL-3.0-only
#include "theme.h"
#include "../language_preference.h"
#include <QPainter>
#include <QPainterPath>
#include <QDir>
#include <QHash>
#include <QListView>
#include <QStyledItemDelegate>
#include <QStyleOptionSpinBox>
#include <cmath>

namespace Yarn {
namespace {
struct Message { const char *en,*zh,*es; };
constexpr Message messages[]{
#include "../strings.inc"
};
int locale=static_cast<int>(Launcher::DefaultLanguage);
void stitch(QPainter& p, QRectF r, QColor color, int radius) {
    QPen pen(color,1.25,Qt::DashLine,Qt::RoundCap); pen.setDashPattern({3,4});
    p.setPen(pen); p.setBrush(Qt::NoBrush); p.drawRoundedRect(r,radius,radius);
}
void button(QPainter& p, QPointF c, double r,QColor color) {
    p.setBrush(color); p.setPen(QPen(color.darker(120),1.25)); p.drawEllipse(c,r,r);
    p.setBrush(Qt::NoBrush); p.drawEllipse(c,r-3,r-3);
    p.setPen(QPen(QColor("#fff8eb"),2.2,Qt::SolidLine,Qt::RoundCap));
    for (int i: {-1,1}) p.drawLine(c+QPointF(-r*.22,i*r*.22),c+QPointF(r*.22,-i*r*.22));
}
void glyph(QPainter& p,Page page) {
    p.setPen(QPen(QColor("#46524b"),2.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
    switch(page) {
    case Home: {
        QPainterPath h;h.moveTo(8,23);h.lineTo(24,10);h.lineTo(40,23);h.moveTo(13,22);h.lineTo(13,38);
        h.lineTo(35,38);h.lineTo(35,22);h.moveTo(21,37);h.lineTo(21,28);h.lineTo(27,28);h.lineTo(27,37);p.drawPath(h);break;
    }
    case Install:
        p.drawRoundedRect(QRectF(10,26,28,11),3,3);p.drawLine(24,10,24,29);
        p.drawLine(18,22,24,29);p.drawLine(30,22,24,29);break;
    case Basic:case Display:
        p.drawRoundedRect(QRectF(8,11,32,23),4,4);p.drawLine(24,35,24,39);p.drawLine(16,39,32,39);break;
    case ControllerGuide:case Controls:
        p.drawRoundedRect(QRectF(7,15,34,20),9,9);p.drawLine(12,24,20,24);p.drawLine(16,20,16,28);
        p.drawPoint(32,21);p.drawPoint(28,26);break;
    case Saves: {
        QPainterPath f;f.moveTo(9,36);f.lineTo(9,13);f.lineTo(21,13);f.lineTo(25,18);f.lineTo(39,18);f.lineTo(39,36);f.closeSubpath();p.drawPath(f);break;
    }
    case Diagnostics:
        p.drawEllipse(QRectF(9,9,22,22));p.drawLine(28,28,39,39);p.drawLine(14,20,18,20);p.drawLine(18,20,21,16);p.drawLine(21,16,25,22);break;
    default:break;
    }
}
}
QString T(const char* key) {
    static const auto lookup=[] { QHash<QByteArray,const Message*> map;
        for (const auto& m:messages) map.insert(m.en,&m); return map; }();
    const auto* m=lookup.value(key,nullptr);
    return QString::fromUtf8(!m || locale==1?key:locale==2?m->es:m->zh);
}
void language(int value) { locale=value; }
QString status(const QString& value) {
    for (const auto prefix:{"Data verification failed: ","Already assigned to ","Missing ","Could not start the native executable: "})
        if (value.startsWith(QLatin1String(prefix))) return T(prefix)+status(value.mid(qstrlen(prefix)));
    return T(value.toUtf8().constData());
}
QColor fabricColor(Page page) {
    constexpr std::array colors{"#dceee1","#f4e2c4","#f6e5bc","#e0eaf2","#f4dfe1","#e0e7ee","#e2eee1","#e0eaf2"};
    return QColor(colors[page]);
}
QIcon symbol(Page page) {
    QPixmap pix(96,96);pix.setDevicePixelRatio(2);pix.fill(Qt::transparent);
    QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);
    const auto color=fabricColor(page);
    p.setBrush(color);p.setPen(QPen(color.darker(116),1));p.drawRoundedRect(QRectF(1,1,46,46),13,13);
    stitch(p,QRectF(4,4,40,40),color.darker(128),9);glyph(p,page);return QIcon(pix);
}
void Linen::paintEvent(QPaintEvent*) {
    QPainter p(this);p.fillRect(rect(),QColor("#f8f4e9"));
    p.setPen(QColor(152,118,76,15));
    for(int x=0;x<width();x+=7)p.drawLine(x,0,x,height());
    for(int y=0;y<height();y+=7)p.drawLine(0,y,width(),y);
}
Banner::Banner(Page which,const QString& root,QWidget* parent):QFrame(parent),page(which) {
    if(page==Home) {
        art.load(QDir(root).filePath("imgs/Kirbys_Epic_Yarn_Concept-Art.png"));
        if(art.isNull())art.load(":/yarn/mascot.png");
    }
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
}
void Banner::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
    QRectF r=QRectF(rect()).adjusted(1,1,-1,-3);
    auto c=fabricColor(page);
    QPainterPath shape;shape.addRoundedRect(r,23,23);
    p.fillPath(shape.translated(0,2),c.darker(118));p.fillPath(shape,c);p.setClipPath(shape);
    p.setPen(QPen(QColor(70,110,85,15),1.5));
    for(int i=-height();i<width()+height();i+=32) {
        p.drawLine(i,0,i+height(),height());p.drawLine(i,height(),i+height(),0);
    }
    if(page==Home) {
        const QSizeF size=art.size().scaled(int(width()*.59),int(height()*1.065),Qt::KeepAspectRatio);
        p.drawPixmap(QRectF(width()-size.width()-10,height()-size.height()-4,size.width(),size.height()),art,QRectF(art.rect()));
    } else {
        const QPointF center(width()-87,height()/2.0);
        p.save();p.translate(center);p.rotate(7);p.translate(-34,-34);p.scale(68./48.,68./48.);
        p.setBrush(QColor("#fff9eb"));p.setPen(QPen(c.darker(120),1));p.drawRoundedRect(QRectF(0,0,48,48),12,12);
        stitch(p,QRectF(3,3,42,42),c.darker(145),9);glyph(p,page);p.restore();
        button(p,{center.x()+42,center.y()-20},10,QColor("#edb3bf"));
        button(p,{center.x()-45,center.y()+17},7,QColor("#e5c079"));
    }
    stitch(p,r.adjusted(7,7,-7,-7),c.darker(127),18);
}

QString styleSheet() {return R"(
QWidget {color:#39483e; font-size:15px;}
QLabel {background:transparent;}
QLabel#brand {font-family:Georgia,serif;font-weight:bold;font-size:24px;color:#30483f;}
QLabel#kicker {font:italic 28px Georgia,serif;color:#546e5c;}
QLabel#title {font-size:35px;font-weight:bold;color:#344e3f;}
QLabel#homeTitle {font-size:68px;font-weight:bold;color:#314c40;}
QLabel#edition {font:14px Consolas,monospace;color:#597060;}
QLabel#libraryTitle {font-size:23px;font-weight:bold;color:#36463e;}
QLabel#section {font-weight:600;font-size:18px;}
QLabel#soft {color:#657469;font-size:13px;}
QLabel#notice {background:#fbe3d7;color:#70432f;padding:7px;border-radius:8px;}
QFrame#card {background:#fffdf6;border:1px solid #dbd6c5;border-radius:16px;}
QPushButton,QComboBox,QDoubleSpinBox {background:#fffdf7;border:1px solid #cfcbb9;border-radius:9px;padding:6px 12px;min-height:22px;}
QPushButton:hover,QComboBox:hover {background:#f7e8e9;border-color:#c88fa2;}
QPushButton:focus,QComboBox:focus,QSlider:focus,QDoubleSpinBox:focus,QCheckBox:focus {border:2px solid #aa6680;}
QPushButton:disabled,QComboBox:disabled {color:#92958a;background:#eeede4;}
QPushButton#nav {background:transparent;border:0;border-bottom:3px solid transparent;border-radius:0;padding:5px 8px;color:#59665d;}
QPushButton#nav:focus {background:#f2eade;}
QPushButton#nav:checked {border-bottom:3px solid #b76781;color:#493f43;}
QPushButton#nav:hover {color:#964d65;}
QPushButton#help {color:#65766b;background:#f1f1e6;border:1px dashed #aab6a4;border-radius:11px;padding:0;min-height:0;}
QScrollArea,QWidget#paper,QWidget#viewport {background:transparent;border:0;}
QScrollBar:vertical {background:transparent;width:9px;} QScrollBar::handle:vertical {background:#c7ccba;min-height:28px;border-radius:4px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
QSlider::groove:horizontal {background:#dde2cf;border-radius:3px;height:6px;}
QSlider::sub-page:horizontal {background:#ce889f;border-radius:3px;}
QSlider::handle:horizontal {background:#c67691;border:2px solid #f8e0e5;width:15px;margin:-6px 0;border-radius:8px;}
QToolTip {background:#fff9ea;color:#38463d;border:1px solid #c3baa2;padding:5px;}
QComboBox QAbstractItemView {background:#fffaf0;selection-background-color:#efd1d9;selection-color:#423e39;}
QComboBox::drop-down {border:0;width:24px;}
QDoubleSpinBox#fabricNumber, QDoubleSpinBox#fabricNumber:focus {
    /* Qt already subtracts both step buttons from the editor's width. */
    background:transparent;border:0;padding:5px;min-height:30px;
}
QDoubleSpinBox#fabricNumber::down-button {
    subcontrol-origin:border;subcontrol-position:center left;left:5px;width:26px;height:28px;
}
QDoubleSpinBox#fabricNumber::up-button {
    subcontrol-origin:border;subcontrol-position:center right;right:5px;width:26px;height:28px;
}
QDoubleSpinBox#fabricNumber QLineEdit {background:transparent;border:0;padding:0;}
QPlainTextEdit {background:#f5f4e9;border:1px solid #d8d5c6;border-radius:9px;padding:8px;}
QTabBar::tab {background:#edeedc;border-radius:8px;padding:7px 15px;margin-right:5px;}
QTabBar::tab:selected {background:#f0d3de;color:#713e50;}
)"; }

SewnSwitch::SewnSwitch() {
    setFixedSize(58,30);setCursor(Qt::PointingHandCursor);setFocusPolicy(Qt::StrongFocus);
}
void SewnSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    if(!isEnabled())p.setOpacity(.55);
    const QColor cloth=isChecked()?QColor("#bcdac5"):QColor("#e7e5d8");
    p.setPen(QPen(hasFocus()?QColor("#a35e7c"):cloth.darker(119),hasFocus()?2:1));
    p.setBrush(cloth);p.drawRoundedRect(QRectF(1,1,56,28),14,14);
    stitch(p,QRectF(4,4,50,22),cloth.darker(123),11);
    button(p,QPointF(isChecked()?42:16,15),10,isChecked()?QColor("#77ad8a"):QColor("#bcbaaa"));
}
QPixmap brandButton(bool small) {
    const int side=small?18:34;
    QPixmap pix(side*2,side*2);pix.setDevicePixelRatio(2);pix.fill(Qt::transparent);
    QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);
    button(p,{side/2.,side/2.-1},small?7:14,small?QColor("#8bb69b"):QColor("#ed9caf"));return pix;
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
    stitch(p,r.adjusted(5,5,-5,-5),primary?QColor("#f2c1d0"):QColor("#c5bca7"),radius-4);
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
    stitch(p,r.adjusted(4,4,-4,-4),QColor("#d9cfb8"),7);
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
    stitch(p,cloth.adjusted(4,4,-4,-4),QColor("#d9cfb8"),7);

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
LibraryCard::LibraryCard(Page image,QString title,QString description)
    :QPushButton(title),illustration(image),caption(std::move(description)) {
    setCursor(Qt::PointingHandCursor);setToolTip(caption);setAccessibleDescription(caption);
    setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);setMinimumHeight(96);
}
QSize LibraryCard::sizeHint() const {return {260,96};}
void LibraryCard::paintEvent(QPaintEvent*) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    auto r=QRectF(rect()).adjusted(1,1,-1,-2);p.setBrush(underMouse()?QColor("#fcf4ea"):QColor("#fffdf5"));
    p.setPen(QPen(hasFocus()?QColor("#b87f94"):QColor("#d9d5c8"),hasFocus()?2:1));p.drawRoundedRect(r,15,15);
    const bool small=width()<230;
    const int iconSize=small?39:61;const int x=small?9:16;
    symbol(illustration).paint(&p,QRect(x,(height()-iconSize)/2,iconSize,iconSize));
    const int left=x+iconSize+(small?7:13);
    auto space=QRect(left,0,width()-left-24,height());
    auto f=font();f.setPixelSize(small?15:19);f.setBold(true);p.setFont(f);p.setPen(QColor("#35433a"));
    p.drawText(space.adjusted(0,small?0:18,0,0),small?Qt::AlignVCenter:Qt::AlignTop,
               p.fontMetrics().elidedText(text(),Qt::ElideRight,space.width()));
    if(!small){f.setPixelSize(13);f.setBold(false);p.setFont(f);p.setPen(QColor("#657264"));
        p.drawText(space.adjusted(0,51,0,0),Qt::AlignTop,p.fontMetrics().elidedText(caption,Qt::ElideRight,space.width()));}
    const QPointF c(width()-19,height()/2.);p.setPen(QPen(QColor("#839383"),1.7,Qt::SolidLine,Qt::RoundCap));
    p.drawPolyline(QPolygonF{{c.x()-2,c.y()-4},{c.x()+2,c.y()},{c.x()-2,c.y()+4}});
}
}
