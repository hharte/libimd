# CMakeLists.txt for libimd

cmake_minimum_required(VERSION 3.24)

# --- Versioning ---
find_package(Git QUIET)
set(FULL_GIT_VERSION "0.0.0-nogit")     # Default full descriptive version
set(CMAKE_PROJECT_VERSION "0.0.0")      # Default version for project() command

if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    # 1. Get the full descriptive version (tags, hash, dirty status)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _full_git_version_temp
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    if(GIT_DESCRIBE_RESULT EQUAL 0)
        set(FULL_GIT_VERSION ${_full_git_version_temp})
        message(STATUS "Raw Git describe version: ${FULL_GIT_VERSION}")
    else()
        message(WARNING "git describe --tags --dirty --always failed. Using fallback version ${FULL_GIT_VERSION}.")
    endif()

    # 2. Try to get the latest tag and extract Major.Minor.Patch for project()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE LATEST_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_TAG_RESULT
    )
    if(GIT_TAG_RESULT EQUAL 0)
        # Try to extract X.Y.Z or X.Y from tag (allowing optional 'v' prefix)
        string(REGEX MATCH "^v?([0-9]+\\.[0-9]+(\\.[0-9]+)?)" _extracted_version ${LATEST_TAG})
        if(CMAKE_MATCH_1)
            set(CMAKE_PROJECT_VERSION ${CMAKE_MATCH_1})
            message(STATUS "Using semantic version for CMake project: ${CMAKE_PROJECT_VERSION}")
        else()
            message(WARNING "Latest Git tag (${LATEST_TAG}) doesn't match expected format (X.Y or X.Y.Z). Using default ${CMAKE_PROJECT_VERSION} for project().")
        endif()
    else()
        message(WARNING "Could not get latest Git tag. Using default version ${CMAKE_PROJECT_VERSION} for project().")
    endif()

    # If the full git version (e.g., from describe) exactly matches the semantic version tag,
    # it means we are exactly on that tag commit. Mark it as an official release.
    # Note: This assumes your tags match the CMAKE_PROJECT_VERSION format (e.g., "1.2.3", not "v1.2.3")
    # If your tags have a 'v' prefix, adjust the comparison or the CMAKE_PROJECT_VERSION extraction.
    if("${CMAKE_PROJECT_VERSION}" STREQUAL "${FULL_GIT_VERSION}")
        set(FULL_GIT_VERSION "OFFICIAL RELEASE")
        message(STATUS "Current commit matches latest tag. Setting version to OFFICIAL RELEASE.")
    endif()

else()
    message(WARNING "Git not found or not a Git repository. Using fallback versions.")
    # Defaults are already set above
endif()

# Project name and version (using sanitized, CMake-compatible version)
project(libimd VERSION ${CMAKE_PROJECT_VERSION} LANGUAGES C)

# Add version definitions globally for all subsequent targets
add_compile_definitions(
    CMAKE_VERSION_STR="${CMAKE_PROJECT_VERSION}"
    GIT_VERSION_STR="${FULL_GIT_VERSION}"
)

# Standard C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF) # Use pure C11, disable GNU extensions if not needed
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR")

# Define source and test directories relative to CMakeLists.txt
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Library: libimd ---
add_library(libimd STATIC ${SOURCE_DIR}/libimd.c ${SOURCE_DIR}/libimd_utils.c)
target_include_directories(libimd
    PUBLIC ${SOURCE_DIR}
    PRIVATE ${SOURCE_DIR}
)
target_compile_features(libimd PUBLIC c_std_99)

# --- Library: libimdf ---
add_library(libimdf STATIC ${SOURCE_DIR}/libimdf.c)
target_include_directories(libimdf
    PUBLIC ${SOURCE_DIR}
    PRIVATE ${SOURCE_DIR}
)
target_link_libraries(libimdf PRIVATE libimd) # libimdf depends on libimd
target_compile_features(libimdf PUBLIC c_std_99)

# --- Library: libimdchk ---
add_library(libimdchk STATIC ${SOURCE_DIR}/libimdchk.c)
target_include_directories(libimdchk PUBLIC ${SOURCE_DIR})
target_link_libraries(libimdchk PRIVATE libimd) # libimdchk depends on libimd
set_target_properties(libimdchk PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)

# --- Compiler Flags ---
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(COMMON_C_FLAGS -Wall -Wextra -Wpedantic)
elseif(MSVC)
    set(COMMON_C_FLAGS "/W4") # /WX)
    set(COMMON_C_FLAGS "/GR")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS) # Suppress MSVC warnings
endif()

if (COMMON_C_FLAGS)
    target_compile_options(libimd PRIVATE ${COMMON_C_FLAGS})
    target_compile_options(libimdf PRIVATE ${COMMON_C_FLAGS})
    target_compile_options(libimdchk PRIVATE ${COMMON_C_FLAGS})
endif()

# --- Installation (Optional) ---
install(FILES README.md LICENSE DESTINATION .)
install(TARGETS libimd ARCHIVE DESTINATION lib)
install(TARGETS libimdf ARCHIVE DESTINATION lib)
install(TARGETS libimdchk ARCHIVE DESTINATION lib)
install(FILES ${SOURCE_DIR}/libimd.h DESTINATION include)
install(FILES ${SOURCE_DIR}/libimdf.h DESTINATION include)
install(FILES ${SOURCE_DIR}/libimdchk.h DESTINATION include)

# --- CPack configuration ---

# Set basic package information
# These often default correctly based on the project() command,
# but setting them explicitly ensures correctness.
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "Howard M. Harte")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ImageDisk Library (Cross Platform)")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# Set the generator based on the OS
if(WIN32)
  set(CPACK_GENERATOR "ZIP") # Use ZIP archive generator on Windows
else()
  set(CPACK_GENERATOR "TGZ") # Use TGZ (tar.gz) archive generator on other systems (OSX, Linux)
endif()

# Include the CPack module - this must come AFTER setting CPACK variables
include(CPack)

# --- Output ---
message(STATUS "Configuring IMD Library project...")
message(STATUS "  Source directory: ${SOURCE_DIR}")
message(STATUS "  Building library: libimd")
message(STATUS "  Building library: libimdf")
message(STATUS "  Building library: libimdchk")
