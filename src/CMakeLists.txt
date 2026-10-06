set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

add_subdirectory(nodeeditor-master nodeeditor_build)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    client.cpp
    client.hpp
    interpret_generator.cpp
    interpret_generator.h
    DynamicPortsModel.cpp
    DynamicPortsModel.hpp
    PortAddRemoveWidget.cpp
    PortAddRemoveWidget.hpp
    spec_parser/automaton-data.cpp
    spec_parser/automaton-data.hpp
    spec_parser/automaton-parser.cpp
    spec_parser/automaton-parser.hpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(fsm-editor
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(fsm-editor SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(fsm-editor
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(fsm-editor PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    QtNodes
)

# Copy Python interpreter core next to the executable
add_custom_command(
    TARGET fsm-editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/interpret/fsm_core
            $<TARGET_FILE_DIR:fsm-editor>/interpret/fsm_core
)

if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.fsm-editor)
endif()

set_target_properties(fsm-editor PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS fsm-editor
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(fsm-editor)
endif()

