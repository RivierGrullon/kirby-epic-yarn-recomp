# SPDX-License-Identifier: GPL-3.0-only
from PyQt6.QtWidgets import QDialog, QFileDialog


def choose_path(parent, title, initial='', filters=None):
    dialog = QFileDialog(parent)
    dialog.setOption(QFileDialog.Option.DontUseNativeDialog)
    dialog.setWindowTitle(parent.text(title))
    if initial:
        dialog.setDirectory(initial)
    if filters:
        dialog.setFileMode(QFileDialog.FileMode.ExistingFile)
        dialog.setNameFilters(filters)
    else:
        dialog.setFileMode(QFileDialog.FileMode.Directory)
        dialog.setOption(QFileDialog.Option.ShowDirsOnly)
    dialog.setLabelText(QFileDialog.DialogLabel.LookIn, parent.text('&Look in:'))
    dialog.setLabelText(QFileDialog.DialogLabel.FileType, parent.text('Files of &type:'))
    path = dialog.selectedFiles()[0] if dialog.exec() == QDialog.DialogCode.Accepted else ''
    dialog.deleteLater()
    return path
