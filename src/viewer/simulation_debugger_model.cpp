#include "viewer/simulation_debugger_model.h"

#include "simulation_debug_sources.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <utility>

void InitializeSimulationDebugResources() {
    Q_INIT_RESOURCE(simulation_debug_sources);
}

namespace forevertas::viewer {
namespace {

QString HtmlEscape(QString text) {
    return text.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
            .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
            .replace(QLatin1Char('>'), QStringLiteral("&gt;"));
}

QString TrimDebuggerNoise(QString diagnostics) {
    QStringList kept;
    for (const QString &line : diagnostics.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("DW_TAG_member '_M_t'")) ||
            line.trimmed().isEmpty()) {
            continue;
        }
        kept.push_back(line);
    }
    return kept.join(QLatin1Char('\n')).trimmed();
}

QVariantList JsonVector(const QJsonValue &value) {
    QVariantList result;
    const QJsonArray array = value.toArray();
    result.reserve(array.size());
    for (const QJsonValue &component : array) {
        result.push_back(component.toDouble());
    }
    return result;
}

} // namespace

SimulationDebuggerModel::SimulationDebuggerModel(QObject *parent)
    : QObject(parent) {
    InitializeSimulationDebugResources();
    expandedFolders_.insert(QStringLiteral("src"));
    expandedFolders_.insert(QStringLiteral("src/simulation"));
    expandedFolders_.insert(QStringLiteral("src/simulation/runtime"));
    expandedFolders_.insert(QStringLiteral("src/engine"));
    expandedFolders_.insert(QStringLiteral("src/engine/physics"));
    debugger_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&debugger_,
            &QProcess::readyReadStandardOutput,
            this,
            &SimulationDebuggerModel::readDebuggerOutput);
    connect(&debugger_,
            &QProcess::errorOccurred,
            this,
            [this](QProcess::ProcessError) {
                if (!stopping_ && active_) {
                    failSession(QStringLiteral(
                            "LLDB could not run the reference "
                            "simulation."));
                }
            });
    connect(&debugger_,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus status) {
                if (stopping_ || !active_) {
                    return;
                }
                setRunning(false);
                active_ = false;
                cancelStep();
                clearExecutionLocation();
                if (status == QProcess::NormalExit && exitCode == 0) {
                    setStatus(
                            QStringLiteral("Reference simulation completed."));
                    emit sessionFinished();
                } else {
                    failSession(QStringLiteral(
                            "The reference simulation debugger "
                            "stopped unexpectedly."));
                }
                emit stateChanged();
            });
    loadSources();
}

SimulationDebuggerModel::~SimulationDebuggerModel() {
    stopSession();
}

bool SimulationDebuggerModel::available() const {
    return available_;
}
bool SimulationDebuggerModel::active() const {
    return active_;
}
bool SimulationDebuggerModel::running() const {
    return running_;
}
bool SimulationDebuggerModel::stepping() const {
    return stepping_;
}
bool SimulationDebuggerModel::compiling() const {
    return compiling_;
}
bool SimulationDebuggerModel::canStepSource() const {
    return active_ && workerReady_ && !running_ && !compiling_ && !stepping_ &&
           activeLine_ > 0 && sourceIndex(activeFilePath_) >= 0;
}
bool SimulationDebuggerModel::canStepTick() const {
    return active_ && workerReady_ && !running_ && !compiling_ && !stepping_;
}
QString SimulationDebuggerModel::backendName() const {
    return backendName_;
}
QString SimulationDebuggerModel::selectedFilePath() const {
    return selectedFilePath_;
}
int SimulationDebuggerModel::activeLine() const {
    return activeLine_;
}
QString SimulationDebuggerModel::activeFilePath() const {
    return activeFilePath_;
}
QString SimulationDebuggerModel::statusText() const {
    return statusText_;
}
QString SimulationDebuggerModel::editError() const {
    return editError_;
}
qint64 SimulationDebuggerModel::executionTick() const {
    return executionTick_;
}

bool SimulationDebuggerModel::darkMode() const {
    return darkMode_;
}

void SimulationDebuggerModel::setDarkMode(bool value) {
    if (darkMode_ == value) {
        return;
    }
    darkMode_ = value;
    emit themeChanged();
    emit linesChanged();
}

bool SimulationDebuggerModel::hasEdits() const {
    return std::any_of(
            sources_.begin(), sources_.end(), [](const SourceFile &source) {
                return !source.edits.isEmpty();
            });
}

QVariantList SimulationDebuggerModel::fileEntries() const {
    return visibleFileEntries();
}

QVariantList SimulationDebuggerModel::lines() const {
    QVariantList result;
    const SourceFile *const source = selectedSource();
    if (source == nullptr) {
        return result;
    }
    result.reserve(source->currentLines.size());
    for (int index = 0; index < source->currentLines.size(); ++index) {
        const int line = index + 1;
        const QString &text = source->currentLines[index];
        const QString trimmed = text.trimmed();
        const bool sourceLine = !trimmed.isEmpty() &&
                                !trimmed.startsWith(QStringLiteral("//")) &&
                                !trimmed.startsWith(QLatin1Char('#')) &&
                                trimmed != QStringLiteral("{") &&
                                trimmed != QStringLiteral("}");
        result.push_back(
                QVariantMap{
                        {QStringLiteral("number"), line},
                        {QStringLiteral("text"), text},
                        {QStringLiteral("original"),
                         source->originalLines[index]},
                        {QStringLiteral("highlighted"),
                         syntaxHighlighted(text)},
                        {QStringLiteral("active"),
                         active_ && source->path == activeFilePath_ &&
                                 line == activeLine_},
                        {QStringLiteral("breakpoint"),
                         source->breakpoints.contains(line)},
                        {QStringLiteral("modified"),
                         source->edits.contains(line)},
                        {QStringLiteral("editable"), sourceLine},
                        {QStringLiteral("executable"),
                         source->executableLines.contains(line) ||
                                 source->breakpoints.contains(line) ||
                                 (source->path == activeFilePath_ &&
                                  line == activeLine_)},
                        {QStringLiteral("inlineValue"), inlineValues(text)}});
    }
    return result;
}

QVariantList SimulationDebuggerModel::variables() const {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(variables_.size()));
    for (const Variable &variable : variables_) {
        result.push_back(
                QVariantMap{
                        {QStringLiteral("name"), variable.name},
                        {QStringLiteral("value"), variable.value},
                        {QStringLiteral("type"), variable.type},
                        {QStringLiteral("pinned"),
                         pinnedNames_.contains(variable.name)}});
    }
    return result;
}

QVariantList SimulationDebuggerModel::pinnedVariables() const {
    QVariantList result;
    QStringList names(pinnedNames_.begin(), pinnedNames_.end());
    std::sort(names.begin(), names.end());
    for (const QString &name : names) {
        const auto found = std::find_if(
                variables_.begin(),
                variables_.end(),
                [&name](const Variable &variable) {
                    return variable.name == name;
                });
        result.push_back(
                QVariantMap{
                        {QStringLiteral("name"), name},
                        {QStringLiteral("value"),
                         found == variables_.end()
                                 ? QStringLiteral("out of scope")
                                 : found->value},
                        {QStringLiteral("type"),
                         found == variables_.end() ? QString() : found->type}});
    }
    return result;
}

