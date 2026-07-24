#include "app/packs_directory_finder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

#include <algorithm>

namespace forevertas::app {
namespace {

bool HasWildcard(const QString &component) {
    return component.contains(QLatin1Char('*')) ||
            component.contains(QLatin1Char('?')) ||
            component.contains(QLatin1Char('['));
}

QStringList UniqueExistingDirectories(const QStringList &paths) {
    QStringList result;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.isDir()) {
            continue;
        }
        const QString canonical = info.canonicalFilePath();
        const QString normalized = canonical.isEmpty()
                ? QDir::cleanPath(info.absoluteFilePath())
                : canonical;
        if (!result.contains(normalized)) {
            result.push_back(normalized);
        }
    }
    return result;
}

QStringList ExpandDirectoryPattern(const QString &sourcePattern) {
    QString pattern = QDir::fromNativeSeparators(
            QDir::cleanPath(sourcePattern));
    if (!QFileInfo(pattern).isAbsolute()) {
        return {};
    }

    QString root;
    QString remainder;
#ifdef Q_OS_WIN
    if (pattern.size() >= 3 && pattern.at(1) == QLatin1Char(':') &&
        pattern.at(2) == QLatin1Char('/')) {
        root = pattern.left(3);
        remainder = pattern.mid(3);
    } else if (pattern.startsWith(QStringLiteral("//"))) {
        const QStringList parts = pattern.mid(2).split(
                QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            return {};
        }
        root = QStringLiteral("//%1/%2/").arg(parts.at(0), parts.at(1));
        remainder = parts.mid(2).join(QLatin1Char('/'));
    } else {
        return {};
    }
#else
    if (!pattern.startsWith(QLatin1Char('/'))) {
        return {};
    }
    root = QStringLiteral("/");
    remainder = pattern.mid(1);
#endif

    QStringList paths{root};
    const QStringList components = remainder.split(
            QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &component : components) {
        QStringList next;
        for (const QString &base : paths) {
            const QDir directory(base);
            if (HasWildcard(component)) {
                const QStringList matches = directory.entryList(
                        {component},
                        QDir::Dirs | QDir::NoDotAndDotDot,
                        QDir::Name);
                for (const QString &match : matches) {
                    next.push_back(directory.filePath(match));
                }
            } else {
                const QString child = directory.filePath(component);
                if (QFileInfo(child).isDir()) {
                    next.push_back(child);
                }
            }
        }
        paths = UniqueExistingDirectories(next);
        if (paths.isEmpty()) {
            break;
        }
    }
    return paths;
}

void AddWineInstallPatterns(QStringList &patterns,
                            const QString &prefixPattern) {
    static const QStringList gameDirectories{
            QStringLiteral("TmUnitedForever"),
            QStringLiteral("TrackMania United Forever"),
            QStringLiteral("TrackMania Nations Forever"),
            QStringLiteral("TmNationsForever")};
    for (const QString &programFiles : {
                 QStringLiteral("Program Files (x86)"),
                 QStringLiteral("Program Files")}) {
        for (const QString &gameDirectory : gameDirectories) {
            patterns.push_back(QDir(prefixPattern).filePath(
                    QStringLiteral("drive_c/%1/%2/Packs")
                            .arg(programFiles, gameDirectory)));
        }
    }
}

void AddNativeInstallPatterns(QStringList &patterns,
                              const QString &root) {
    if (root.isEmpty()) {
        return;
    }
    static const QStringList gameDirectories{
            QStringLiteral("TmUnitedForever"),
            QStringLiteral("TrackMania United Forever"),
            QStringLiteral("TrackMania Nations Forever"),
            QStringLiteral("TmNationsForever")};
    for (const QString &gameDirectory : gameDirectories) {
        patterns.push_back(QDir(root).filePath(
                QStringLiteral("%1/Packs").arg(gameDirectory)));
    }
}

