// SPDX-License-Identifier: GPL-3.0-only
#include "app/window.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QIcon>

int main(int argc,char** argv) {
    bool test=false;
    for(int i=1;i<argc;++i) if(QByteArray(argv[i])=="--self-test") test=true;
    if(test && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM","offscreen");
    QApplication app(argc,argv);
    app.setApplicationName("Kirby's Epic Yarn");
    app.setStyle("Fusion");
#ifdef Q_OS_WIN
    // The headless Qt platform has no system font database. Use the same
    // installed wordmark faces in previews as in a regular desktop window.
    if(test) {
        QDir fonts(qEnvironmentVariable("WINDIR")+"/Fonts");
        for(const char* face:{"georgia.ttf","georgiab.ttf","georgiai.ttf","consola.ttf","msyh.ttc","msyhbd.ttc"})
            QFontDatabase::addApplicationFont(fonts.filePath(face));
    }
#endif
    app.setWindowIcon(QIcon(":/yarn/launcher.ico"));
    const int fontId=QFontDatabase::addApplicationFont(":/yarn/font.ttf");
    QFont font=app.font();
    QStringList families{"Microsoft YaHei UI","Noto Sans CJK SC"};
    if(fontId>=0) families.append(QFontDatabase::applicationFontFamilies(fontId));
    font.setFamilies(families); app.setFont(font);
    QFile stylesheet(":/yarn/fabric.qss");
    if (!stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical("Could not open the bundled launcher stylesheet");
        return 1;
    }
    app.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    QDir bin(QCoreApplication::applicationDirPath());bin.cdUp();
    QDir root=bin;
    if(!bin.exists("kirby-portable.txt")) {root.cdUp();root.cdUp();}
#ifdef Q_OS_WIN
    const auto host=bin.filePath("kirby-launcher.exe");
#else
    const auto host=bin.filePath("kirby-launcher");
#endif
    Yarn::Window window(root.absolutePath(),host,test);
    if(test) {
        const auto args=app.arguments();
        const int option=args.indexOf("--previews");
        return window.preview(option>=0?args.value(option+1):QString())?0:1;
    }
    window.show();
    return app.exec();
}
