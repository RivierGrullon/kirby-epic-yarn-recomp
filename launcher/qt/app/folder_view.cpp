// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include "theme/patchwork.h"
#include <QBoxLayout>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QLabel>
#include <QStackedWidget>
#include <QTreeView>
#include <QUrl>

namespace Yarn {
void Window::openFolder(const char* title, const QString& path) {
    if (path.isEmpty() || !QDir().mkpath(path)) {
        notice(T("Could not open folder"));
        return;
    }
    const QString folder = QDir(path).absolutePath();
    QDialog dialog(this);
    dialog.setObjectName("folderView");
    dialog.setWindowTitle(T(title));
    dialog.resize(620, 390);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    auto* header = new QHBoxLayout;
    auto* emblem = new QLabel;
    emblem->setPixmap(brandButton());
    header->addWidget(emblem);
    header->addWidget(label(T(title), "section"), 1);
    layout->addLayout(header);
    auto* location = label(QDir::toNativeSeparators(folder));
    location->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(location);

    auto* contents = new QStackedWidget;
    auto* tree = new QTreeView;
    contents->addWidget(tree);
    auto* empty = label(T("This folder is empty."), "emptyFolder");
    empty->setAlignment(Qt::AlignCenter);
    contents->addWidget(empty);
    layout->addWidget(contents, 1);
    auto* files = new QFileSystemModel(&dialog);
    files->setReadOnly(true);
    tree->setAccessibleName(T(title));
    tree->setModel(files);
    tree->setRootIndex(files->setRootPath(folder));
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setSortingEnabled(true);
    tree->sortByColumn(0, Qt::AscendingOrder);
    tree->setColumnHidden(2, true); // Type
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    auto updateContents = [=] {
        contents->setCurrentIndex(files->rowCount(tree->rootIndex()) ? 0 : 1);
    };
    connect(files, &QFileSystemModel::directoryLoaded, contents, updateContents);
    connect(files, &QFileSystemModel::rowsInserted, contents, updateContents);
    connect(files, &QFileSystemModel::rowsRemoved, contents, updateContents);

    auto* error = label({}, "notice");
    error->hide();
    layout->addWidget(error);
    auto open = [error](const QString& target, const char* failure) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(target))) {
            error->setText(T(failure));
            error->show();
        }
    };
    connect(tree, &QTreeView::activated, &dialog, [files, open](const QModelIndex& index) {
        if (!files->isDir(index)) open(files->filePath(index), "Could not open file");
    });
    auto* buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    action(buttons, "Open in file manager", [open, folder] { open(folder, "Could not open folder"); })->setAutoDefault(false);
    buttons->addStretch();
    action(buttons, "Close", [&dialog] { dialog.reject(); })->setAutoDefault(false);
    dialog.exec();
}
} // namespace Yarn
