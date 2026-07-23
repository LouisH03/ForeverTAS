#include "app/search_controller.h"
#include "viewer/race_viewer_controller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QVariant>

int main(int argc, char **argv) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ForeverTAS"));
    QCoreApplication::setOrganizationDomain(
            QStringLiteral("forevertas.local"));
    QCoreApplication::setApplicationName(QStringLiteral("ForeverTAS"));

    forevertas::app::SearchController controller;
    forevertas::viewer::RaceViewerController viewer;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({
            {QStringLiteral("controller"),
             QVariant::fromValue(static_cast<QObject *>(&controller))},
            {QStringLiteral("viewer"),
             QVariant::fromValue(static_cast<QObject *>(&viewer))}});
    QObject::connect(
            &engine,
            &QQmlApplicationEngine::objectCreationFailed,
            &application,
            []() { QCoreApplication::exit(1); },
            Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("ForeverTAS"),
                          QStringLiteral("Main"));

    if (application.arguments().contains(
                QStringLiteral("--qml-smoke-test"))) {
        QTimer::singleShot(0, &application, &QCoreApplication::quit);
    }
    return application.exec();
}
