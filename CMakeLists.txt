cmake_minimum_required(VERSION 3.22.1)
project(dantes_inferno LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    add_compile_options(-mssse3 -msse4.1)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/lib/cmake/rexglue")
    list(PREPEND CMAKE_FIND_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk")
    set(rexglue_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/lib/cmake/rexglue" CACHE PATH "" FORCE)
    set(REXSDK_DIR "" CACHE PATH "" FORCE)
endif()

include(generated/rexglue.cmake)

if(TARGET rex::rexglue)
    set_target_properties(rex::rexglue PROPERTIES
        IMPORTED_LOCATION "/usr/bin/true"
    )
endif()
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE RelWithDebInfo Release "")

if(REXSDK_DIR)
    # The generated depfile only tracks the manifest and XEX. Track the ARM VMX
    # backend too so stale scalar code cannot survive an SDK/codegen update.
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp"
        APPEND
        DEPENDS
            "${REXSDK_DIR}/src/codegen/builders/vector.cpp"
            "${REXSDK_DIR}/include/rex/ppc/intrinsics.h"
    )
endif()

set(DANTESINFERNO_SOURCES
    src/main.cpp
    src/dantes_iso_installer.cpp
)

if(ANDROID)
    if(NOT DEFINED CMAKE_ANDROID_ARCH_ABI AND DEFINED ANDROID_ABI)
        set(CMAKE_ANDROID_ARCH_ABI ${ANDROID_ABI})
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libadrenotools/CMakeLists.txt")
        add_subdirectory(thirdparty/libadrenotools)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
        include("${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
    endif()

    set(DANTES_ANDROID_SOURCES
        src/dantes_driver.cpp
        src/dantes_main_android.cpp
        src/dantes_iso_installer.cpp
        src/rex_app.cpp
        ${GENERATED_SOURCES}
    )

    add_library(dantes_inferno SHARED ${DANTES_ANDROID_SOURCES})
    target_compile_definitions(dantes_inferno PRIVATE
        REXGLUE_BUILD_CONFIG="Release"
        __ANDROID__
    )
    target_link_libraries(dantes_inferno PRIVATE
        rex::runtime
        rex::gpu-xenos
        android
        log
        adrenotools
        aaudio
    )
    target_include_directories(dantes_inferno PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/default"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libadrenotools/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/thirdparty/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/share/rexglue"
    )
    if(TARGET main_hook)
        add_dependencies(dantes_inferno
            main_hook
            hook_impl
            file_redirect_hook
            gsl_alloc_hook
        )
    endif()
elseif(WIN32)
    list(APPEND DANTESINFERNO_SOURCES src/dantes_inferno.rc)
    add_executable(dantes_inferno WIN32 ${DANTESINFERNO_SOURCES})
    rexglue_setup_target(dantes_inferno GPU_PLUGINS xenos)
else()
    add_executable(dantes_inferno ${DANTESINFERNO_SOURCES})
    rexglue_setup_target(dantes_inferno GPU_PLUGINS xenos)
endif()

if(NOT MSVC AND (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64" OR ANDROID_ABI MATCHES "arm64-v8a"))
    # Base ARMv8-A architecture supported across ARM64 devices
    # Optimization flags for ARM64 recompilation:
    # -O3: Maximum optimization level for speed
    # -fomit-frame-pointer: Free up x29 frame pointer register for codegen register allocation
    # -fno-stack-protector: Eliminate 20,000+ stack canary checks inserted across recompiled guest functions
    # -ffp-contract=fast: Allow ARM64 fused multiply-add (fmla/fmls) instructions for vector math
    # -fvectorize: Enable aggressive LLVM loop and SLP vectorization
    target_compile_options(dantes_inferno PRIVATE
        -march=armv8-a
        -O3
        -fomit-frame-pointer
        -fno-stack-protector
        -ffp-contract=fast
        -fvectorize
    )
    if(TARGET dantes_inferno_recomp)
        target_compile_options(dantes_inferno_recomp PRIVATE
            -march=armv8-a
            -O3
            -fomit-frame-pointer
            -fno-stack-protector
            -ffp-contract=fast
            -fvectorize
        )
    endif()

    # Optimize across the many generated recompilation translation units.
    include(CheckIPOSupported)
    check_ipo_supported(RESULT DANTESINFERNO_IPO_SUPPORTED OUTPUT DANTESINFERNO_IPO_ERROR)
    if(DANTESINFERNO_IPO_SUPPORTED)
        set_property(TARGET dantes_inferno PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        if(TARGET dantes_inferno_recomp)
            set_property(TARGET dantes_inferno_recomp
                PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
    else()
        message(WARNING "LTO unavailable for dantes_inferno: ${DANTESINFERNO_IPO_ERROR}")
    endif()
endif()

if(ANDROID)
    # Ensure symbols needed by SDLActivity / JNI entrypoints remain exported
    set_target_properties(dantes_inferno PROPERTIES
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN FALSE
    )
    # Android 15+ 16 KB page size alignment:
    target_link_options(dantes_inferno PRIVATE "-Wl,-z,max-page-size=16384")
    if(TARGET main_hook)
        target_link_options(main_hook PRIVATE "-Wl,-z,max-page-size=16384")
        target_link_options(hook_impl PRIVATE "-Wl,-z,max-page-size=16384")
        target_link_options(file_redirect_hook PRIVATE "-Wl,-z,max-page-size=16384")
        target_link_options(gsl_alloc_hook PRIVATE "-Wl,-z,max-page-size=16384")
    endif()
endif()

target_include_directories(dantes_inferno PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/thirdparty/imgui"
)

option(DANTESINFERNO_BUILD_LAUNCHER "Build the native Qt launcher" ON)
option(DANTESINFERNO_LAUNCHER_BUILD_TESTS "Build Qt launcher tests" ON)
if(DANTESINFERNO_BUILD_LAUNCHER AND UNIX AND NOT APPLE)
    find_package(Qt6 6.4 COMPONENTS Core Gui Widgets QUIET)
    if(Qt6_FOUND)
        if(DANTESINFERNO_LAUNCHER_BUILD_TESTS)
            enable_testing()
        endif()
        add_subdirectory(launcher-linux)
    else()
        message(WARNING
            "Qt 6.4+ was not found; dantes_inferno_launcher will not be built")
    endif()
endif()
