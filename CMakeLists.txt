set(PROJECT_NAME "obs_scene_tree_view")
set(HEADER_DIRECTORY "obs_scene_tree_view")

set(NAMESPACE_NAME "${PROJECT_NAME}")

set(LIBRARY_NAME "${PROJECT_NAME}")
set(EXECUTABLE_NAME "${PROJECT_NAME}Exec")
set(TEST_NAME "${PROJECT_NAME}Tests")

set(LIB_EXPORT_NAME "${LIBRARY_NAME}Targets")
set(LIB_CONFIG_NAME "${LIBRARY_NAME}Config")
set(LIB_VERSION_NAME "${LIB_CONFIG_NAME}Version")

cmake_minimum_required(VERSION 3.20)
project("${PROJECT_NAME}" VERSION 0.0.2)

if(NOT DEFINED BUILD_IN_OBS)
        set(BUILD_IN_OBS OFF)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

if(NOT ${BUILD_IN_OBS})
        set(CMAKE_CXX_STANDARD 20)

        find_package(libobs REQUIRED)
        find_package(obs-frontend-api REQUIRED)

        if(NOT DEFINED LIBOBS_PLUGIN_DESTINATION)
                set(LIBOBS_PLUGIN_DESTINATION "lib/obs-plugins")
        endif()

        if(NOT DEFINED LIBOBS_PLUGIN_DATA_DESTINATION)
                set(LIBOBS_PLUGIN_DATA_DESTINATION "share/obs/obs-plugins")
        endif()

        set(OBS_PLUGIN_LIB_DIR "${LIBOBS_PLUGIN_DESTINATION}" CACHE PATH "Path to obs plugins (relative to CMAKE_INSTALL_PREFIX)")
        set(OBS_PLUGIN_DATA_DIR "${LIBOBS_PLUGIN_DATA_DESTINATION}" CACHE PATH "Path to scene tree view data dir (relative to CMAKE_INSTALL_PREFIX)")

        if(WIN32)
                message(WARNING "Building ${PROJECT_NAME} outside of OBS is currently not supported")
        endif()
endif()

if(NOT TARGET OBS::obs-frontend-api)
    add_library(OBS::obs-frontend-api ALIAS obs-frontend-api)
endif()

set(LIB_SRC_FILES
        obs_scene_tree_view/obs_scene_tree_view.cpp
        obs_scene_tree_view/stv_item_model.cpp
        obs_scene_tree_view/stv_item_view.cpp
)


##########################################
## Version
configure_file(cmake/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/include/${HEADER_DIRECTORY}/version.h" @ONLY)


##########################################
## Qt6 UI
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_AUTOUIC_SEARCH_PATHS} "${CMAKE_CURRENT_SOURCE_DIR}/forms")
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)


##########################################
## Library
add_library(${LIBRARY_NAME} SHARED ${LIB_SRC_FILES} ${VT_UI_HEADERS})
add_library("${NAMESPACE_NAME}::${LIBRARY_NAME}" ALIAS ${LIBRARY_NAME})
target_compile_options(${LIBRARY_NAME} PUBLIC $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-Wall -Wextra>)

set_target_properties(${LIBRARY_NAME} PROPERTIES PREFIX "")

target_include_directories(${LIBRARY_NAME}
        PUBLIC
                "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
                "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
                "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"

        PRIVATE
)

target_link_libraries(${LIBRARY_NAME}
        PUBLIC
                OBS::libobs
                OBS::obs-frontend-api
                Qt6::Widgets

        PRIVATE
)


##########################################
## Install files
if(${BUILD_IN_OBS})
        install_obs_plugin_with_data(${LIBRARY_NAME} data)
else()
        install(TARGETS ${LIBRARY_NAME}
                EXPORT ${LIB_EXPORT_NAME}
                LIBRARY DESTINATION "${OBS_PLUGIN_LIB_DIR}"
                ARCHIVE DESTINATION "${OBS_PLUGIN_LIB_DIR}")

        install(DIRECTORY "data/locale"
                DESTINATION "${OBS_PLUGIN_DATA_DIR}/${PROJECT_NAME}")
endif()
