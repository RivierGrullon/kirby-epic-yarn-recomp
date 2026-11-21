// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "pages/page.h"
#include <QFrame>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>

class QPainter;

namespace Yarn {
void drawStitch(QPainter& painter, QRectF bounds, QColor color, int radius);
void drawButtonPatch(QPainter& painter, QPointF center, double radius, QColor color);
QPixmap brandButton(bool small=false);

class Banner : public QFrame {
public:
    Banner(Page page, const QString& root, QWidget* parent=nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Page page;
    QPixmap art;
};
class Linen : public QWidget {
protected:
    void paintEvent(QPaintEvent*) override;
};

class LibraryCard : public QPushButton {
public:
    LibraryCard(Page illustration,QString name,QString caption);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Page illustration;
    QString caption;
};
} // namespace Yarn