bool SimulationDebuggerModel::selectFile(const QString &path) {
    if (sourceIndex(path) < 0) {
        return false;
    }
    if (selectedFilePath_ == path) {
        return true;
    }
    selectedFilePath_ = path;
    emit selectionChanged();
    emit linesChanged();
    return true;
}

void SimulationDebuggerModel::toggleFolder(const QString &path) {
    if (expandedFolders_.contains(path)) {
        expandedFolders_.remove(path);
    } else {
        expandedFolders_.insert(path);
    }
    emit filesChanged();
}

bool SimulationDebuggerModel::updateLine(int lineNumber, const QString &text) {
    SourceFile *const source = selectedSource();
    if (source == nullptr || lineNumber <= 0 ||
        lineNumber > source->currentLines.size()) {
        return false;
    }
    if (text.contains(QLatin1Char('\n')) ||
        text.contains(QLatin1Char('\r'))) {
        editError_ = QStringLiteral(
                "A live edit must contain exactly one source line.");
        emit linesChanged();
        return false;
    }
    const int index = lineNumber - 1;
    if (source->currentLines[index] == text) {
        return true;
    }
    source->currentLines[index] = text;
    if (text == source->originalLines[index]) {
        source->edits.remove(lineNumber);
    } else {
        const QString key = lineKey(source->path, lineNumber);
        const qint64 effectiveTick =
                active_ && executedLinesThisTick_.contains(key)
                        ? executionTick_ + 1
                        : executionTick_;
        source->edits.insert(lineNumber, SourceEdit{effectiveTick});
    }
    editError_.clear();
    if (active_) {
        syncSourceBreakpoints(*source);
        if (running_ && commandInFlight_ && !handlingDebuggerOutput_ &&
            (currentCommand_.kind == CommandKind::Run ||
             currentCommand_.kind == CommandKind::Continue)) {
            editInterruptRequested_ = true;
            debugger_.write("\x03", 1);
        }
    }
    emit filesChanged();
    emit linesChanged();
    setStatus(
            source->edits.contains(lineNumber)
                    ? QStringLiteral("Native edit queued for tick %1.")
                              .arg(source->edits[lineNumber].effectiveTick)
                    : QStringLiteral("Source line restored."));
    return true;
}

bool SimulationDebuggerModel::toggleBreakpoint(
        const QString &path, int lineNumber) {
    const int index = sourceIndex(path);
    if (index < 0 || lineNumber <= 0 ||
        lineNumber >
                sources_[static_cast<std::size_t>(index)].currentLines.size()) {
        return false;
    }
    SourceFile &source = sources_[static_cast<std::size_t>(index)];
    if (source.breakpoints.contains(lineNumber)) {
        source.breakpoints.remove(lineNumber);
    } else {
        source.breakpoints.insert(lineNumber);
        source.executableLines.insert(lineNumber);
    }
    saveBreakpoints();
    if (active_) {
        syncSourceBreakpoints(source);
        if (running_ && commandInFlight_ && !handlingDebuggerOutput_ &&
            (currentCommand_.kind == CommandKind::Run ||
             currentCommand_.kind == CommandKind::Continue)) {
            editInterruptRequested_ = true;
            debugger_.write("\x03", 1);
        }
    }
    emit filesChanged();
    if (selectedFilePath_ == path) {
        emit linesChanged();
    }
    return true;
}

bool SimulationDebuggerModel::togglePinned(const QString &name) {
    const auto found = std::find_if(
            variables_.begin(),
            variables_.end(),
            [&name](const Variable &variable) {
                return variable.name == name;
            });
    if (found == variables_.end() && !pinnedNames_.contains(name)) {
        return false;
    }
    if (pinnedNames_.contains(name)) {
        pinnedNames_.remove(name);
    } else {
        pinnedNames_.insert(name);
    }
    emit variablesChanged();
    return true;
}

void SimulationDebuggerModel::resetEdits() {
    for (SourceFile &source : sources_) {
        source.currentLines = source.originalLines;
        source.edits.clear();
    }
    editError_.clear();
    emit filesChanged();
    emit linesChanged();
    setStatus(QStringLiteral("All in-memory source edits were reset."));
}

bool SimulationDebuggerModel::stepSubstep() {
    return beginStep(StepMode::Substep);
}

bool SimulationDebuggerModel::stepSourceLine() {
    return beginStep(StepMode::SourceLine);
}

bool SimulationDebuggerModel::stepTick() {
    return beginStep(StepMode::Tick);
}

void SimulationDebuggerModel::configure(const QString &backendName) {
    backendName_ = backendName.trimmed().isEmpty() ? QStringLiteral("Reference")
                                                   : backendName;
    stopSession();
    emit stateChanged();
}

bool SimulationDebuggerModel::startSession(
        const QString &packsDirectory, const QString &replayPath) {
    if (!available_ || packsDirectory.trimmed().isEmpty() ||
        replayPath.trimmed().isEmpty()) {
        setStatus(QStringLiteral(
                "Load a replay and Packs directory before "
                "starting native source debugging."));
        return false;
    }
    stopSession();
    stopping_ = false;
    commandQueue_.clear();
    debuggerBuffer_.clear();
    editError_.clear();
    executedLinesThisTick_.clear();
    currentLineKey_.clear();
    lastBreakpointKey_.clear();
    executionTick_ = 0;
    installedBreakpointKeys_.clear();
    installedBreakpointIds_.clear();
    setupQueued_ = false;
    startupPromptsRemaining_ = 1;
    hasCurrentCommand_ = false;
    commandInFlight_ = false;
    advanceScheduled_ = false;
    cancelStep();
    pauseRequested_ = false;
    editInterruptRequested_ = false;
    handlingDebuggerOutput_ = false;
    atTickBoundary_ = false;
    workerReady_ = false;
    packsDirectory_ = QDir::toNativeSeparators(packsDirectory);
    replayPath_ = QDir::toNativeSeparators(replayPath);
    active_ = true;
    setRunning(false);
    setStatus(QStringLiteral("Starting the real reference physics engine..."));
    emit stateChanged();
    debugger_.setWorkingDirectory(QCoreApplication::applicationDirPath());
    const QString debuggerCommand =
            quoteShellArgument(lldbExecutablePath()) +
            QStringLiteral(" --no-lldbinit");
    debugger_.start(
            scriptExecutablePath(),
            {QStringLiteral("-qefc"),
             debuggerCommand,
             QStringLiteral("/dev/null")});
    if (!debugger_.waitForStarted(5000)) {
        active_ = false;
        failSession(QStringLiteral("LLDB could not be started."));
        return false;
    }
    return true;
}

void SimulationDebuggerModel::stopSession() {
    const bool wasActive = active_;
    stopping_ = true;
    setRunning(false);
    active_ = false;
    compiling_ = false;
    commandInFlight_ = false;
    advanceScheduled_ = false;
    cancelStep();
    atTickBoundary_ = false;
    commandQueue_.clear();
    hasCurrentCommand_ = false;
    installedBreakpointKeys_.clear();
    installedBreakpointIds_.clear();
    clearExecutionLocation();
    if (debugger_.state() != QProcess::NotRunning) {
        debugger_.write("quit\n");
        debugger_.closeWriteChannel();
        if (!debugger_.waitForFinished(1500)) {
            debugger_.terminate();
            if (!debugger_.waitForFinished(1000)) {
                debugger_.kill();
                debugger_.waitForFinished(1000);
            }
        }
    }
    stopping_ = false;
    if (wasActive) {
        setStatus(QStringLiteral("Native source debugging stopped."));
        emit stateChanged();
        emit sessionFinished();
    }
}

