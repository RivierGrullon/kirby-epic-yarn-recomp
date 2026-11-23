// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTabBar>

namespace Yarn {
class YarnTabs : public QTabBar {
public:
    YarnTabs();
protected:
    QSize tabSizeHint(int index) const override;
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
class FabricSpinBox : public QDoubleSpinBox {
public:
    FabricSpinBox();
protected:
    void paintEvent(QPaintEvent*) override;
};
} // namespace Yarn
