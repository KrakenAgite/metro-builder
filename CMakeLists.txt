cmake_minimum_required(VERSION 3.16)
project(MetroBuilder VERSION 1.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent)

add_executable(metrobuilder
    src/main.cpp
    src/CityData.h
    src/OsmParser.h src/OsmParser.cpp
    src/OsmLoader.h src/OsmLoader.cpp
    src/VectorTiles.h src/VectorTiles.cpp
    src/Metro.h src/Metro.cpp
    src/Events.cpp
    src/Goals.cpp
    src/Route.cpp
    src/Economy.cpp
    src/Scenario.cpp
    src/Section.cpp
    src/Profile.h src/Profile.cpp
    src/TransitImport.h src/TransitImport.cpp
    src/Achievements.h src/Achievements.cpp
    src/MapView.h src/MapView.cpp
    src/DensityLayer.h src/DensityLayer.cpp
    src/Schematic.h src/Schematic.cpp
    src/Charts.h src/Charts.cpp
    src/Audio.h src/Audio.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/Icons.h src/Icons.cpp
    src/Ui.h src/Ui.cpp
)

target_link_libraries(metrobuilder PRIVATE Qt6::Widgets Qt6::Network Qt6::Concurrent)

# traduction anglaise (translations/metrobuilder_en.ts, compilée avec lrelease) intégrée à l'exécutable
qt_add_resources(metrobuilder "i18n" PREFIX "/i18n" BASE translations FILES translations/metrobuilder_en.qm)

# Son : waveOut sous Windows ; ailleurs PulseAudio (fonctionne aussi avec PipeWire), facultatif.
if(WIN32)
    target_link_libraries(metrobuilder PRIVATE winmm shell32)
    set_target_properties(metrobuilder PROPERTIES WIN32_EXECUTABLE ON) # pas de console
    target_sources(metrobuilder PRIVATE assets/metrobuilder.rc)
    return()
endif()
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(PULSE IMPORTED_TARGET libpulse-simple)
endif()
if(PULSE_FOUND)
    target_link_libraries(metrobuilder PRIVATE PkgConfig::PULSE)
    target_compile_definitions(metrobuilder PRIVATE HAVE_PULSE)
else()
    message(STATUS "libpulse-simple introuvable : le jeu sera compilé sans son")
endif()
