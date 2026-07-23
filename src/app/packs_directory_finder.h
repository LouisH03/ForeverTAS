#ifndef FOREVERTAS_APP_PACKS_DIRECTORY_FINDER_H
#define FOREVERTAS_APP_PACKS_DIRECTORY_FINDER_H

#include <QString>
#include <QStringList>

namespace forevertas::app {

QStringList DefaultPacksDirectorySearchPatterns();
QString FindInstalledPacksDirectory(const QStringList &patterns);
QString FindInstalledPacksDirectory();
bool IsUsablePacksDirectory(const QString &path);

}  // namespace forevertas::app

#endif
