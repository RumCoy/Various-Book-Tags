cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
endif()
project(VariousBookTags VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(COMMONLIB_PREBUILT_MULTICONFIG ON CACHE BOOL "" FORCE)
add_subdirectory(lib/commonlibsse-ng EXCLUDE_FROM_ALL)
if(NOT COMMAND add_commonlibsse_plugin)
    include(lib/commonlibsse-ng/cmake/CommonLibSSE.cmake)
endif()

set(VBT_INTERNAL_DATA_PATH "${CMAKE_CURRENT_BINARY_DIR}/VariousBookTags_InternalData.ini")
set(VBT_RESOURCE_HEADER_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/Resource.h")
set(VBT_INTERNAL_DATA_PREMERGED_PATH "" CACHE FILEPATH
    "Optional premerged internal-data INI produced by CI")
set(VBT_SHARED_PARENT_BASE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/SharedParentList_Base.ini")
set(VBT_CURATED_DATA_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/VariousBookTags_Curated.ini")
set(VBT_MERGE_SCRIPT_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Merge-TagLayers.ps1")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${VBT_SHARED_PARENT_BASE_PATH}"
    "${VBT_CURATED_DATA_PATH}"
    "${VBT_MERGE_SCRIPT_PATH}"
)

if(VBT_INTERNAL_DATA_PREMERGED_PATH)
    if(NOT EXISTS "${VBT_INTERNAL_DATA_PREMERGED_PATH}")
        message(FATAL_ERROR
            "Premerged internal-data INI not found: ${VBT_INTERNAL_DATA_PREMERGED_PATH}")
    endif()
    configure_file(
        "${VBT_INTERNAL_DATA_PREMERGED_PATH}"
        "${VBT_INTERNAL_DATA_PATH}"
        COPYONLY
    )
else()
    find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
    execute_process(
        COMMAND "${POWERSHELL_EXECUTABLE}"
            -NoProfile
            -ExecutionPolicy Bypass
            -File "${VBT_MERGE_SCRIPT_PATH}"
            -Product VBT
            -BasePath "${VBT_SHARED_PARENT_BASE_PATH}"
            -CuratedPath "${VBT_CURATED_DATA_PATH}"
            -OutputPath "${VBT_INTERNAL_DATA_PATH}"
        RESULT_VARIABLE VBT_MERGE_RESULT
        OUTPUT_VARIABLE VBT_MERGE_OUTPUT
        ERROR_VARIABLE VBT_MERGE_ERROR
    )
    if(NOT VBT_MERGE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Failed to merge VBT internal data:\n${VBT_MERGE_OUTPUT}\n${VBT_MERGE_ERROR}")
    endif()
endif()
configure_file(
    src/VariousBookTags.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/VariousBookTags.rc"
    @ONLY
)

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME "Various Book Tags"
    AUTHOR "Various Book Tags contributors"
    SOURCES
        src/main.cpp
        src/PCH.h
        src/BookProcessor.cpp
        src/BookProcessor.h
        src/Config.cpp
        src/Config.h
        src/Menu.cpp
        src/Menu.h
        src/EmbeddedData.cpp
        src/EmbeddedData.h
        src/FormOrigin.cpp
        src/FormOrigin.h
        src/Resource.h
        "${CMAKE_CURRENT_BINARY_DIR}/VariousBookTags.rc"
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "VariousBookTags")

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/skse-menu-framework/resources")

target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION "SKSE/Plugins"
    LIBRARY DESTINATION "SKSE/Plugins"
)
