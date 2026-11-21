// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QString>

class QWidget;

namespace Yarn {
enum class PathChoice { Directory, OpenSave, ExportSave };
QString choosePath(QWidget* parent, PathChoice choice, const QString& title, const QString& path);
bool confirmAction(QWidget* parent, const QString& title, const QString& message,
                   const QString& action);
}