void SimulationDebuggerModel::play() {
    if (!active_ || compiling_ || stepping_) {
        return;
    }
    lastBreakpointKey_.clear();
    pauseRequested_ = false;
    editError_.clear();
    setRunning(true);
    setStatus(QStringLiteral("Stepping the real reference physics engine."));
    scheduleAdvance();
}

void SimulationDebuggerModel::pause() {
    if (!active_) {
        return;
    }
    if (stepping_) {
        return;
    }
    setRunning(false);
    pauseRequested_ = true;
    setStatus(QStringLiteral(
            "Pausing at the next native source-line boundary..."));
    if (!commandInFlight_ && activeLine_ > 0) {
        pauseRequested_ = false;
        variables_.clear();
        queueCommand(
                CommandKind::Variables,
                QStringLiteral("frame variable --show-types"));
        setStatus(QStringLiteral("Paused at a native source-line boundary."));
    } else if (!commandInFlight_ && atTickBoundary_) {
        queueCommand(CommandKind::Continue, QStringLiteral("continue"));
    }
}

QString SimulationDebuggerModel::syntaxHighlighted(const QString &text) const {
    QString escaped = HtmlEscape(text);
    static const QRegularExpression stringPattern(
            QStringLiteral("(\".*?\"|'.*?')"));
    static const QRegularExpression numberPattern(
            QStringLiteral("\\b([0-9]+(?:\\.[0-9]+)?[fFuUlL]*)\\b"));
    static const QRegularExpression keywordPattern(QStringLiteral(
            "\\b(auto|bool|break|case|catch|class|const|constexpr|"
            "continue|default|do|double|else|enum|explicit|false|"
            "float|for|if|inline|int|namespace|noexcept|nullptr|"
            "private|protected|public|return|sizeof|static|struct|"
            "switch|template|this|throw|true|try|typename|using|"
            "void|while)\\b"));
    const QString stringColor =
            darkMode_ ? QStringLiteral("#e9b86c") : QStringLiteral("#8a4f27");
    const QString numberColor =
            darkMode_ ? QStringLiteral("#d9a0e8") : QStringLiteral("#8b3fa3");
    const QString keywordColor =
            darkMode_ ? QStringLiteral("#80b9ef") : QStringLiteral("#235f9e");
    const QString commentColor =
            darkMode_ ? QStringLiteral("#91a092") : QStringLiteral("#6b786b");
    escaped.replace(
            stringPattern,
            QStringLiteral("<span style=\"color:%1\">\\1</span>")
                    .arg(stringColor));
    escaped.replace(
            numberPattern,
            QStringLiteral("<span style=\"color:%1\">\\1</span>")
                    .arg(numberColor));
    escaped.replace(
            keywordPattern,
            QStringLiteral(
                    "<span style=\"color:%1;font-weight:600\">"
                    "\\1</span>")
                    .arg(keywordColor));
    const qsizetype comment = escaped.indexOf(QStringLiteral("//"));
    if (comment >= 0) {
        escaped = escaped.left(comment) +
                  QStringLiteral("<span style=\"color:%1\">")
                          .arg(commentColor) +
                  escaped.mid(comment) + QStringLiteral("</span>");
    }
    return escaped;
}

QString SimulationDebuggerModel::fileName(const QString &path) {
    return path.section(QLatin1Char('/'), -1);
}

int SimulationDebuggerModel::depth(const QString &path) {
    return path.count(QLatin1Char('/'));
}

QString SimulationDebuggerModel::lldbExecutablePath() {
#ifdef FOREVERTAS_LLDB_EXECUTABLE
    return QString::fromUtf8(FOREVERTAS_LLDB_EXECUTABLE);
#else
    return {};
#endif
}

QString SimulationDebuggerModel::scriptExecutablePath() {
#ifdef FOREVERTAS_SCRIPT_EXECUTABLE
    return QString::fromUtf8(FOREVERTAS_SCRIPT_EXECUTABLE);
#else
    return {};
#endif
}

QString SimulationDebuggerModel::workerExecutablePath() {
#ifdef Q_OS_WIN
    const QString name =
            QStringLiteral("forevertas-simulation-debug-worker.exe");
#else
    const QString name = QStringLiteral("forevertas-simulation-debug-worker");
#endif
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QString adjacent = applicationDirectory.filePath(name);
    if (QFileInfo::exists(adjacent)) {
        return adjacent;
    }
    return applicationDirectory.filePath(QStringLiteral("bin/") + name);
}

QString SimulationDebuggerModel::sourceRootPath() {
#ifdef FOREVERTAS_SIMULATION_DEBUG_SOURCE_ROOT
    return QDir::cleanPath(
            QString::fromUtf8(FOREVERTAS_SIMULATION_DEBUG_SOURCE_ROOT));
#else
    return {};
#endif
}

