#ifndef FOREVERTAS_VIEWER_SIMULATION_DEBUGGER_MODEL_H
#define FOREVERTAS_VIEWER_SIMULATION_DEBUGGER_MODEL_H

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QStringList>
#include <QVariantList>

#include <vector>

namespace forevertas::viewer {

class SimulationDebuggerModel final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool compiling READ compiling NOTIFY stateChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY stateChanged)
    Q_PROPERTY(QVariantList fileEntries READ fileEntries NOTIFY filesChanged)
    Q_PROPERTY(
            QString selectedFilePath READ selectedFilePath NOTIFY
                    selectionChanged)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY linesChanged)
    Q_PROPERTY(int activeLine READ activeLine NOTIFY executionChanged)
    Q_PROPERTY(
            QString activeFilePath READ activeFilePath NOTIFY executionChanged)
    Q_PROPERTY(QVariantList variables READ variables NOTIFY variablesChanged)
    Q_PROPERTY(
            QVariantList pinnedVariables READ pinnedVariables NOTIFY
                    variablesChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString editError READ editError NOTIFY linesChanged)
    Q_PROPERTY(bool hasEdits READ hasEdits NOTIFY filesChanged)
    Q_PROPERTY(qint64 executionTick READ executionTick NOTIFY executionChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY
                       themeChanged)

  public:
    explicit SimulationDebuggerModel(QObject *parent = nullptr);
    ~SimulationDebuggerModel() override;

    bool available() const;
    bool active() const;
    bool running() const;
    bool compiling() const;
    QString backendName() const;
    QVariantList fileEntries() const;
    QString selectedFilePath() const;
    QVariantList lines() const;
    int activeLine() const;
    QString activeFilePath() const;
    QVariantList variables() const;
    QVariantList pinnedVariables() const;
    QString statusText() const;
    QString editError() const;
    bool hasEdits() const;
    qint64 executionTick() const;
    bool darkMode() const;

    Q_INVOKABLE bool selectFile(const QString &path);
    Q_INVOKABLE void toggleFolder(const QString &path);
    Q_INVOKABLE bool updateLine(int lineNumber, const QString &text);
    Q_INVOKABLE bool toggleBreakpoint(const QString &path, int lineNumber);
    Q_INVOKABLE bool togglePinned(const QString &name);
    Q_INVOKABLE void resetEdits();
    void setDarkMode(bool value);

    void configure(const QString &backendName);
    bool startSession(const QString &packsDirectory, const QString &replayPath);
    void stopSession();
    void play();
    void pause();

  signals:
    void stateChanged();
    void filesChanged();
    void selectionChanged();
    void linesChanged();
    void executionChanged();
    void variablesChanged();
    void frameProduced(const QVariantMap &frame);
    void sessionFinished();
    void themeChanged();

  private:
    struct SourceEdit {
        qint64 effectiveTick = 0;
    };

    struct SourceFile {
        QString path;
        QString absolutePath;
        QStringList originalLines;
        QStringList currentLines;
        QHash<int, SourceEdit> edits;
        QSet<int> breakpoints;
        QSet<int> executableLines;
    };

    struct Variable {
        QString name;
        QString value;
        QString type;
        QString evaluateName;
        int variablesReference = 0;
    };

    enum class CommandKind {
        Setting,
        FunctionBreakpoint,
        SourceBreakpoint,
        Run,
        Continue,
        Variables,
        EvaluateEdit,
        JumpAfterEdit,
        Quit,
    };

    struct DebuggerCommand {
        CommandKind kind = CommandKind::Setting;
        QString text;
        QString sourcePath;
        int line = 0;
    };

    QString syntaxHighlighted(const QString &text) const;
    static QString fileName(const QString &path);
    static int depth(const QString &path);
    static QString lldbExecutablePath();
    static QString scriptExecutablePath();
    static QString workerExecutablePath();
    static QString sourceRootPath();
    static QString quoteDebuggerArgument(const QString &value);
    static QString quoteShellArgument(const QString &value);

    int sourceIndex(const QString &path) const;
    SourceFile *selectedSource();
    const SourceFile *selectedSource() const;
    QVariantList visibleFileEntries() const;
    QString inlineValues(const QString &line) const;
    QString relativeSourcePath(const QString &absolutePath) const;
    QString lineKey(const QString &path, int line) const;
    int statementEndLine(const SourceFile &source, int line) const;
    bool editApplies(const SourceFile &source, int line) const;
    void loadSources();
    void syncSourceBreakpoints(SourceFile &source);
    void installSourceBreakpoint(SourceFile &source, int line);
    void queueCommand(
            CommandKind kind,
            const QString &text,
            const QString &sourcePath = {},
            int line = 0);
    void sendNextCommand();
    void readDebuggerOutput();
    void consumeDebuggerPrompts();
    void
    handleCommandResult(const DebuggerCommand &command, const QString &output);
    void handleDebuggerStop(const QString &output);
    void handleSourceStop(const QString &absolutePath, int line);
    void parseWorkerOutput(const QString &output);
    void parseVariables(const QString &output);
    bool debuggerCommandFailed(const QString &output) const;
    void setStatus(const QString &status);
    void handleWorkerState(const QByteArray &json);
    void scheduleAdvance();
    void advanceExecution();
    void applyCurrentEdit();
    void jumpPastCurrentStatement();
    void clearExecutionLocation();
    void failSession(const QString &message);
    void setRunning(bool value);

    std::vector<SourceFile> sources_;
    std::vector<Variable> variables_;
    QSet<QString> expandedFolders_;
    QSet<QString> pinnedNames_;
    QSet<QString> executedLinesThisTick_;
    QSet<QString> installedBreakpointKeys_;
    QQueue<DebuggerCommand> commandQueue_;
    DebuggerCommand currentCommand_;
    QProcess debugger_;
    QString selectedFilePath_;
    QString backendName_ = QStringLiteral("Reference");
    QString statusText_;
    QString editError_;
    QString debuggerBuffer_;
    QString packsDirectory_;
    QString replayPath_;
    QString currentLineKey_;
    int activeLine_ = -1;
    QString activeFilePath_;
    qint64 executionTick_ = 0;
    QString lastBreakpointKey_;
    bool available_ = false;
    bool active_ = false;
    bool running_ = false;
    bool compiling_ = false;
    bool setupQueued_ = false;
    int startupPromptsRemaining_ = 0;
    bool hasCurrentCommand_ = false;
    bool commandInFlight_ = false;
    bool advanceScheduled_ = false;
    bool pauseRequested_ = false;
    bool editInterruptRequested_ = false;
    bool handlingDebuggerOutput_ = false;
    bool atTickBoundary_ = false;
    bool workerReady_ = false;
    bool stopping_ = false;
    bool darkMode_ = false;
};

} // namespace forevertas::viewer

#endif
