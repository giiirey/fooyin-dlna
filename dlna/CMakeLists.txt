cmake_minimum_required(VERSION 3.16)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC OFF)

# Find Fooyin and its modules (installs FooyinConfig.cmake with targets + macros)
find_package(Fooyin REQUIRED)

create_fooyin_plugin(
    dlna
    JSON_IN dlna.json.in
    DEPENDS
        Fooyin::Core
        Fooyin::Gui
        Fooyin::Utils
    SOURCES
        dlnaplugin.h
        dlnaplugin.cpp
        dlnaentry.h
        dlnadevice.h
        dlnadevice.cpp
        dlnabrowse.h
        dlnabrowse.cpp
        dlnabrowsercontroller.h
        dlnabrowsercontroller.cpp
        dlnatrack.h
        dlnatrack.cpp
        dlnabrowserdialog.h
        dlnabrowserdialog.cpp
        dlnastreamdevice.h
        dlnastreamdevice.cpp
        dlnaremotesourceprovider.h
        dlnaremotesourceprovider.cpp
)