QString SimulationDebuggerModel::quoteDebuggerArgument(const QString &value) {
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString SimulationDebuggerModel::quoteShellArgument(const QString &value) {
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

int SimulationDebuggerModel::sourceIndex(const QString &path) const {
    const auto found = std::find_if(
            sources_.begin(),
            sources_.end(),
            [&path](const SourceFile &source) { return source.path == path; });
    return found == sources_.end()
                   ? -1
                   : static_cast<int>(std::distance(sources_.begin(), found));
}

SimulationDebuggerModel::SourceFile *SimulationDebuggerModel::selectedSource() {
    const int index = sourceIndex(selectedFilePath_);
    return index < 0 ? nullptr : &sources_[static_cast<std::size_t>(index)];
}

const SimulationDebuggerModel::SourceFile *
SimulationDebuggerModel::selectedSource() const {
    const int index = sourceIndex(selectedFilePath_);
    return index < 0 ? nullptr : &sources_[static_cast<std::size_t>(index)];
}

QVariantList SimulationDebuggerModel::visibleFileEntries() const {
    QSet<QString> directories;
    for (const SourceFile &source : sources_) {
        QString folder = source.path.section(QLatin1Char('/'), 0, -2);
        while (!folder.isEmpty()) {
            directories.insert(folder);
            folder = folder.section(QLatin1Char('/'), 0, -2);
        }
    }
    QStringList sortedDirectories(directories.begin(), directories.end());
    std::sort(sortedDirectories.begin(), sortedDirectories.end());

    QVariantList result;
    const auto visible = [this](const QString &path) {
        QString parent = path.section(QLatin1Char('/'), 0, -2);
        while (!parent.isEmpty()) {
            if (!expandedFolders_.contains(parent)) {
                return false;
            }
            parent = parent.section(QLatin1Char('/'), 0, -2);
        }
        return true;
    };
    for (const QString &directory : sortedDirectories) {
        if (!visible(directory)) {
            continue;
        }
        const QString prefix = directory + QLatin1Char('/');
        const bool modified = std::any_of(
                sources_.begin(),
                sources_.end(),
                [&prefix](const SourceFile &source) {
                    return source.path.startsWith(prefix) &&
                           !source.edits.isEmpty();
                });
        const bool breakpoint = std::any_of(
                sources_.begin(),
                sources_.end(),
                [&prefix](const SourceFile &source) {
                    return source.path.startsWith(prefix) &&
                           !source.breakpoints.isEmpty();
                });
        result.push_back(
                QVariantMap{
                        {QStringLiteral("path"), directory},
                        {QStringLiteral("name"), fileName(directory)},
                        {QStringLiteral("depth"), depth(directory)},
                        {QStringLiteral("directory"), true},
                        {QStringLiteral("expanded"),
                         expandedFolders_.contains(directory)},
                        {QStringLiteral("modified"), modified},
                        {QStringLiteral("breakpoint"), breakpoint},
                        {QStringLiteral("selected"), false}});
    }
    for (const SourceFile &source : sources_) {
        if (!visible(source.path)) {
            continue;
        }
        result.push_back(
                QVariantMap{
                        {QStringLiteral("path"), source.path},
                        {QStringLiteral("name"), fileName(source.path)},
                        {QStringLiteral("depth"), depth(source.path)},
                        {QStringLiteral("directory"), false},
                        {QStringLiteral("expanded"), false},
                        {QStringLiteral("modified"), !source.edits.isEmpty()},
                        {QStringLiteral("breakpoint"),
                         !source.breakpoints.isEmpty()},
                        {QStringLiteral("selected"),
                         source.path == selectedFilePath_},
                        {QStringLiteral("active"),
                         source.path == activeFilePath_}});
    }
    std::sort(
            result.begin(),
            result.end(),
            [](const QVariant &left, const QVariant &right) {
                const QVariantMap leftMap = left.toMap();
                const QVariantMap rightMap = right.toMap();
                const QString leftPath =
                        leftMap.value(QStringLiteral("path")).toString();
                const QString rightPath =
                        rightMap.value(QStringLiteral("path")).toString();
                if (leftPath == rightPath) {
                    return leftMap.value(QStringLiteral("directory")).toBool();
                }
                const QString leftPrefix = leftPath + QLatin1Char('/');
                const QString rightPrefix = rightPath + QLatin1Char('/');
                if (rightPath.startsWith(leftPrefix)) {
                    return true;
                }
                if (leftPath.startsWith(rightPrefix)) {
                    return false;
                }
                return leftPath < rightPath;
            });
    return result;
}

QString SimulationDebuggerModel::inlineValues(const QString &line) const {
    QStringList values;
    for (const Variable &variable : variables_) {
        if (variable.name.isEmpty() ||
            variable.name == QStringLiteral("this")) {
            continue;
        }
        const QRegularExpression pattern(
                QStringLiteral("(^|[^A-Za-z0-9_])%1([^A-Za-z0-9_]|$)")
                        .arg(QRegularExpression::escape(variable.name)));
        if (pattern.match(line).hasMatch()) {
            values.push_back(
                    variable.name + QStringLiteral(" = ") + variable.value);
            if (values.size() == 3) {
                break;
            }
        }
    }
    return values.join(QStringLiteral("   "));
}

QString
SimulationDebuggerModel::relativeSourcePath(const QString &absolutePath) const {
    const QString clean = QDir::cleanPath(absolutePath);
    const QString root = sourceRootPath();
    if (!root.isEmpty() && clean.startsWith(root + QLatin1Char('/'))) {
        return clean.mid(root.size() + 1);
    }
    const qsizetype sourceIndex = clean.lastIndexOf(QStringLiteral("/src/"));
    if (sourceIndex >= 0) {
        const QString candidate = clean.mid(sourceIndex + 1);
        if (this->sourceIndex(candidate) >= 0) {
            return candidate;
        }
    }
    const QString basename = QFileInfo(clean).fileName();
    QString uniqueMatch;
    for (const SourceFile &source : sources_) {
        if (fileName(source.path) != basename) {
            continue;
        }
        if (!uniqueMatch.isEmpty()) {
            return {};
        }
        uniqueMatch = source.path;
    }
    if (!uniqueMatch.isEmpty()) {
        return uniqueMatch;
    }
    return {};
}

QString SimulationDebuggerModel::lineKey(const QString &path, int line) const {
    return path + QLatin1Char(':') + QString::number(line);
}

int SimulationDebuggerModel::statementEndLine(
        const SourceFile &source, int line) const {
    int parentheses = 0;
    int brackets = 0;
    bool sawDelimiter = false;
    for (int current = line; current <= source.originalLines.size();
         ++current) {
        const QString &text = source.originalLines[current - 1];
        bool string = false;
        QChar quote;
        for (int index = 0; index < text.size(); ++index) {
            const QChar character = text[index];
            if (string) {
                if (character == QLatin1Char('\\')) {
                    ++index;
                } else if (character == quote) {
                    string = false;
                }
                continue;
            }
            if (character == QLatin1Char('"') ||
                character == QLatin1Char('\'')) {
                string = true;
                quote = character;
            } else if (character == QLatin1Char('(')) {
                ++parentheses;
                sawDelimiter = true;
            } else if (character == QLatin1Char(')')) {
                --parentheses;
            } else if (character == QLatin1Char('[')) {
                ++brackets;
                sawDelimiter = true;
            } else if (character == QLatin1Char(']')) {
                --brackets;
            }
        }
        const QString trimmed = text.trimmed();
        if (parentheses <= 0 && brackets <= 0 &&
            (trimmed.endsWith(QLatin1Char(';')) ||
             trimmed.endsWith(QLatin1Char('{')) ||
             trimmed == QStringLiteral("}") ||
             (!sawDelimiter && current > line))) {
            return current;
        }
    }
    return line;
}

bool SimulationDebuggerModel::editApplies(
        const SourceFile &source, int line) const {
    const auto found = source.edits.constFind(line);
    return found != source.edits.cend() &&
           found->effectiveTick <= executionTick_;
}

void SimulationDebuggerModel::loadSources() {
    sources_.clear();
    const QString root = sourceRootPath();
    for (const std::string_view entry : kSimulationDebugSourcePaths) {
        const QString path = QString::fromUtf8(
                entry.data(), static_cast<qsizetype>(entry.size()));
        QFile file(QStringLiteral(":/simulation-debug/") + path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QString content = QString::fromUtf8(file.readAll());
        content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        content.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        QStringList lines = content.split(QLatin1Char('\n'));
        if (!lines.isEmpty() && lines.back().isEmpty()) {
            lines.removeLast();
        }
        sources_.push_back(
                SourceFile{
                        path,
                        QDir(root).filePath(path),
                        lines,
                        lines,
                        {},
                        {},
                        {}});
    }
    std::sort(
            sources_.begin(),
            sources_.end(),
            [](const SourceFile &left, const SourceFile &right) {
                return left.path < right.path;
            });
    const QString preferred = QStringLiteral(
            "src/simulation/runtime/replay_simulation_runtime.cpp");
    selectedFilePath_ =
            sourceIndex(preferred) >= 0
                    ? preferred
                    : (sources_.empty() ? QString() : sources_.front().path);
    available_ = !sources_.empty() &&
                 QFileInfo::exists(workerExecutablePath()) &&
                 QFileInfo::exists(lldbExecutablePath()) &&
                 QFileInfo::exists(scriptExecutablePath());
    restoreBreakpoints();
    statusText_ =
            available_
                    ? QStringLiteral("Real reference engine source is ready.")
                    : QStringLiteral(
                              "Native source debugging requires the reference "
                              "worker, LLDB, and a terminal bridge.");
}

void SimulationDebuggerModel::restoreBreakpoints() {
    const QStringList persisted = QSettings()
                                          .value(QStringLiteral(
                                                  "simulationDebugger/"
                                                  "breakpointsV1"))
                                          .toStringList();
    for (const QString &key : persisted) {
        const qsizetype separator = key.lastIndexOf(QLatin1Char(':'));
        bool lineValid = false;
        const int line = key.mid(separator + 1).toInt(&lineValid);
        const QString path = separator > 0 ? key.left(separator) : QString();
        const int index = sourceIndex(path);
        if (!lineValid || line <= 0 || index < 0 ||
            line > sources_[static_cast<std::size_t>(index)]
                            .currentLines.size()) {
            continue;
        }
        SourceFile &source = sources_[static_cast<std::size_t>(index)];
        source.breakpoints.insert(line);
        source.executableLines.insert(line);
    }
}

void SimulationDebuggerModel::saveBreakpoints() const {
    QStringList persisted;
    for (const SourceFile &source : sources_) {
        for (const int line : source.breakpoints) {
            persisted.push_back(lineKey(source.path, line));
        }
    }
    std::sort(persisted.begin(), persisted.end());
    QSettings().setValue(
            QStringLiteral("simulationDebugger/breakpointsV1"), persisted);
}

void SimulationDebuggerModel::syncSourceBreakpoints(SourceFile &source) {
    QSet<int> lines = source.breakpoints;
    for (auto edit = source.edits.cbegin(); edit != source.edits.cend();
         ++edit) {
        lines.insert(edit.key());
    }
    const QString keyPrefix = source.path + QLatin1Char(':');
    const QList<QString> installed = installedBreakpointKeys_.values();
    for (const QString &key : installed) {
        if (!key.startsWith(keyPrefix)) {
            continue;
        }
        bool lineValid = false;
        const int installedLine = key.mid(keyPrefix.size()).toInt(&lineValid);
        if (!lineValid || lines.contains(installedLine)) {
            continue;
        }
        installedBreakpointKeys_.remove(key);
        const int breakpointId = installedBreakpointIds_.take(key);
        if (breakpointId > 0) {
            queueCommand(
                    CommandKind::SourceBreakpointRemove,
                    QStringLiteral("breakpoint delete %1").arg(breakpointId),
                    key,
                    breakpointId);
        }
    }
    QList<int> sorted(lines.begin(), lines.end());
    std::sort(sorted.begin(), sorted.end());
    for (const int line : sorted) {
        installSourceBreakpoint(source, line);
    }
}

void SimulationDebuggerModel::installSourceBreakpoint(
        SourceFile &source, int line) {
    const QString key = lineKey(source.path, line);
    if (!active_ || installedBreakpointKeys_.contains(key)) {
        return;
    }
    installedBreakpointKeys_.insert(key);
    source.executableLines.insert(line);
    queueCommand(
            CommandKind::SourceBreakpoint,
            QStringLiteral("breakpoint set --file %1 --line %2")
                    .arg(quoteDebuggerArgument(source.absolutePath))
                    .arg(line),
            source.path,
            line);
}

void SimulationDebuggerModel::queueCommand(
        CommandKind kind,
        const QString &text,
        const QString &sourcePath,
        int line) {
    if (!active_ || debugger_.state() == QProcess::NotRunning) {
        return;
    }
    commandQueue_.enqueue(DebuggerCommand{kind, text, sourcePath, line});
    sendNextCommand();
}

void SimulationDebuggerModel::sendNextCommand() {
    if (!active_ || hasCurrentCommand_ || commandQueue_.isEmpty() ||
        debugger_.state() != QProcess::Running) {
        return;
    }
    currentCommand_ = commandQueue_.dequeue();
    hasCurrentCommand_ = true;
    commandInFlight_ = true;
    debugger_.write(currentCommand_.text.toUtf8());
    debugger_.write("\n");
}

void SimulationDebuggerModel::readDebuggerOutput() {
    const QByteArray chunk = debugger_.readAllStandardOutput();
    debuggerBuffer_ += QString::fromLocal8Bit(chunk);
    consumeDebuggerPrompts();
}

void SimulationDebuggerModel::consumeDebuggerPrompts() {
    static const QString prompt = QStringLiteral("(lldb) ");
    qsizetype promptPosition = debuggerBuffer_.indexOf(prompt);
    while (promptPosition >= 0) {
        handlingDebuggerOutput_ = true;
        const QString output = debuggerBuffer_.left(promptPosition);
        debuggerBuffer_.remove(0, promptPosition + prompt.size());
        parseWorkerOutput(output);
        if (startupPromptsRemaining_ > 0) {
            --startupPromptsRemaining_;
            if (startupPromptsRemaining_ == 0 && !setupQueued_) {
                setupQueued_ = true;
                queueCommand(
                        CommandKind::Setting,
                        QStringLiteral("target create %1")
                                .arg(quoteDebuggerArgument(
                                        workerExecutablePath())));
                queueCommand(
                        CommandKind::Setting,
                        QStringLiteral(
                                "settings set -- target.run-args %1 %2")
                                .arg(quoteDebuggerArgument(packsDirectory_))
                                .arg(quoteDebuggerArgument(replayPath_)));
                queueCommand(
                        CommandKind::Setting,
                        QStringLiteral(
                                "settings set symbols.enable-external-lookup "
                                "false"));
                queueCommand(
                        CommandKind::Setting,
                        QStringLiteral(
                                "settings set "
                                "target.inline-breakpoint-strategy "
                                "always"));
                queueCommand(
                        CommandKind::FunctionBreakpoint,
                        QStringLiteral(
                                "breakpoint set --name "
                                "forevertas_debugger_tick_boundary"));
                queueCommand(
                        CommandKind::FunctionBreakpoint,
                        QStringLiteral("breakpoint set --method AdvanceTicks"));
                queueCommand(CommandKind::Run, QStringLiteral("run"));
                setStatus(QStringLiteral(
                        "Loading replay in the reference engine..."));
            }
        } else if (hasCurrentCommand_) {
            const DebuggerCommand completed = currentCommand_;
            hasCurrentCommand_ = false;
            commandInFlight_ = false;
            handleCommandResult(completed, output);
        }
        handlingDebuggerOutput_ = false;
        sendNextCommand();
        promptPosition = debuggerBuffer_.indexOf(prompt);
    }
}

void SimulationDebuggerModel::handleCommandResult(
        const DebuggerCommand &command, const QString &output) {
    switch (command.kind) {
    case CommandKind::Setting:
    case CommandKind::FunctionBreakpoint:
        if (debuggerCommandFailed(output)) {
            failSession(
                    QStringLiteral("LLDB rejected required debugger setup: %1")
                            .arg(TrimDebuggerNoise(output)));
        }
        break;
    case CommandKind::SourceBreakpoint:
        handleSourceBreakpointResult(command, output);
        scheduleAdvance();
        break;
    case CommandKind::SourceBreakpointRemove:
        if (debuggerCommandFailed(output)) {
            installedBreakpointKeys_.insert(command.sourcePath);
            installedBreakpointIds_.insert(command.sourcePath, command.line);
            editError_ =
                    QStringLiteral("Could not remove native breakpoint %1: %2")
                            .arg(command.line)
                            .arg(TrimDebuggerNoise(output));
            emit linesChanged();
        }
        scheduleAdvance();
        break;
    case CommandKind::Run:
    case CommandKind::Continue:
    case CommandKind::Substep:
    case CommandKind::SourceLineStep:
    case CommandKind::RefreshLocation:
        handleDebuggerStop(output);
        break;
    case CommandKind::Variables:
        parseVariables(output);
        if (running_) {
            scheduleAdvance();
        }
        break;
    case CommandKind::EvaluateEdit: {
        compiling_ = false;
        emit stateChanged();
        QString diagnostic = TrimDebuggerNoise(output);
        if (diagnostic.startsWith(command.text)) {
            diagnostic.remove(0, command.text.size());
            diagnostic = diagnostic.trimmed();
        }
        if (debuggerCommandFailed(diagnostic)) {
            cancelStep();
            editError_ = diagnostic;
            if (editError_.isEmpty()) {
                editError_ = QStringLiteral(
                        "The edited C++ statement was rejected.");
            }
            setRunning(false);
            emit linesChanged();
            setStatus(QStringLiteral("Native edit did not compile."));
        } else {
            jumpPastCurrentStatement();
        }
        break;
    }
    case CommandKind::JumpAfterEdit:
        if (debuggerCommandFailed(output)) {
            cancelStep();
            editError_ = QStringLiteral(
                                 "The debugger could not skip the original "
                                 "source statement: %1")
                                 .arg(TrimDebuggerNoise(output));
            setRunning(false);
            compiling_ = false;
            emit linesChanged();
        } else {
            executedLinesThisTick_.insert(currentLineKey_);
            if (stepping_) {
                if (stepMode_ == StepMode::Tick) {
                    queueCommand(
                            CommandKind::Continue, QStringLiteral("continue"));
                } else {
                    queueCommand(
                            CommandKind::RefreshLocation,
                            QStringLiteral("frame info"));
                }
            } else {
                scheduleAdvance();
            }
        }
        break;
    case CommandKind::Quit:
        break;
    }
}

void SimulationDebuggerModel::handleSourceBreakpointResult(
        const DebuggerCommand &command, const QString &output) {
    const QString requestedKey = lineKey(command.sourcePath, command.line);
    const int index = sourceIndex(command.sourcePath);
    if (index < 0) {
        installedBreakpointKeys_.remove(requestedKey);
        return;
    }
    SourceFile &source = sources_[static_cast<std::size_t>(index)];

    static const QRegularExpression locationPattern(
            QStringLiteral("\\bat\\s+(.+?):(\\d+)(?::\\d+)?(?:\\s|,|$)"));
    static const QRegularExpression idPattern(
            QStringLiteral("\\bBreakpoint\\s+(\\d+):"),
            QRegularExpression::CaseInsensitiveOption);
    const int breakpointId = idPattern.match(output).captured(1).toInt();
    int resolvedLine = -1;
    QRegularExpressionMatchIterator locations =
            locationPattern.globalMatch(output);
    while (locations.hasNext()) {
        const QRegularExpressionMatch location = locations.next();
        const QString resolvedPath =
                relativeSourcePath(QDir::cleanPath(location.captured(1)));
        if (resolvedPath == command.sourcePath ||
            QFileInfo(location.captured(1)).fileName() ==
                    fileName(command.sourcePath)) {
            resolvedLine = location.captured(2).toInt();
            break;
        }
    }

    const bool unresolved =
            resolvedLine <= 0 ||
            output.contains(
                    QStringLiteral("no locations (pending)"),
                    Qt::CaseInsensitive) ||
            output.contains(
                    QStringLiteral("Unable to resolve breakpoint"),
                    Qt::CaseInsensitive);
    if (debuggerCommandFailed(output) || unresolved) {
        installedBreakpointKeys_.remove(requestedKey);
        installedBreakpointIds_.remove(requestedKey);
        if (breakpointId > 0) {
            queueCommand(
                    CommandKind::SourceBreakpointRemove,
                    QStringLiteral("breakpoint delete %1").arg(breakpointId),
                    requestedKey,
                    breakpointId);
        }
        if (source.breakpoints.remove(command.line)) {
            saveBreakpoints();
            emit filesChanged();
        }
        editError_ =
                QStringLiteral("Line %1 has no executable debugger location.")
                        .arg(command.line);
        const QString diagnostic = TrimDebuggerNoise(output);
        if (!diagnostic.isEmpty()) {
            editError_ += QLatin1Char('\n') + diagnostic;
        }
        emit linesChanged();
        setStatus(QStringLiteral("Breakpoint could not be resolved."));
        return;
    }

    const bool stillWanted = source.breakpoints.contains(command.line) ||
                             source.edits.contains(command.line);
    if (!stillWanted) {
        installedBreakpointKeys_.remove(requestedKey);
        if (breakpointId > 0) {
            queueCommand(
                    CommandKind::SourceBreakpointRemove,
                    QStringLiteral("breakpoint delete %1").arg(breakpointId),
                    requestedKey,
                    breakpointId);
        }
        return;
    }

    if (breakpointId > 0) {
        installedBreakpointIds_.insert(requestedKey, breakpointId);
    }
    source.executableLines.insert(resolvedLine);
    if (source.breakpoints.contains(command.line) &&
        resolvedLine != command.line) {
        source.breakpoints.remove(command.line);
        source.breakpoints.insert(resolvedLine);
        installedBreakpointKeys_.remove(requestedKey);
        installedBreakpointIds_.remove(requestedKey);
        const QString resolvedKey = lineKey(command.sourcePath, resolvedLine);
        installedBreakpointKeys_.insert(resolvedKey);
        if (breakpointId > 0) {
            installedBreakpointIds_.insert(resolvedKey, breakpointId);
        }
        saveBreakpoints();
        emit filesChanged();
        emit linesChanged();
        setStatus(
                QStringLiteral("Breakpoint moved to executable line %1 in %2.")
                        .arg(resolvedLine)
                        .arg(fileName(command.sourcePath)));
    }
}

void SimulationDebuggerModel::handleDebuggerStop(const QString &output) {
    if (!active_) {
        return;
    }
    if (output.contains(QStringLiteral("forevertas_debugger_tick_boundary"))) {
        if (!currentLineKey_.isEmpty()) {
            executedLinesThisTick_.insert(currentLineKey_);
        }
        currentLineKey_.clear();
        executedLinesThisTick_.clear();
        activeLine_ = -1;
        activeFilePath_.clear();
        atTickBoundary_ = true;
        editInterruptRequested_ = false;
        emit executionChanged();
        emit linesChanged();
        emit filesChanged();
        for (SourceFile &source : sources_) {
            syncSourceBreakpoints(source);
        }
        if (stepping_) {
            finishStep(
                    stepMode_ == StepMode::Tick
                            ? QStringLiteral(
                                      "Advanced exactly one physics tick.")
                            : QStringLiteral(
                                      "Step reached the next tick boundary."));
        } else if (pauseRequested_) {
            setStatus(QStringLiteral(
                    "Locating the next native source-line boundary..."));
            queueCommand(CommandKind::Continue, QStringLiteral("continue"));
        } else {
            setStatus(QStringLiteral("Reference tick %1 completed.")
                              .arg(executionTick_));
            scheduleAdvance();
        }
        return;
    }
    static const QRegularExpression locationPattern(
            QStringLiteral("\\bat (.+?):(\\d+)(?::\\d+)?\\s*$"),
            QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator matches =
            locationPattern.globalMatch(output);
    QRegularExpressionMatch last;
    while (matches.hasNext()) {
        last = matches.next();
    }
    if (last.hasMatch()) {
        handleSourceStop(
                QDir::cleanPath(last.captured(1).trimmed()),
                last.captured(2).toInt());
        return;
    }
    if (output.contains(QStringLiteral("exited with status")) ||
        output.contains(QStringLiteral("Process 0 exited"))) {
        return;
    }
    if (editInterruptRequested_) {
        editInterruptRequested_ = false;
        scheduleAdvance();
        return;
    }
    if (stepping_) {
        if (stepMode_ == StepMode::Tick) {
            queueCommand(CommandKind::Continue, QStringLiteral("continue"));
        } else {
            cancelStep();
            setStatus(QStringLiteral(
                    "The debugger step did not resolve to a source line."));
        }
        return;
    }
    if (running_) {
        queueCommand(CommandKind::Continue, QStringLiteral("continue"));
    }
}

void SimulationDebuggerModel::handleSourceStop(
        const QString &absolutePath, int line) {
    const QString relative = relativeSourcePath(absolutePath);
    if (!workerReady_) {
        queueCommand(CommandKind::Continue, QStringLiteral("continue"));
        return;
    }
    if (relative.isEmpty() || line <= 0) {
        if (stepping_) {
            if (stepMode_ == StepMode::Tick) {
                queueCommand(CommandKind::Continue, QStringLiteral("continue"));
            } else {
                finishStep(QStringLiteral(
                        "Step paused outside the explorable source tree."));
            }
        } else if (running_ || pauseRequested_) {
            editInterruptRequested_ = false;
            queueCommand(CommandKind::Continue, QStringLiteral("continue"));
        } else {
            setStatus(QStringLiteral("Paused in the native reference engine."));
        }
        return;
    }
    pauseRequested_ = false;
    editInterruptRequested_ = false;
    atTickBoundary_ = false;
    if (!currentLineKey_.isEmpty() &&
        currentLineKey_ != lineKey(relative, line)) {
        executedLinesThisTick_.insert(currentLineKey_);
    }
    activeFilePath_ = relative;
    activeLine_ = line;
    currentLineKey_ = lineKey(relative, line);
    const int index = sourceIndex(relative);
    if (index < 0) {
        queueCommand(CommandKind::Continue, QStringLiteral("continue"));
        return;
    }
    SourceFile &source = sources_[static_cast<std::size_t>(index)];
    source.executableLines.insert(line);
    selectFile(relative);
    emit filesChanged();
    emit executionChanged();
    emit linesChanged();

    const bool userBreakpoint = source.breakpoints.contains(line);
    if (userBreakpoint && lastBreakpointKey_ != currentLineKey_) {
        lastBreakpointKey_ = currentLineKey_;
        setRunning(false);
        cancelStep();
        setStatus(QStringLiteral("Paused at breakpoint %1:%2.")
                          .arg(fileName(relative))
                          .arg(line));
    } else if (!userBreakpoint) {
        lastBreakpointKey_.clear();
    }

    if (stepping_ && stepMode_ == StepMode::Tick) {
        if (!executedLinesThisTick_.contains(currentLineKey_) &&
            editApplies(source, line)) {
            applyCurrentEdit();
        } else {
            queueCommand(CommandKind::Continue, QStringLiteral("continue"));
        }
    } else if (stepping_) {
        const QString status =
                stepMode_ == StepMode::Substep
                        ? QStringLiteral("Substep completed at %1:%2.")
                                  .arg(fileName(relative))
                                  .arg(line)
                        : QStringLiteral("Source-line step completed at %1:%2.")
                                  .arg(fileName(relative))
                                  .arg(line);
        finishStep(status);
        variables_.clear();
        queueCommand(
                CommandKind::Variables,
                QStringLiteral("frame variable --show-types"));
    } else if (!running_ && !editApplies(source, line)) {
        if (!userBreakpoint) {
            setStatus(QStringLiteral("Paused at %1:%2.")
                              .arg(fileName(relative))
                              .arg(line));
        }
        variables_.clear();
        queueCommand(
                CommandKind::Variables,
                QStringLiteral("frame variable --show-types"));
    } else if (running_) {
        scheduleAdvance();
    }
}

void SimulationDebuggerModel::parseWorkerOutput(const QString &output) {
    const QString marker = QStringLiteral("@FOREVERTAS_STATE ");
    const QString errorMarker = QStringLiteral("@FOREVERTAS_ERROR ");
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (line.startsWith(marker)) {
            handleWorkerState(line.mid(marker.size()).toUtf8());
        } else if (line.startsWith(errorMarker)) {
            const QJsonDocument error = QJsonDocument::fromJson(
                    line.mid(errorMarker.size()).toUtf8());
            failSession(error.object()
                                .value(QStringLiteral("message"))
                                .toString(QStringLiteral(
                                        "Reference worker failed.")));
        }
    }
}

void SimulationDebuggerModel::parseVariables(const QString &output) {
    variables_.clear();
    static const QRegularExpression variablePattern(
            QStringLiteral("^\\s*\\(([^)]*)\\)\\s+([^\\s=]+)\\s+=\\s+(.*)$"));
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = variablePattern.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        variables_.push_back(
                Variable{
                        match.captured(2),
                        match.captured(3).trimmed(),
                        match.captured(1).trimmed(),
                        match.captured(2),
                        0});
        if (variables_.size() >= 512u) {
            break;
        }
    }
    std::sort(
            variables_.begin(),
            variables_.end(),
            [](const Variable &left, const Variable &right) {
                return left.name < right.name;
            });
    emit variablesChanged();
    emit linesChanged();
}

bool SimulationDebuggerModel::debuggerCommandFailed(
        const QString &output) const {
    static const QRegularExpression errorPattern(
            QStringLiteral(
                    "(error:|Errors occurred while evaluating|"
                    "<user expression [^>]*>:\\d+:\\d+: error:)"),
            QRegularExpression::CaseInsensitiveOption);
    return errorPattern.match(output).hasMatch();
}

void SimulationDebuggerModel::setStatus(const QString &status) {
    if (statusText_ == status) {
        return;
    }
    statusText_ = status;
    emit stateChanged();
}

void SimulationDebuggerModel::handleWorkerState(const QByteArray &json) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        failSession(
                QStringLiteral("Reference worker emitted an invalid state."));
        return;
    }
    const QJsonObject state = document.object();
    workerReady_ = true;
    executionTick_ =
            static_cast<qint64>(state.value(QStringLiteral("tick")).toDouble());
    QVariantMap frame;
    frame.insert(QStringLiteral("tick"), executionTick_);
    frame.insert(
            QStringLiteral("timeMs"),
            static_cast<qint64>(
                    state.value(QStringLiteral("timeMs")).toDouble()));
    frame.insert(
            QStringLiteral("durationMs"),
            static_cast<qint64>(
                    state.value(QStringLiteral("durationMs")).toDouble()));
    frame.insert(
            QStringLiteral("position"),
            JsonVector(state.value(QStringLiteral("position"))));
    frame.insert(
            QStringLiteral("rotation"),
            JsonVector(state.value(QStringLiteral("rotation"))));
    frame.insert(
            QStringLiteral("linearSpeed"),
            JsonVector(state.value(QStringLiteral("linearSpeed"))));
    frame.insert(
            QStringLiteral("angularSpeed"),
            JsonVector(state.value(QStringLiteral("angularSpeed"))));
    frame.insert(
            QStringLiteral("force"),
            JsonVector(state.value(QStringLiteral("force"))));
    frame.insert(
            QStringLiteral("torque"),
            JsonVector(state.value(QStringLiteral("torque"))));
    frame.insert(
            QStringLiteral("accelerate"),
            state.value(QStringLiteral("accelerate")).toDouble());
    frame.insert(
            QStringLiteral("brake"),
            state.value(QStringLiteral("brake")).toDouble());
    frame.insert(
            QStringLiteral("steering"),
            state.value(QStringLiteral("steering")).toDouble());
    frame.insert(
            QStringLiteral("checkpointsCollected"),
            state.value(QStringLiteral("checkpointsCollected")).toInt());
    frame.insert(
            QStringLiteral("checkpointsTotal"),
            state.value(QStringLiteral("checkpointsTotal")).toInt());
    frame.insert(
            QStringLiteral("completedLaps"),
            state.value(QStringLiteral("completedLaps")).toInt());
    frame.insert(
            QStringLiteral("totalLaps"),
            state.value(QStringLiteral("totalLaps")).toInt());
    frame.insert(
            QStringLiteral("raceCompleted"),
            state.value(QStringLiteral("raceCompleted")).toBool());
    if (state.value(QStringLiteral("finishTimeMs")).isDouble()) {
        frame.insert(
                QStringLiteral("finishTimeMs"),
                static_cast<qint64>(
                        state.value(QStringLiteral("finishTimeMs"))
                                .toDouble()));
    }
    emit frameProduced(frame);
    emit executionChanged();
}

