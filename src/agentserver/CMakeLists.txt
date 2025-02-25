# Agent server
set(akonadi_agent_server_srcs
    agentpluginloader.cpp
    agentserver.cpp
    agentthread.cpp
    main.cpp
)

ecm_qt_declare_logging_category(akonadi_agent_server_srcs HEADER akonadiagentserver_debug.h IDENTIFIER AKONADIAGENTSERVER_LOG CATEGORY_NAME org.kde.pim.akonadiagentserver)

add_executable(akonadi_agent_server ${akonadi_agent_server_srcs})
set_target_properties(akonadi_agent_server PROPERTIES MACOSX_BUNDLE FALSE)

target_link_libraries(akonadi_agent_server
    akonadi_shared
    KF5AkonadiPrivate
    Qt5::Core
    Qt5::DBus
    Qt5::Widgets
)

# Agent plugin launcher
set(akonadi_agent_launcher_SRCS
    agentpluginloader.cpp
    agentlauncher.cpp
    akonadiagentserver_debug.cpp
)

add_executable(akonadi_agent_launcher ${akonadi_agent_launcher_SRCS})
set_target_properties(akonadi_agent_launcher PROPERTIES MACOSX_BUNDLE FALSE)

target_link_libraries(akonadi_agent_launcher
    akonadi_shared
    KF5AkonadiPrivate
    Qt5::Core
    Qt5::Widgets
)

# Install both helper apps.
install(TARGETS akonadi_agent_launcher
          DESTINATION ${BIN_INSTALL_DIR})

install(TARGETS akonadi_agent_server
        ${KF5_INSTALL_TARGETS_DEFAULT_ARGS})
