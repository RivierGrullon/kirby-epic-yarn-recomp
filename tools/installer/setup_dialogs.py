# SPDX-License-Identifier: GPL-3.0-only
from PyQt6.QtCore import QDir, Qt, QUrl
from PyQt6.QtGui import QDesktopServices, QFileSystemModel
from PyQt6.QtWidgets import (QDialog, QFileDialog, QHBoxLayout, QHeaderView, QLabel,
                             QStackedWidget, QTreeView, QVBoxLayout)

from .setup_theme import FabricButton, button_patch, label


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


def show_folder(parent, title, path):
    if path is None or not path.is_dir():
        parent.statusBar().showMessage(parent.text('Could not open folder'), 5000)
        return
    dialog = QDialog(parent)
    dialog.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
    dialog.setObjectName('folderView')
    dialog.setWindowTitle(parent.text(title))
    dialog.resize(620, 390)
    layout = QVBoxLayout(dialog)
    layout.setContentsMargins(16, 14, 16, 14)
    layout.setSpacing(10)
    heading = QHBoxLayout()
    emblem = QLabel()
    emblem.setPixmap(button_patch('#ed9caf', 32))
    heading.addWidget(emblem)
    heading.addWidget(label(parent.text(title), 'section'), 1)
    layout.addLayout(heading)
    location = label(QDir.toNativeSeparators(str(path)))
    location.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse |
                                     Qt.TextInteractionFlag.TextSelectableByKeyboard)
    layout.addWidget(location)

    contents = QStackedWidget()
    layout.addWidget(contents, 1)
    tree = QTreeView()
    contents.addWidget(tree)
    empty = label(parent.text('This folder is empty.'), 'emptyFolder')
    empty.setAlignment(Qt.AlignmentFlag.AlignCenter)
    contents.addWidget(empty)
    files = QFileSystemModel(dialog)
    files.setReadOnly(True)
    tree.setAccessibleName(parent.text(title))
    tree.setModel(files)
    tree.setRootIndex(files.setRootPath(str(path)))
    tree.setSortingEnabled(True)
    tree.sortByColumn(0, Qt.SortOrder.AscendingOrder)
    tree.setColumnHidden(2, True)
    tree.header().setStretchLastSection(False)
    tree.header().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
    for column in (1, 3):
        tree.header().setSectionResizeMode(column, QHeaderView.ResizeMode.ResizeToContents)

    def refresh_contents():
        contents.setCurrentWidget(tree if files.rowCount(tree.rootIndex()) else empty)

    files.directoryLoaded.connect(refresh_contents)
    files.rowsInserted.connect(refresh_contents)
    files.rowsRemoved.connect(refresh_contents)

    error = label('', 'notice')
    layout.addWidget(error)
    error.hide()

    def open_target(target, failure):
        if not QDesktopServices.openUrl(QUrl.fromLocalFile(str(target))):
            error.setText(parent.text(failure))
            error.show()

    def open_item(index):
        if not files.isDir(index):
            open_target(files.filePath(index), 'Could not open file')

    tree.activated.connect(open_item)
    buttons = QHBoxLayout()
    layout.addLayout(buttons)
    manager = FabricButton(parent.text('Open in file manager'))
    manager.setAutoDefault(False)
    manager.clicked.connect(lambda: open_target(path, 'Could not open folder'))
    buttons.addWidget(manager)
    buttons.addStretch()
    close = FabricButton(parent.text('Close'))
    close.setAutoDefault(False)
    close.clicked.connect(dialog.reject)
    buttons.addWidget(close)
    dialog.open()
