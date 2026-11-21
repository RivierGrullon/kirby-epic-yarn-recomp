// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include <QBoxLayout>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>

namespace Yarn {
void Window::buildInstall() {
    auto* data=card("Game data","Choose the extracted DATA folder from your own RK5E01 Rev 0 (USA) copy, then open Play.");
    dataState=label({});data->addWidget(dataState);field(data,"Game data folder",model["data"].toString());
    if(ready && !model["verified"].toBool())data->addWidget(label(status(model["dataStatus"].toString()),"notice"));
    auto* buttons=new QHBoxLayout;data->addLayout(buttons);
    action(buttons,"Choose DATA folder",[this]{
        const auto folder=QFileDialog::getExistingDirectory(this,T("Choose DATA folder"),model["data"].toString());
        if(!folder.isEmpty())request("data",{{"path",folder}});
    })->setEnabled(!model["running"].toBool());
    action(buttons,"Verify selected folder",[this]{request("verify");});
    auto* setup=card("Installed game","Kirby Setup handles installation and updates in a separate window.");
    if(!model["installationError"].toString().isEmpty())setup->addWidget(label(model["installationError"].toString(),"notice"));
    if(!model["setupAvailable"].toBool())setup->addWidget(label(T("Open your Kirby Setup AppImage or Windows setup application to install the game."),"soft"));
    action(setup,"Open Kirby Setup",[this]{request("setup");})->setEnabled(model["setupAvailable"].toBool() && !model["running"].toBool());
}
} // namespace Yarn