void SimulationDebuggerModel::scheduleAdvance() {
    if (!active_ || !running_ || compiling_ || commandInFlight_ ||
        advanceScheduled_) {
        return;
    }
    advanceScheduled_ = true;
    QTimer::singleShot(8, this, [this]() {
        advanceScheduled_ = false;
        advanceExecution();
    });
}

bool SimulationDebuggerModel::beginStep(StepMode mode) {
    if (mode == StepMode::None ||
        (mode == StepMode::Tick ? !canStepTick() : !canStepSource())) {
        return false;
    }
    pauseRequested_ = false;
    editError_.clear();
    stepMode_ = mode;
    stepping_ = true;
    emit stateChanged();
    emit executionChanged();

    const int index = sourceIndex(activeFilePath_);
    if (index >= 0 && activeLine_ > 0 &&
        !executedLinesThisTick_.contains(currentLineKey_) &&
        editApplies(sources_[static_cast<std::size_t>(index)], activeLine_)) {
        setStatus(QStringLiteral(
                "Executing the edited source line before stepping."));
        applyCurrentEdit();
        return true;
    }
    queueStepCommand();
    return true;
}

void SimulationDebuggerModel::queueStepCommand() {
    switch (stepMode_) {
    case StepMode::Substep:
        setStatus(QStringLiteral("Advancing one native substep..."));
        queueCommand(CommandKind::Substep, QStringLiteral("thread step-inst"));
        break;
    case StepMode::SourceLine:
        setStatus(QStringLiteral("Advancing one source line..."));
        queueCommand(
                CommandKind::SourceLineStep,
                QStringLiteral("thread step-over"));
        break;
    case StepMode::Tick:
        setStatus(QStringLiteral("Advancing one physics tick..."));
        queueCommand(CommandKind::Continue, QStringLiteral("continue"));
        break;
    case StepMode::None:
        break;
    }
}

