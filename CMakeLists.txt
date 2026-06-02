cmake_minimum_required(VERSION 3.23)
project(wgrapheme LANGUAGES C VERSION 0.2.0)
set(CMAKE_C_STANDARD 99)

option(WGRAPHEME_GENERATE_DATA OFF "Download unicode specs for WGRAPHEME_UNICODE_VERSION and re-generate data from them")
option(WGRAPHEME_ENABLE_TESTING OFF "Enable testing")
set(WGRAPHEME_UNICODE_VERSION "17.0.0" CACHE STRING "Specify unicode version to generate data for")

set(PACKAGE_NAME wgrapheme)

add_library(wgrapheme)
add_library(wgrapheme::wgrapheme ALIAS wgrapheme)

target_sources(${PROJECT_NAME}
    PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/wgrapheme.h

    PRIVATE
    src/wgrapheme.c
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
    WGRAPHEME_EXPORTS
)

if (BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME}
        PUBLIC
        WGRAPHEME_SHARED_DEFINE
    )
endif ()


include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# install targets' resulting files
install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PACKAGE_NAME}Targets
    COMPONENT TGuy
    FILE_SET HEADERS
)

install(
    EXPORT ${PACKAGE_NAME}Targets
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PACKAGE_NAME}"
    NAMESPACE ${PACKAGE_NAME}::
)

# Generate main config file
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PACKAGE_NAME}Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_NAME}Config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PACKAGE_NAME}"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_NAME}Version.cmake"
    COMPATIBILITY SameMinorVersion
)

install(
    FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PACKAGE_NAME}Version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PACKAGE_NAME}"
)

set(WGRAPHEME_UCD_DIR "${CMAKE_CURRENT_BINARY_DIR}/ucd/${WGRAPHEME_UNICODE_VERSION}")
set(UCD_BASE_URL "https://www.unicode.org/Public/${WGRAPHEME_UNICODE_VERSION}/ucd")


if (WGRAPHEME_GENERATE_DATA)
    set(WGRAPHEME_UCD_FILES
        "${WGRAPHEME_UCD_DIR}/GraphemeBreakProperty.txt"
        "${WGRAPHEME_UCD_DIR}/DerivedCoreProperties.txt"
        "${WGRAPHEME_UCD_DIR}/emoji-data.txt"
    )

    file(DOWNLOAD
        "${UCD_BASE_URL}/auxiliary/GraphemeBreakProperty.txt"
        "${WGRAPHEME_UCD_DIR}/GraphemeBreakProperty.txt"
        SHOW_PROGRESS
    )

    file(DOWNLOAD
        "${UCD_BASE_URL}/DerivedCoreProperties.txt"
        "${WGRAPHEME_UCD_DIR}/DerivedCoreProperties.txt"
        SHOW_PROGRESS
    )

    file(DOWNLOAD
        "${UCD_BASE_URL}/emoji/emoji-data.txt"
        "${WGRAPHEME_UCD_DIR}/emoji-data.txt"
        SHOW_PROGRESS
    )

    add_executable(wgrapheme_gen_data tools/gen_data.c)
    target_include_directories(wgrapheme_gen_data PRIVATE src)

    add_custom_command(
        OUTPUT "src/wgrapheme_data.h"
        COMMAND wgrapheme_gen_data
        "${WGRAPHEME_UCD_DIR}"
        "${WGRAPHEME_UNICODE_VERSION}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/wgrapheme_data.h"
        DEPENDS
        wgrapheme_gen_data
        ${WGRAPHEME_UCD_FILES}
        COMMENT "Generating wgrapheme Unicode tables"
        VERBATIM
    )

    add_custom_target(wgrapheme_generate_data
        DEPENDS "src/wgrapheme_data.h"
    )

    add_dependencies(wgrapheme wgrapheme_generate_data)
endif ()

if (WGRAPHEME_ENABLE_TESTING)
    file(
        DOWNLOAD
        "${UCD_BASE_URL}/auxiliary/GraphemeBreakTest.txt"
        "${WGRAPHEME_UCD_DIR}/GraphemeBreakTest.txt"
        SHOW_PROGRESS
    )
    add_executable(wgraphemetest test/test_graphemebreak.c)
    target_link_libraries(wgraphemetest wgrapheme)
    add_test(wgraphemetest wgraphemetest "${WGRAPHEME_UCD_DIR}/GraphemeBreakTest.txt")
    enable_testing()
endif ()
