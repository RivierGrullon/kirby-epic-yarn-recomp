// SPDX-License-Identifier: GPL-3.0-only
#include "theme/patchwork.h"
#include <QPainter>
#include <QPainterPath>
#include <QDir>
#include <utility>

namespace Yarn {
void drawStitch(QPainter& p, QRectF r, QColor color, int radius) {
    QPen pen(color,1.25,Qt::DashLine,Qt::RoundCap); pen.setDashPattern({3,4});
    p.setPen(pen); p.setBrush(Qt::NoBrush); p.drawRoundedRect(r,radius,radius);
}
void drawButtonPatch(QPainter& p, QPointF c, double r,QColor color) {
    p.setBrush(color); p.setPen(QPen(color.darker(120),1.25)); p.drawEllipse(c,r,r);
    p.setBrush(Qt::NoBrush); p.drawEllipse(c,r-3,r-3);
    p.setPen(QPen(QColor("#fff8eb"),2.2,Qt::SolidLine,Qt::RoundCap));
    for (int i: {-1,1}) p.drawLine(c+QPointF(-r*.22,i*r*.22),c+QPointF(r*.22,-i*r*.22));
}

namespace {
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

QColor fabricColor(Page page) {
    constexpr std::array colors{"#dceee1","#f4e2c4","#f6e5bc","#e0eaf2","#f4dfe1","#e0e7ee","#e2eee1","#e0eaf2"};
    return QColor(colors[page]);
}
QIcon symbol(Page page) {
    QPixmap pix(96,96);pix.setDevicePixelRatio(2);pix.fill(Qt::transparent);
    QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);
    const auto color=fabricColor(page);
    p.setBrush(color);p.setPen(QPen(color.darker(116),1));p.drawRoundedRect(QRectF(1,1,46,46),13,13);
    drawStitch(p,QRectF(4,4,40,40),color.darker(128),9);glyph(p,page);return QIcon(pix);
}
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
        drawStitch(p,QRectF(3,3,42,42),c.darker(145),9);glyph(p,page);p.restore();
        drawButtonPatch(p,{center.x()+42,center.y()-20},10,QColor("#edb3bf"));
        drawButtonPatch(p,{center.x()-45,center.y()+17},7,QColor("#e5c079"));
    }
    drawStitch(p,r.adjusted(7,7,-7,-7),c.darker(127),18);
}

QPixmap brandButton(bool small) {
    const int side=small?18:34;
    QPixmap pix(side*2,side*2);pix.setDevicePixelRatio(2);pix.fill(Qt::transparent);
    QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);
    drawButtonPatch(p,{side/2.,side/2.-1},small?7:14,small?QColor("#8bb69b"):QColor("#ed9caf"));return pix;
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
} // namespace Yarn
