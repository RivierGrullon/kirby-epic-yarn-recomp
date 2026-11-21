// SPDX-License-Identifier: GPL-3.0-only
#include "theme/dialogs.h"
#include "app/translations.h"
#include "theme/form_controls.h"
#include <QFileDialog>
#include <QMessageBox>

namespace Yarn {
QString choosePath(QWidget* parent, PathChoice choice, const QString& title, const QString& path) {
    QFileDialog dialog(parent, title, path);
    dialog.setLabelText(QFileDialog::LookIn, T("&Look in:"));
    if (choice == PathChoice::Directory) {
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::ShowDirsOnly);
    } else {
        const bool exporting = choice == PathChoice::ExportSave;
        dialog.setAcceptMode(exporting ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
        dialog.setFileMode(exporting ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
        dialog.setNameFilter(T("Dolphin Wii save (*.bin);;All files (*)"));
    }
    dialog.setLabelText(QFileDialog::FileType, T("Files of &type:"));
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
}

bool confirmAction(QWidget* parent, const QString& title, const QString& message,
                   const QString& action) {
    QMessageBox box(QMessageBox::Warning, title, title, QMessageBox::NoButton, parent);
    box.setObjectName("fabricConfirm");
    box.setTextFormat(Qt::PlainText);
    box.setInformativeText(message);
    auto* proceed = new FabricButton(action);
    auto* cancel = new FabricButton(T("Cancel"));
    box.addButton(proceed, QMessageBox::AcceptRole);
    box.addButton(cancel, QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == proceed;
}
} // namespace Yarn
