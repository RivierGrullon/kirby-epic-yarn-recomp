// SPDX-License-Identifier: GPL-3.0-only
#include "app/translations.h"
#include "../../language_preference.h"
#include <QHash>

namespace Yarn {
namespace {
struct Message { const char *en,*zh,*es; };
constexpr Message messages[]{
#include "../../strings.inc"
};
int locale=static_cast<int>(Launcher::DefaultLanguage);
}

QString T(const char* key) {
    static const auto lookup=[] { QHash<QByteArray,const Message*> map;
        for (const auto& m:messages) map.insert(m.en,&m); return map; }();
    const auto* m=lookup.value(key,nullptr);
    return QString::fromUtf8(!m || locale==1?key:locale==2?m->es:m->zh);
}
void language(int value) { locale=value; }
QString status(const QString& value) {
    for (const auto prefix:{"Data verification failed: ","Already assigned to ","Missing ","Could not start the native executable: "})
        if (value.startsWith(QLatin1String(prefix))) return T(prefix)+status(value.mid(qstrlen(prefix)));
    return T(value.toUtf8().constData());
}
} // namespace Yarn
