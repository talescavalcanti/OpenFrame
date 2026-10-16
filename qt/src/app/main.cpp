#include "EditorModel.h"
#include "FilterPreviews.h"
#include "Snapshot.h"

#include <QCommandLineParser>
#include <QDirIterator>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStyleHints>

#ifdef Q_OS_LINUX
#include <filesystem>
#endif

namespace {

// No AppImage, o Qt usa só os plugins e os módulos QML do pacote. O QT_PLUGIN_PATH e o
// QML_IMPORT_PATH de fora (um Qt de desenvolvimento, por exemplo) se somam aos do pacote, e
// um plugin de outro Qt pode derrubar o app: o de mídia FFmpeg pede a libva-drm e aborta sem
// ela. O AppImage abre pelo AppRun, um link para usr/bin/OpenFrame, e o linuxdeploy põe o
// qt.conf ao lado do executável; o build não tem nenhum dos dois.
void useOnlyBundledQt()
{
#ifdef Q_OS_LINUX
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path bin = fs::canonical("/proc/self/exe", error).parent_path();
    if (error || !fs::exists(bin / "qt.conf", error) || !fs::exists(bin.parent_path().parent_path() / "AppRun", error))
        return;
    for (const char *name : {"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"})
        qunsetenv(name);
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    useOnlyBundledQt();
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("OpenFrame"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("openframe.app"));
    QGuiApplication::setApplicationName(QStringLiteral("OpenFrame"));
    QGuiApplication::setApplicationVersion(QStringLiteral(OPENFRAME_VERSION));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/OpenFrame/icons/openframe-256.png")));

    // Sem GPU (modo de captura offscreen e algumas máquinas virtuais), o Qt Quick
    // desenha por software; todos os itens do app funcionam assim.
    if (QGuiApplication::platformName() == QLatin1String("offscreen") || qEnvironmentVariableIsSet("OPENFRAME_SOFTWARE"))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Inter (pesos 200 a 500) e os ícones Lucide vêm no próprio executável.
    QDirIterator fonts(QStringLiteral(":/qt/qml/OpenFrame/fonts"), {QStringLiteral("*.ttf")});
    while (fonts.hasNext())
        QFontDatabase::addApplicationFont(fonts.next());
    QFont font(QStringLiteral("Inter"));
    font.setPixelSize(12);
    QGuiApplication::setFont(font);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OpenFrame, editor de vídeo"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption openOption({QStringLiteral("o"), QStringLiteral("open")},
                                  QStringLiteral("Abre um projeto .openframe direto, fora da biblioteca."),
                                  QStringLiteral("arquivo"));
    parser.addOption(openOption);
    parser.addPositionalArgument(QStringLiteral("projeto"), QStringLiteral("Projeto .openframe a importar e abrir."));
    parser.process(app);

    of::EditorModel model;
    if (parser.isSet(openOption))
        model.openExternal(parser.value(openOption));
    else if (!parser.positionalArguments().isEmpty())
        model.openProjectFile(QUrl::fromLocalFile(parser.positionalArguments().first()));

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("filters"), new of::FilterPreviews);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("OpenFrame", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    of::Snapshot::runIfRequested(&engine, &model);
    return app.exec();
}
