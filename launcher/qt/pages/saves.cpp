// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include "theme/dialogs.h"
#include <QBoxLayout>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>

namespace Yarn {
void Window::buildSaves() {
    auto* box=card(model["saveFound"].toBool()?"Game save found":"No game save yet",
                   "The game manages its own saves. Choosing another DATA folder does not move them.");
    field(box,"Save files",model["saves"].toString());
    auto* buttons=new QHBoxLayout;box->addLayout(buttons);
    action(buttons,"Open Saves",[this]{openFolder(model["saves"].toString());});
    action(buttons,"Import Dolphin save",[this]{
        const auto folder=choosePath(this,PathChoice::Directory,T("Choose Dolphin save folder"),QDir(root).filePath("local"));
        if(!folder.isEmpty() && confirmReplace())request("importSave",{{"path",folder}});
    })->setEnabled(!model["running"].toBool());
    action(buttons,"Import save (.bin)",[this]{
        const auto file=choosePath(this,PathChoice::OpenSave,T("Choose Dolphin save (.bin)"),QDir(root).filePath("local"));
        if(!file.isEmpty() && confirmReplace())request("importSave",{{"path",file}});
    })->setEnabled(!model["running"].toBool());
    auto* exportButtons=new QHBoxLayout;box->addLayout(exportButtons);
    action(exportButtons,"Export save (.bin)",[this]{
        const auto file=choosePath(this,PathChoice::ExportSave,T("Choose export file"),QDir(root).filePath("RK5E01-save.bin"));
        if(!file.isEmpty())request("exportSave",{{"path",file},{"folder",false}});
    })->setEnabled(!model["running"].toBool() && model["saveFound"].toBool());
    action(exportButtons,"Export save folder",[this]{
        const auto folder=choosePath(this,PathChoice::Directory,T("Choose export folder"),QDir(root).filePath("local"));
        if(!folder.isEmpty())request("exportSave",{{"path",folder},{"folder",true}});
    })->setEnabled(!model["running"].toBool() && model["saveFound"].toBool());
    importSaveState=label({},"soft");importSaveState->hide();box->addWidget(importSaveState);

    auto* backups=card("Backups","Imports and restores keep a copy of the previous save here.");
    const auto backupList=model["backups"].toArray();
    if(backupList.isEmpty()) {
        backups->addWidget(label(T("No backups yet."),"soft"));
    } else {
        for(const auto& item:backupList) {
            const auto b=item.toObject();
            auto* row=new QWidget;auto* lay=new QHBoxLayout(row);lay->setContentsMargins(0,2,0,2);
            lay->addWidget(label(b["timestamp"].toString()),1);
            action(lay,"Restore",[this,b]{
                if(confirmReplace())request("restoreBackup",{{"name",b["name"].toString()}});
            })->setEnabled(!model["running"].toBool());
            action(lay,"Delete",[this,b]{
                request("deleteBackup",{{"name",b["name"].toString()}});
            })->setEnabled(!model["running"].toBool());
            backups->addWidget(row);
        }
    }
}

bool Window::confirmReplace() {
    return confirmAction(this, T("Replace save"),
        T("This will replace your current save. A backup of the current save will be made first."),
        T("Replace"));
}
} // namespace Yarn
