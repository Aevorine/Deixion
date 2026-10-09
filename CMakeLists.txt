cmake_minimum_required(VERSION 3.25)
project(Deixion VERSION 1.0.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(DX_ROOT ${CMAKE_SOURCE_DIR})
set(DX_GEN ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${DX_GEN})
find_program(NODE_EXE node)
set(DX_PLACEHOLDERS "")
if(NOT NODE_EXE)
  list(APPEND DX_PLACEHOLDERS ui_pack.rc)
endif()
foreach(f ${DX_PLACEHOLDERS})
  if(NOT EXISTS ${DX_GEN}/${f})
    file(WRITE ${DX_GEN}/${f} "")
  endif()
endforeach()

set(DX_INTERNAL Deixion)
set(DX_ORIGINAL Deixion.exe)
configure_file(res/app.rc.in ${DX_GEN}/app.rc @ONLY)
set(DX_INTERNAL deixion-cli)
set(DX_ORIGINAL deixion-cli.exe)
configure_file(res/app.rc.in ${DX_GEN}/cli.rc @ONLY)
set(DX_INTERNAL deixion-setup)
set(DX_ORIGINAL Deixion-Setup-x64.exe)
configure_file(res/app.rc.in ${DX_GEN}/setup.rc @ONLY)

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00
  DX_VERSION="${PROJECT_VERSION}" DX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} DX_VERSION_MINOR=${PROJECT_VERSION_MINOR} DX_VERSION_PATCH=${PROJECT_VERSION_PATCH})
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type-mismatch -Wno-unknown-pragmas)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
add_link_options(-static)

file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
add_library(dxcore STATIC ${CORE_SRC})
target_include_directories(dxcore PUBLIC src)
target_link_libraries(dxcore PUBLIC ole32 oleaut32 uuid uiautomationcore windowscodecs d3d11 dxgi dwmapi
  user32 gdi32 shell32 advapi32 winhttp bcrypt shlwapi psapi version imm32 comctl32 uxtheme)

# 界面文件打成一个包，作为资源嵌进 Deixion.exe；没有 node 时退化为读磁盘（DEIXION_UI_DIR）。
if(NODE_EXE)
  file(GLOB_RECURSE UI_FILES CONFIGURE_DEPENDS ui/*)
  add_custom_command(OUTPUT ${DX_GEN}/ui.pack ${DX_GEN}/ui_pack.rc
    COMMAND ${NODE_EXE} ${DX_ROOT}/tools/pack-ui.mjs ${DX_ROOT}/ui ${DX_GEN}/ui.pack ${DX_GEN}/ui_pack.rc
    DEPENDS ${UI_FILES} ${DX_ROOT}/tools/pack-ui.mjs VERBATIM)
  set_source_files_properties(${DX_GEN}/ui_pack.rc PROPERTIES OBJECT_DEPENDS ${DX_GEN}/ui.pack)
endif()

file(GLOB APP_SRC CONFIGURE_DEPENDS src/app/*.cpp)
add_executable(Deixion WIN32 ${APP_SRC} ${DX_GEN}/app.rc ${DX_GEN}/ui_pack.rc)
target_include_directories(Deixion PRIVATE third_party/webview2/include)
target_link_libraries(Deixion PRIVATE dxcore)
target_link_options(Deixion PRIVATE -mwindows -municode)
add_custom_command(TARGET Deixion POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
  ${DX_ROOT}/third_party/webview2/bin/WebView2Loader.dll $<TARGET_FILE_DIR:Deixion>/WebView2Loader.dll)

# skill 随程序分发：放在 exe 旁的 skills/deixion，接入页与安装器据此写入 ~/.claude/skills。
file(GLOB_RECURSE SKILL_FILES CONFIGURE_DEPENDS ${DX_ROOT}/skills/deixion/*)
add_custom_command(TARGET Deixion POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
  ${DX_ROOT}/skills/deixion $<TARGET_FILE_DIR:Deixion>/skills/deixion)

file(GLOB CLI_SRC CONFIGURE_DEPENDS src/cli/*.cpp)
add_executable(deixion-cli ${CLI_SRC} ${DX_GEN}/cli.rc)
target_link_libraries(deixion-cli PRIVATE dxcore)

file(GLOB SETUP_SRC CONFIGURE_DEPENDS src/setup/*.cpp)
if(NODE_EXE)
  set(PAYLOAD_SPECS
    Deixion.exe=$<TARGET_FILE:Deixion>
    deixion-cli.exe=$<TARGET_FILE:deixion-cli>
    WebView2Loader.dll=${DX_ROOT}/third_party/webview2/bin/WebView2Loader.dll
    LICENSE=${DX_ROOT}/LICENSE)
  foreach(f ${SKILL_FILES})
    file(RELATIVE_PATH rel ${DX_ROOT}/skills/deixion ${f})
    list(APPEND PAYLOAD_SPECS skills/deixion/${rel}=${f})
  endforeach()
  add_custom_command(OUTPUT ${DX_GEN}/payload.bin ${DX_GEN}/payload.rc
    COMMAND ${NODE_EXE} ${DX_ROOT}/tools/pack-payload.mjs ${DX_GEN}/payload.bin ${DX_GEN}/payload.rc ${PAYLOAD_SPECS}
    DEPENDS Deixion deixion-cli ${SKILL_FILES} ${DX_ROOT}/LICENSE ${DX_ROOT}/tools/pack-payload.mjs VERBATIM)
  set_source_files_properties(${DX_GEN}/payload.rc PROPERTIES OBJECT_DEPENDS ${DX_GEN}/payload.bin)
  add_executable(deixion-setup WIN32 ${SETUP_SRC} ${DX_GEN}/setup.rc ${DX_GEN}/payload.rc)
  set_target_properties(deixion-setup PROPERTIES OUTPUT_NAME Deixion-Setup-x64)
  target_link_libraries(deixion-setup PRIVATE ole32 oleaut32 uuid shell32 user32 gdi32 advapi32 shlwapi comctl32 bcrypt dwmapi uxtheme wintrust crypt32 winhttp)
  target_link_options(deixion-setup PRIVATE -mwindows -municode)
endif()

add_executable(dx-testapp WIN32 EXCLUDE_FROM_ALL tools/testapp/main.cpp)
target_link_libraries(dx-testapp PRIVATE user32 gdi32)
target_link_options(dx-testapp PRIVATE -mwindows -municode)
