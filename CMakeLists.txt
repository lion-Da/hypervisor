cmake_minimum_required(VERSION 3.20)

#set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

project(hypervisor LANGUAGES C CXX)

##########################################

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

##########################################

#set(WDK_WINVER "0x0603" CACHE STRING "Default WINVER for WDK targets")

##########################################
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded$<$<CONFIG:Debug>:Debug>)

if(MSVC)
  add_link_options(
    $<$<NOT:$<STREQUAL:${CMAKE_MSVC_RUNTIME_LIBRARY},MultiThreaded>>:/NODEFAULTLIB:libcmt.lib>
    $<$<NOT:$<STREQUAL:${CMAKE_MSVC_RUNTIME_LIBRARY},MultiThreadedDLL>>:/NODEFAULTLIB:msvcrt.lib>
    $<$<NOT:$<STREQUAL:${CMAKE_MSVC_RUNTIME_LIBRARY},MultiThreadedDebug>>:/NODEFAULTLIB:libcmtd.lib>
    $<$<NOT:$<STREQUAL:${CMAKE_MSVC_RUNTIME_LIBRARY},MultiThreadedDebugDLL>>:/NODEFAULTLIB:msvcrtd.lib>
  )
endif()

##########################################

include(cmake/utils.cmake)

##########################################

set_new_artifact_directory()
enable_driver_support()

##########################################

set(OPT_DEBUG "/Od /Ob0 /Zi")
set(OPT_RELEASE "/O2 /Ob2 /Zi")

add_link_options(/DEBUG)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${OPT_DEBUG}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${OPT_DEBUG}")

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${OPT_RELEASE}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OPT_RELEASE}")


##########################################

add_subdirectory_and_get_targets("external" EXTERNAL_TARGETS)
add_subdirectory_and_get_targets("src" SRC_TARGETS)
targets_set_warnings_as_errors(${SRC_TARGETS})


# 添加驱动签名和部署目标
if(WIN32)
    # 获取hyperhook_driver目标的输出路径
    set(DRIVER_OUTPUT_NAME "hyperhook") 
    set(DRIVER_OUTPUT_PATH "${CMAKE_BINARY_DIR}/artifacts-debug/${DRIVER_OUTPUT_NAME}.sys")
    message(STATUS "DRIVER_OUTPUT_PATH: ${DRIVER_OUTPUT_PATH}")
    # 添加签名目标
    add_custom_target(sign_driver ALL
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cert/sign.bat "$<TARGET_FILE:driver>"
        DEPENDS driver
        COMMENT "Signing driver..."
        VERBATIM
    )

    # 添加部署目标
    add_custom_target(deploy_to_vm ALL
        COMMAND powershell -ExecutionPolicy Bypass -File 
                "${CMAKE_CURRENT_SOURCE_DIR}/deploy_to_vm.ps1"
                -BuildConfig $<CONFIG>
                -ArtifactPath "${ARTIFACT_DIRECTORY}"
        DEPENDS sign_driver
        COMMENT "Deploying to VM..."
        VERBATIM
    )
endif()