QStringList ReadSteamLibraryRoots(const QStringList &steamRoots) {
    QStringList roots = UniqueExistingDirectories(steamRoots);
    static const QRegularExpression modernPath(
            QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
    static const QRegularExpression legacyPath(
            QStringLiteral(
                    "^\\s*\"\\d+\"\\s+\"([^\"]+)\"\\s*$"));

    const QStringList rootsToInspect = roots;
    for (const QString &steamRoot : rootsToInspect) {
        QFile file(QDir(steamRoot).filePath(
                QStringLiteral("steamapps/libraryfolders.vdf")));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString text = QString::fromUtf8(file.readAll());
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            QRegularExpressionMatch match = modernPath.match(line);
            if (!match.hasMatch()) {
                match = legacyPath.match(line);
            }
            if (!match.hasMatch()) {
                continue;
            }
            QString path = match.captured(1);
            path.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
            if (QFileInfo(path).isDir()) {
                roots.push_back(QFileInfo(path).canonicalFilePath());
            }
        }
    }
    return UniqueExistingDirectories(roots);
}

QStringList PlatformSteamRoots() {
    const QString home = QDir::homePath();
    QStringList roots;
#ifdef Q_OS_WIN
    const QString programFilesX86 =
            qEnvironmentVariable("ProgramFiles(x86)");
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    roots << QDir(programFilesX86).filePath(QStringLiteral("Steam"))
          << QDir(programFiles).filePath(QStringLiteral("Steam"));
    for (const QString &registryPath : {
                 QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
                 QStringLiteral(
                         "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam")}) {
        const QSettings registry(registryPath, QSettings::NativeFormat);
        roots << registry.value(QStringLiteral("SteamPath")).toString()
              << registry.value(QStringLiteral("InstallPath")).toString();
    }
#elif defined(Q_OS_MACOS)
    roots << QDir(home).filePath(
                     QStringLiteral("Library/Application Support/Steam"));
#else
    roots << QDir(home).filePath(QStringLiteral(".local/share/Steam"))
          << QDir(home).filePath(QStringLiteral(".steam/steam"))
          << QDir(home).filePath(QStringLiteral(".steam/root"))
          << QDir(home).filePath(QStringLiteral(
                     ".var/app/com.valvesoftware.Steam/.local/share/Steam"));
    for (const QString &dataRoot :
         QStandardPaths::standardLocations(
                 QStandardPaths::GenericDataLocation)) {
        roots << QDir(dataRoot).filePath(QStringLiteral("Steam"))
              << QDir(dataRoot).filePath(QStringLiteral("steam"));
    }
#endif
    return ReadSteamLibraryRoots(roots);
}

void AddSteamPatterns(QStringList &patterns, const QString &libraryRoot) {
    const QString common = QDir(libraryRoot).filePath(
            QStringLiteral("steamapps/common"));
    AddNativeInstallPatterns(patterns, common);

    const QString compatPrefix = QDir(libraryRoot).filePath(
            QStringLiteral("steamapps/compatdata/*/pfx"));
    AddWineInstallPatterns(patterns, compatPrefix);
}

bool IsUsefulMountedRoot(const QString &path) {
#ifdef Q_OS_WIN
    return !path.isEmpty();
#else
    return path == QStringLiteral("/") ||
            path.startsWith(QStringLiteral("/media/")) ||
            path.startsWith(QStringLiteral("/run/media/")) ||
            path.startsWith(QStringLiteral("/mnt/")) ||
            path.startsWith(QStringLiteral("/Volumes/"));
#endif
}

}  // namespace

bool IsUsablePacksDirectory(const QString &path) {
    const QFileInfo info(path);
    if (!info.isDir() || !info.isReadable()) {
        return false;
    }
    const QDir directory(info.absoluteFilePath());
    if (directory.exists(QStringLiteral("packlist.dat"))) {
        return true;
    }
    return directory.entryList(
                   {QStringLiteral("*.pak")},
                   QDir::Files | QDir::Readable)
            .size() >= 2;
}

