// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include "app/translations.h"
#include <QBoxLayout>
#include <QDesktopServices>
#include <QDir>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>

namespace Yarn {
void Window::buildDiagnostics() {
    auto* d=card("Build information");QJsonObject runtime=model["runtime"].toObject();
    for(auto name:{"build","runtime"}) {
        const auto b=model[name].toObject();
        if(!b.isEmpty())field(d,QString(name)=="build"?"Launcher":"Native game",
            b["revision"].toString().left(12)+"   ·   "+b["compiler"].toString());
    }
    if(!runtime.isEmpty())d->addWidget(label(T(model["match"].toBool()?"Launcher and runtime build records match.":"Launcher and runtime build records differ."),"soft"));
    if(!model["buildError"].toString().isEmpty())d->addWidget(label(model["buildError"].toString(),"notice"));
    auto* detail=new QPlainTextEdit;detail->setReadOnly(true);detail->setMaximumHeight(180);
    detail->setPlainText(QString::fromUtf8(QJsonDocument(QJsonObject{{"launcher",model["build"]},{"runtime",model["runtime"]}}).toJson()));
    auto* details=action(d,"Source and dependency details",[detail]{detail->setVisible(!detail->isVisible());});
    d->addWidget(detail);detail->hide();details->setCheckable(true);
    action(d,"Refresh build information",[this]{request("refresh");})->setEnabled(!model["checking"].toBool()&&!model["running"].toBool());
    auto* files=card("Shared settings");field(files,"Native executable",model["executable"].toString());
    field(files,"Shared settings",model["settings"].toString());
    if(!model["settingsError"].toString().isEmpty())files->addWidget(label(model["settingsError"].toString(),"notice"));
    action(files,model["dirty"].toBool()?"Discard changes and reload":"Reload settings",[this]{request("reload");});
    auto* shortcuts=new QVBoxLayout;files->addLayout(shortcuts);
    action(shortcuts,"Open Settings Folder",[this]{openFolder("Shared settings",QFileInfo(model["settings"].toString()).absolutePath());});
    action(shortcuts,"Open Logs",[this]{openFolder("Open Logs",QDir(root).filePath("build-logs"));});
    action(shortcuts,"Open Build Guide",[this]{
        const auto file=QDir(root).filePath("docs/building.md");
        const auto url=QFileInfo::exists(file)?QUrl::fromLocalFile(file):QUrl("https://github.com/RivierGrullon/kirby-epic-yarn-recomp/blob/dev/docs/building.md");
        if(!QDesktopServices::openUrl(url))notice(T("Could not open folder"));
    });
}
} // namespace Yarn
