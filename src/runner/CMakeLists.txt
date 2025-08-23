file(GLOB_RECURSE runner_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB_RECURSE runner_headers CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)


add_executable(runner #WIN32
	${runner_sources}
	${runner_headers}
)

set_target_properties(runner PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

set_property(TARGET runner APPEND_STRING PROPERTY LINK_FLAGS " /MANIFESTUAC:\"level='requireAdministrator'\"")

target_link_libraries(runner PRIVATE
	library
)

target_compile_options(runner PRIVATE
    "/wd4819"  # 禁用C4819警告（文件包含不能在当前代码页中表示的字符）
)

target_include_directories(runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

set_source_files_properties(resource.rc PROPERTIES LANGUAGE RC)
target_sources(runner PRIVATE
	resource.rc
)