QStringList DefaultPacksDirectorySearchPatterns() {
    const QString home = QDir::homePath();
    QStringList patterns;

#ifdef Q_OS_WIN
    AddNativeInstallPatterns(
            patterns, qEnvironmentVariable("ProgramFiles(x86)"));
    AddNativeInstallPatterns(patterns, qEnvironmentVariable("ProgramFiles"));
    AddNativeInstallPatterns(patterns, QStringLiteral("C:/Games"));
#elif defined(Q_OS_MACOS)
    AddWineInstallPatterns(
            patterns,
            QDir(home).filePath(QStringLiteral(
                    "Library/Application Support/CrossOver/Bottles/*")));
    AddWineInstallPatterns(
            patterns,
            QDir(home).filePath(QStringLiteral(
                    "Library/Containers/com.isaacmarovitz.Whisky/Bottles/*")));
    AddWineInstallPatterns(
            patterns,
            QDir(home).filePath(QStringLiteral(
                    "Library/Application Support/com.isaacmarovitz.Whisky/Bottles/*")));
    AddWineInstallPatterns(
            patterns, QDir(home).filePath(QStringLiteral(".wine")));
#else
    const QString winePrefix = qEnvironmentVariable("WINEPREFIX");
    if (!winePrefix.isEmpty()) {
        AddWineInstallPatterns(patterns, winePrefix);
    }
    AddWineInstallPatterns(
            patterns, QDir(home).filePath(QStringLiteral(".wine")));
    AddWineInstallPatterns(
            patterns,
            QDir(home).filePath(QStringLiteral("Games/*")));
    AddWineInstallPatterns(
            patterns,
            QDir(home).filePath(QStringLiteral("Games/*/pfx")));
    AddWineInstallPatterns(
            patterns,
            QDir(home).filePath(
                    QStringLiteral(".local/share/wineprefixes/*")));
    AddWineInstallPatterns(
            patterns,
            QDir(home).filePath(
                    QStringLiteral(".local/share/bottles/bottles/*")));
    AddWineInstallPatterns(
            patterns,
            QDir(home).filePath(QStringLiteral(
                    ".var/app/com.usebottles.bottles/data/bottles/bottles/*")));
    AddWineInstallPatterns(
            patterns,
            QDir(home).filePath(
                    QStringLiteral("Games/Heroic/Prefixes/*")));
    for (const QString &dataRoot :
         QStandardPaths::standardLocations(
                 QStandardPaths::GenericDataLocation)) {
        AddWineInstallPatterns(
                patterns,
                QDir(dataRoot).filePath(QStringLiteral("wineprefixes/*")));
        AddWineInstallPatterns(
                patterns,
                QDir(dataRoot).filePath(
                        QStringLiteral("bottles/bottles/*")));
    }
    AddNativeInstallPatterns(patterns, QStringLiteral("/opt"));
    AddNativeInstallPatterns(patterns, QStringLiteral("/usr/local/games"));
#endif

    for (const QString &steamRoot : PlatformSteamRoots()) {
        AddSteamPatterns(patterns, steamRoot);
    }

    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isReady() || !IsUsefulMountedRoot(storage.rootPath())) {
            continue;
        }
        const QString root = storage.rootPath();
        AddSteamPatterns(patterns, root);
        AddSteamPatterns(
                patterns, QDir(root).filePath(QStringLiteral("SteamLibrary")));
        AddWineInstallPatterns(
                patterns, QDir(root).filePath(QStringLiteral("Games/*")));
    }

    patterns.removeDuplicates();
    return patterns;
}

QString FindInstalledPacksDirectory(const QStringList &patterns) {
    for (const QString &pattern : patterns) {
        for (const QString &path : ExpandDirectoryPattern(pattern)) {
            if (IsUsablePacksDirectory(path)) {
                const QString canonical = QFileInfo(path).canonicalFilePath();
                return canonical.isEmpty()
                        ? QDir::cleanPath(QFileInfo(path).absoluteFilePath())
                        : canonical;
            }
        }
    }
    return {};
}

QString FindInstalledPacksDirectory() {
    return FindInstalledPacksDirectory(DefaultPacksDirectorySearchPatterns());
}

}  // namespace forevertas::app
