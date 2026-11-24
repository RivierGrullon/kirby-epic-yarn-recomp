// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include "theme/dialogs.h"
#include <QBoxLayout>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace Yarn {
void Window::buildSaves() {
    auto* box=card(model["saveFound"].toBool()?"Game save found":"No game save yet",
                   "The game manages its own saves. Choosing another DATA folder does not move them.");
    field(box,"Save files",model["saves"].toString());
    const auto direction=narrow?QBoxLayout::TopToBottom:QBoxLayout::LeftToRight;
    auto* buttons=new QBoxLayout(direction);box->addLayout(buttons);
    action(buttons,"Open Saves",[this]{openFolder("Save files",model["saves"].toString());});
    action(buttons,"Import Dolphin save",[this]{
        const auto folder=choosePath(this,PathChoice::Directory,T("Choose Dolphin save folder"),QDir(root).filePath("local"));
        if(!folder.isEmpty() && confirmReplace())request("importSave",{{"path",folder}});
    })->setEnabled(!model["running"].toBool());
    action(buttons,"Import save (.bin)",[this]{
        const auto file=choosePath(this,PathChoice::OpenSave,T("Choose Dolphin save (.bin)"),QDir(root).filePath("local"));
        if(!file.isEmpty() && confirmReplace())request("importSave",{{"path",file}});
    })->setEnabled(!model["running"].toBool());
    auto* exportButtons=new QBoxLayout(direction);box->addLayout(exportButtons);
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
    backups->addWidget(label(T("Named backups are kept until you delete them. Close the game before creating one."),"soft"));
    auto* backupName=new QLineEdit;
    backupName->setObjectName("backupName");
    backupName->setMaxLength(64);
    backupName->setPlaceholderText(T("e.g. Before Patch Castle"));
    backupName->setEnabled(!model["running"].toBool() && model["saveFound"].toBool());
    auto* nameRow=row(backups,T("Backup name"),backupName,
        "Copies saved progress, not your current position in a level.");
    auto* nameLayout=static_cast<QHBoxLayout*>(nameRow->layout());
    nameLayout->setStretch(0,0);nameLayout->setStretch(1,1);
    auto* createButtons=new QHBoxLayout;backups->addLayout(createButtons);
    auto* create=action(createButtons,"Create backup",[this,backupName]{
        request("createBackup",{{"label",backupName->text().trimmed()}});
    });
    create->setObjectName("createBackup");
    create->setEnabled(false);createButtons->addStretch();
    connect(backupName,&QLineEdit::textChanged,this,[this,create](const QString& name){
        create->setEnabled(!model["running"].toBool() && model["saveFound"].toBool() && !name.trimmed().isEmpty());
    });
    connect(backupName,&QLineEdit::returnPressed,create,&QPushButton::click);
    const auto backupList=model["backups"].toArray();
    if(backupList.isEmpty()) {
        backups->addWidget(label(T("No backups yet."),"soft"));
    } else {
        for(const auto& item:backupList) {
            const auto b=item.toObject();
            auto* row=new QWidget;auto* lay=new QHBoxLayout(row);lay->setContentsMargins(0,2,0,2);
            auto* details=new QVBoxLayout;
            if(!b["label"].toString().isEmpty())details->addWidget(label(b["label"].toString()));
            details->addWidget(label(b["timestamp"].toString(),"soft"));
            lay->addLayout(details,1);
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
