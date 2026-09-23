cmake_minimum_required(VERSION 3.16)

project(kateantigravity VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Extra CMake Modules (ECM)
find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

# Qt 6
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Test
)

# KDE Frameworks 6
find_package(KF6 6.0 REQUIRED COMPONENTS
    TextEditor
    CoreAddons
    I18n
    XmlGui
    Config
    Wallet
)

add_definitions(-DTRANSLATION_DOMAIN=\"kateantigravity\")

# -----------------------------------------------------------------------------
# Internal core library
#
# All non-entry-point logic lives here as an OBJECT library so it is compiled
# once per consumer and shared by the plugin and every unit test target. This
# removes the previously duplicated (and easily desynchronised) per-test source
# lists. Only the KTextEditor plugin entry points (plugin.cpp / pluginview.cpp)
# stay outside, since tests don't need the XMLGUI client machinery.
# -----------------------------------------------------------------------------
set(KATEANTIGRAVITY_CORE_SOURCES
    src/viewhelper.cpp
    src/inlinenoteprovider.cpp
    src/eventfilter.cpp
    src/contextbuilder.cpp
    src/agyclient.cpp
    src/agyprocesschannel.cpp
    src/agymodels.cpp
    src/editblockparser.cpp
    src/editblockapplier.cpp
    src/gitcommithelper.cpp
    src/chatsession.cpp
    src/chatsessionmanager.cpp
    src/chatwidget.cpp
    src/chathtmlrenderer.cpp
    src/chatcompletionpopup.cpp
    src/slashcommandrouter.cpp
    src/projectfileindexer.cpp
    src/mentionresolver.cpp
    src/symbolindex.cpp
    src/projectdetector.cpp
    src/projectrules.cpp
    src/tokenstats.cpp
    src/settings.cpp
    src/configpage.cpp
    src/agyaccount.cpp
    src/agyinfodialog.cpp
)

add_library(kateantigravity_core STATIC ${KATEANTIGRAVITY_CORE_SOURCES})
set_target_properties(kateantigravity_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(kateantigravity_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(kateantigravity_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        KF6::TextEditor
        KF6::CoreAddons
        KF6::I18n
        KF6::XmlGui
        KF6::ConfigCore
        KF6::Wallet
)

# Plugin Target
#
# The KTextEditor entry points (plugin.cpp / pluginview.cpp) are compiled in the
# plugin target itself rather than in the core library. This keeps the plugin's
# generated moc/metatype compilation unit (mocs_compilation.cpp) alongside the
# rest of the source set it was validated with, avoiding a QVariant metatype
# static_assert that is triggered when plugin.cpp is moc'd in isolation.
kcoreaddons_add_plugin(kateantigravity
    SOURCES
        src/plugin.cpp
        src/pluginview.cpp
        ${KATEANTIGRAVITY_CORE_SOURCES}
    INSTALL_NAMESPACE "kf6/ktexteditor"
)

target_include_directories(kateantigravity
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(kateantigravity
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        KF6::TextEditor
        KF6::CoreAddons
        KF6::I18n
        KF6::XmlGui
        KF6::ConfigCore
        KF6::Wallet
)

# Install UI RC file
install(FILES src/ui.rc
    DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/kateantigravity
)

# Install JSON metadata for plugin discovery
install(FILES kateantigravity.json
    DESTINATION ${KDE_INSTALL_DATADIR}/ktexteditor/plugins
)

# Translations (KI18n)
ki18n_install(po)

# -----------------------------------------------------------------------------
# Unit Tests
#
# Every test compiles only its own test_*.cpp and links the shared core library,
# so there is a single source-of-truth for the implementation sources.
# -----------------------------------------------------------------------------
enable_testing()

# Helper to declare a test that links the shared core library.
function(add_agy_test test_name)
    add_executable(${test_name} test/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE Qt6::Test kateantigravity_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_agy_test(test_ghosttext)
add_agy_test(test_contextbuilder)
add_agy_test(test_chathtmlrenderer)
add_agy_test(test_projectrules_symbols)
add_agy_test(test_editblocks)
add_agy_test(test_gitcommit)
add_agy_test(test_agychannel_integration)
add_agy_test(test_agyclient)
add_agy_test(test_chatsession)
add_agy_test(test_slashandmentions)
add_agy_test(test_chatsessionmanager)

add_agy_test(test_projectdetector)
target_compile_definitions(test_projectdetector PRIVATE CMAKE_CURRENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
