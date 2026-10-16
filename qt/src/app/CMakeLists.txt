# App OpenFrame (QML). Os arquivos QML ficam todos em qml/ e entram no módulo
# "OpenFrame" na raiz (alias pelo nome do arquivo), para se enxergarem sem import.
# Multimedia: o núcleo linka (som do Player); o alvo precisa ser visível aqui,
# onde o executável é finalizado.
find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Multimedia)

qt_add_executable(OpenFrame WIN32 MACOSX_BUNDLE
    main.cpp
)

set_target_properties(OpenFrame PROPERTIES
    OUTPUT_NAME OpenFrame
    MACOSX_BUNDLE_GUI_IDENTIFIER app.openframe.qt
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    # O modelo do Qt com o NSMicrophoneUsageDescription: sem ele, o QMicrophonePermission nega no Mac.
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in
)

file(GLOB OPENFRAME_QML CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" qml/*.qml qml/*.js)
foreach(file IN LISTS OPENFRAME_QML)
    get_filename_component(name "${file}" NAME)
    set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${name}")
endforeach()
set_source_files_properties(qml/Theme.qml qml/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(OPENFRAME_FONTS
    fonts/Inter-ExtraLight.ttf
    fonts/Inter-Light.ttf
    fonts/Inter-Regular.ttf
    fonts/Inter-Medium.ttf
    fonts/lucide.ttf
)

# A pasta do módulo (qmldir etc.) fica em qml/OpenFrame: com o padrão, ela se
# chamaria build/src/app/OpenFrame e colidiria com o executável no Linux.
qt_add_qml_module(OpenFrame
    URI OpenFrame
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/OpenFrame"
    QML_FILES ${OPENFRAME_QML}
    SOURCES
        AnimationThumbs.h AnimationThumbs.cpp
        CanvasCore.h CanvasCore.cpp
        CaptionGeneration.h CaptionGeneration.cpp
        CaptionsCore.h CaptionsCore.cpp
        CanvasGeometry.h CanvasGeometry.cpp
        CanvasOverlay.h CanvasOverlay.cpp
        CoreBridge.h CoreBridge.cpp
        EditorCanvas.cpp
        EditorCaptions.cpp
        EditorModel.h EditorModel.cpp
        EditorMotion.cpp
        EditorRecording.cpp
        EditorRows.cpp
        FilterPreviews.h FilterPreviews.cpp
        KeyedListModel.h KeyedListModel.cpp
        MediaCache.h MediaCache.cpp
        MotionCore.h MotionCore.cpp
        PaintedItems.h PaintedItems.cpp
        RecordingSession.h
        Snapshot.h Snapshot.cpp
        TextPresets.h TextPresets.cpp
    RESOURCES
        ${OPENFRAME_FONTS}
        icons/openframe-256.png
)

target_compile_definitions(OpenFrame PRIVATE OPENFRAME_VERSION="${PROJECT_VERSION}")
target_link_libraries(OpenFrame PRIVATE
    openframe_core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
    Qt6::Network
)

# DLLs do whisper/ggml (com as variantes de CPU) ao lado do executável no Windows:
# sem elas, o OpenFrame.exe do build nem abre.
openframe_deploy_whisper(OpenFrame)

# Ícone e versão do executável no Windows.
if(WIN32)
    get_filename_component(OPENFRAME_ICO "${CMAKE_CURRENT_SOURCE_DIR}/../../packaging/windows/OpenFrame.ico" ABSOLUTE)
    configure_file(../../packaging/windows/OpenFrame.rc.in "${CMAKE_CURRENT_BINARY_DIR}/OpenFrame.rc" @ONLY)
    target_sources(OpenFrame PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/OpenFrame.rc")
endif()

include(GNUInstallDirs)
install(TARGETS OpenFrame
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