void SimulationDebuggerModel::finishStep(const QString &status) {
    if (!stepping_) {
        return;
    }
    stepping_ = false;
    stepMode_ = StepMode::None;
    if (!status.isEmpty()) {
        setStatus(status);
    }
    emit stateChanged();
    emit executionChanged();
}

void SimulationDebuggerModel::cancelStep() {
    if (!stepping_ && stepMode_ == StepMode::None) {
        return;
    }
    stepping_ = false;
    stepMode_ = StepMode::None;
    emit stateChanged();
    emit executionChanged();
}

void SimulationDebuggerModel::advanceExecution() {
    if (!active_ || !running_ || compiling_ || commandInFlight_) {
        return;
    }
    if (atTickBoundary_) {
        queueCommand(CommandKind::Continue, QStringLiteral("continue"));
        return;
    }
    const int index = sourceIndex(activeFilePath_);
    if (index >= 0 && activeLine_ > 0 &&
        !executedLinesThisTick_.contains(currentLineKey_) &&
        editApplies(sources_[static_cast<std::size_t>(index)], activeLine_)) {
        applyCurrentEdit();
        return;
    }
    queueCommand(CommandKind::Continue, QStringLiteral("continue"));
}

void SimulationDebuggerModel::applyCurrentEdit() {
    const int index = sourceIndex(activeFilePath_);
    if (index < 0 || activeLine_ <= 0) {
        scheduleAdvance();
        return;
    }
    const SourceFile &source = sources_[static_cast<std::size_t>(index)];
    if (activeLine_ > source.currentLines.size()) {
        scheduleAdvance();
        return;
    }
    editError_.clear();
    QString expression = source.currentLines[activeLine_ - 1].trimmed();
    if (expression.isEmpty() ||
        expression.startsWith(QStringLiteral("//"))) {
        setStatus(QStringLiteral(
                "Skipping the removed C++ source statement."));
        jumpPastCurrentStatement();
        return;
    }
    compiling_ = true;
    setStatus(QStringLiteral(
            "Compiling edited C++ in the live physics frame..."));
    emit stateChanged();
    if (expression.endsWith(QLatin1Char(';'))) {
        expression.chop(1);
    }
    expression.prepend(QStringLiteral("expression -- "));
    queueCommand(CommandKind::EvaluateEdit, expression);
}

