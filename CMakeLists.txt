cmake_minimum_required(VERSION 3.21)

project(Synsemble VERSION 1.1 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Sql Network)

qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0002 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(AIMeetingTable
    src/main.cpp
    resources.qrc
    src/app/application_context.h
    src/app/application_context.cpp
    src/app/mobile_app_controller.h
    src/app/mobile_app_controller.cpp
    src/core/event_bus.h
    src/core/event_bus.cpp
    src/core/logging.h
    src/core/logging.cpp
    src/core/startup_timeline.h
    src/core/startup_timeline.cpp
    src/core/response_parser.h
    src/core/response_parser.cpp
    src/core/workflow_engine.h
    src/core/workflow_engine.cpp
    src/core/session_runner.h
    src/core/session_runner.cpp
    src/domain/models.h
    src/persistence/database_manager.h
    src/persistence/database_manager.cpp
    src/providers/provider_gateway.h
    src/providers/provider_gateway.cpp
    src/services/credential_store.h
    src/services/credential_store.cpp
    src/services/artifact_manager.h
    src/services/artifact_manager.cpp
    src/services/attachment_import_manager.h
    src/services/attachment_import_manager.cpp
    src/services/budget_manager.h
    src/services/budget_manager.cpp
    src/services/model_catalog_manager.h
    src/services/model_catalog_manager.cpp
    src/services/upload_manager.h
    src/services/upload_manager.cpp
)

target_include_directories(AIMeetingTable PRIVATE src)

target_link_libraries(AIMeetingTable PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    Qt6::Network
)

qt_add_qml_module(AIMeetingTable
    URI AIMeetingTable
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/ConversationPage.qml
        qml/SessionDetails.qml
        qml/SettingsPage.qml
        qml/TranscriptScroll.js
)

if (ANDROID)
    include(FetchContent)
    FetchContent_Declare(android_openssl
        URL https://codeload.github.com/KDAB/android_openssl/tar.gz/b71f1470962019bd89534a2919f5925f93bc5779
        URL_HASH SHA256=6c1dfff0af367bba2f2c2a27f1c2061019537524dc7892ee8efbdc8e1c6224fa
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(android_openssl)
    include("${android_openssl_SOURCE_DIR}/android_openssl.cmake")

    set_target_properties(AIMeetingTable PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "com.aimeetingtable.myapp"
        QT_ANDROID_TARGET_SDK_VERSION 36
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_ABIS "arm64-v8a"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE 5
    )
    add_android_openssl_libraries(AIMeetingTable)

    if (CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        qt_import_plugins(AIMeetingTable EXCLUDE_BY_TYPE qmltooling)
    endif()
endif()

if (WIN32)
    target_link_libraries(AIMeetingTable PRIVATE Advapi32)
endif()

if (BUILD_TESTING AND NOT ANDROID)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test QuickTest)
    add_subdirectory(tests)
endif()
