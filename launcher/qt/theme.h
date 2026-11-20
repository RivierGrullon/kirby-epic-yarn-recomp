// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QColor>
#include <QFrame>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <array>

namespace Yarn {
enum Page { Home, Install, Display, Controls, Saves, Diagnostics, Basic, ControllerGuide, Count };
inline constexpr std::array Titles{"Play","Install","Display","Controls","Saves","Diagnostics","Basic settings","Controls"};
QString T(const char* key);
void language(int index);
QString status(const QString& text);
QIcon symbol(Page page);
QColor fabricColor(Page page);
QString styleSheet();

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
class SewnSwitch : public QCheckBox {
public:
    SewnSwitch();
protected:
    void paintEvent(QPaintEvent*) override;
    bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }
};
class FabricButton : public QPushButton {
public:
    explicit FabricButton(const QString& text={},bool primary=false);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    bool primary;
};
class FabricCombo : public QComboBox {
public:
    FabricCombo();
    QSize sizeHint() const override;
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
QPixmap brandButton(bool small=false);

}
