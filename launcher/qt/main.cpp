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
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
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
    QDir::addSearchPath("yarnTheme", ":/yarn");
    QString style;
    for (const char* path : {":/yarn/fabric.qss", ":/yarn/dialogs.qss"}) {
        QFile stylesheet(path);
        if (!stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCritical("Could not open the bundled stylesheet %s", path);
            return 1;
        }
        style += QString::fromUtf8(stylesheet.readAll());
    }
    app.setStyleSheet(style);
    QDir bin(QCoreApplication::applicationDirPath());bin.cdUp();
    QDir root=bin;
    if(!bin.exists("kirby-portable.txt")) {root.cdUp();root.cdUp();}
#ifdef Q_OS_WIN
    auto host=bin.filePath("kirby-launcher.exe");
#else
    auto host=bin.filePath("kirby-launcher");
#endif
    const auto launchArgs=app.arguments();
    const int hostOption=launchArgs.indexOf("--host"),rootOption=launchArgs.indexOf("--root");
    if(hostOption>=0 || rootOption>=0) {
        if(hostOption<0 || rootOption<0 || hostOption+1>=launchArgs.size() || rootOption+1>=launchArgs.size()) return 2;
        host=launchArgs[hostOption+1];root=QDir(launchArgs[rootOption+1]);
        if(!QDir::isAbsolutePath(host) || !root.isAbsolute()) return 2;
    }
    Yarn::Window window(root.absolutePath(),host,test);
    if(test) {
        const auto args=app.arguments();
        const int option=args.indexOf("--previews");
        return window.preview(option>=0?args.value(option+1):QString())?0:1;
    }
    window.show();
    return app.exec();
}