void SimulationDebuggerModel::jumpPastCurrentStatement() {
    const int index = sourceIndex(activeFilePath_);
    if (index < 0) {
        failSession(QStringLiteral("Edited source file was not found."));
        return;
    }
    const SourceFile &source = sources_[static_cast<std::size_t>(index)];
    const int nextLine = statementEndLine(source, activeLine_) + 1;
    const QString command =
            QStringLiteral("thread jump --file %1 --line %2")
                    .arg(quoteDebuggerArgument(source.absolutePath))
                    .arg(nextLine);
    queueCommand(CommandKind::JumpAfterEdit, command);
}

void SimulationDebuggerModel::clearExecutionLocation() {
    activeLine_ = -1;
    activeFilePath_.clear();
    emit executionChanged();
    emit linesChanged();
    emit filesChanged();
}

void SimulationDebuggerModel::failSession(const QString &message) {
    if (stopping_) {
        return;
    }
    const bool wasActive = active_;
    setRunning(false);
    cancelStep();
    active_ = false;
    compiling_ = false;
    commandInFlight_ = false;
    advanceScheduled_ = false;
    commandQueue_.clear();
    hasCurrentCommand_ = false;
    editError_ = message;
    setStatus(message);
    emit linesChanged();
    emit stateChanged();
    if (debugger_.state() != QProcess::NotRunning) {
        debugger_.write("quit\n");
        QTimer::singleShot(250, this, [this]() {
            if (!active_ && !stopping_ &&
                debugger_.state() != QProcess::NotRunning) {
                debugger_.terminate();
            }
        });
    }
    if (wasActive) {
        emit sessionFinished();
    }
}

void SimulationDebuggerModel::setRunning(bool value) {
    if (running_ == value) {
        return;
    }
    running_ = value;
    emit stateChanged();
}

} // namespace forevertas::viewer
