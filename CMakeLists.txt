cmake_minimum_required(VERSION 3.20)
project(MultiScreenSystem LANGUAGES CXX)

# ------------------------------
# Установка стандарта C++
# ------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------
# Поиск исходников
# ------------------------------
file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS include/*.h include/*.hpp)
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES} ${PROJECT_HEADERS} "include/Metrics.h" )
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)

# ------------------------------
# Подключение FFmpeg как системных include для подавления предупреждений
# ------------------------------
if(WIN32)
    set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/libs/ffmpeg")
    if(NOT EXISTS "${FFMPEG_ROOT}/include" OR NOT EXISTS "${FFMPEG_ROOT}/lib")
        message(FATAL_ERROR
            "FFmpeg for Windows not found in ${FFMPEG_ROOT}.\n"
            "Expected subfolders: include/ and lib/ with .lib files.")
    endif()
    # Используем SYSTEM для include, чтобы подавить warnings из FFmpeg
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${FFMPEG_ROOT}/include")
    target_link_directories(${PROJECT_NAME} PRIVATE "${FFMPEG_ROOT}/lib")
    target_link_libraries(${PROJECT_NAME} PRIVATE
        avformat.lib
        avcodec.lib
        avutil.lib
        swscale.lib
        swresample.lib
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
    pkg_check_modules(AVCODEC  REQUIRED IMPORTED_TARGET libavcodec)
    pkg_check_modules(AVUTIL   REQUIRED IMPORTED_TARGET libavutil)
    pkg_check_modules(SWSCALE  REQUIRED IMPORTED_TARGET libswscale)
    pkg_check_modules(SWRESAMP REQUIRED IMPORTED_TARGET libswresample)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        PkgConfig::libavformat
        PkgConfig::libavcodec
        PkgConfig::libavutil
        PkgConfig::libswscale
        PkgConfig::libswresample
    )
endif()

# ------------------------------
# Header-only библиотеки
# ------------------------------
# cpp-httplib (httplib.h)
set(_CPPHTTPLIB_FOUND OFF)
foreach(_dir IN LISTS
    CMAKE_SOURCE_DIR
)
endforeach()
set(_CPPHTTPLIB_CANDIDATES
    "${CMAKE_SOURCE_DIR}/external/cpp-httplib"
    "${CMAKE_SOURCE_DIR}/external/cpp_httplib"
    "${CMAKE_SOURCE_DIR}/third_party/cpp-httplib"
)
foreach(_d IN LISTS _CPPHTTPLIB_CANDIDATES)
    if(EXISTS "${_d}/httplib.h")
        target_include_directories(${PROJECT_NAME} PRIVATE "${_d}")
        set(_CPPHTTPLIB_FOUND ON)
        message(STATUS "cpp-httplib: use ${_d}")
        break()
    endif()
endforeach()
if(NOT _CPPHTTPLIB_FOUND)
    message(FATAL_ERROR
        "Cannot find cpp-httplib header.\n"
        "Expected: external/cpp-httplib/httplib.h ( . CMakeLists)")
endif()

# nlohmann/json (nlohmann/json.hpp)
set(_NLOHMANN_FOUND OFF)
if(EXISTS "${CMAKE_SOURCE_DIR}/external/nlohmann_json/include/nlohmann/json.hpp")
    target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/nlohmann_json/include")
    set(_NLOHMANN_FOUND ON)
    message(STATUS "nlohmann_json: use external/nlohmann_json/include")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/external/nlohmann_json/nlohmann/json.hpp")
    target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/external/nlohmann_json")
    set(_NLOHMANN_FOUND ON)
    message(STATUS "nlohmann_json: use external/nlohmann_json")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/include/nlohmann/json.hpp")
    target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/include")
    set(_NLOHMANN_FOUND ON)
    message(STATUS "nlohmann_json: use third_party/nlohmann_json/include")
endif()
if(NOT _NLOHMANN_FOUND)
    message(FATAL_ERROR
        "Cannot find nlohmann/json header.\n"
        "Expected: external/nlohmann_json/include/nlohmann/json.hpp ( . CMakeLists)")
endif()

# ------------------------------
# Настройки отладки в Visual Studio
# ------------------------------
set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    VS_DEBUGGER_ENVIRONMENT "PATH=$<TARGET_FILE_DIR:${PROJECT_NAME}>;%PATH%"
)

# ------------------------------
# Флаги компилятора
# ------------------------------
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /external:W0)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ------------------------------
# Копирование папки www рекурсивно в каталог сборки
# ------------------------------
add_custom_target(copy_www ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/www
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/www
    COMMENT "Copying www directory into build output directory"
)

add_dependencies(${PROJECT_NAME} copy_www)

# ------------------------------
# Сценарий копирования config и FFmpeg DLLs (оставляем твой скрипт для config и DLL)
# ------------------------------
set(_STAGE_SCRIPT "${CMAKE_BINARY_DIR}/_stage_runtime.cmake")
file(WRITE "${_STAGE_SCRIPT}" "
if(NOT DEFINED dst_dir)
  message(FATAL_ERROR \"stage_runtime: dst_dir not set\")
endif()
if(NOT DEFINED src_dir)
  set(src_dir \"${CMAKE_SOURCE_DIR}\")
endif()
# 1) config
file(MAKE_DIRECTORY \"\${dst_dir}/config\")
if(EXISTS \"\${src_dir}/config\")
  file(COPY \"\${src_dir}/config/\" DESTINATION \"\${dst_dir}/config\")
endif()
# 2) ensure default configs if missing
if(NOT EXISTS \"\${dst_dir}/config/config.json\")
  file(WRITE \"\${dst_dir}/config/config.json\" [===[
{
  \\\"web\\\": { \\\"enable\\\": true, \\\"port\\\": 8080 },
  \\\"streams\\\": { \\\"enable\\\": true }
}
]===])
endif()
if(NOT EXISTS \"\${dst_dir}/config/streams.json\")
  file(WRITE \"\${dst_dir}/config/streams.json\" [===[
[
  { \\\"name\\\": \\\"Demo Channel\\\", \\\"service_name\\\": \\\"Demo Service\\\", \\\"url\\\": \\\"http://example.invalid/demo.ts\\\" }
]
]===])
endif()
if(NOT EXISTS \"\${dst_dir}/config/settings.json\")
  file(WRITE \"\${dst_dir}/config/settings.json\" [===[
{
  \\\"thresholds\\\": {
    \\\"fps\\\":    { \\\"warn_ratio\\\": 0.75, \\\"crit_ratio\\\": 0.50 },
    \\\"bitrate\\\":{ \\\"warn_kbps\\\": 1500, \\\"crit_kbps\\\": 500 },
    \\\"stall\\\":  { \\\"warn_ms\\\": 1000, \\\"crit_ms\\\": 3000 }
  },
  \\\"alerts\\\": { \\\"webhook\\\": { \\\"enabled\\\": false, \\\"url\\\": \\\"\\\", \\\"timeout_ms\\\": 2000 } }
}
]===])
endif()
# 3) FFmpeg DLLs: копирование .dll файлов   
set(_search_roots
  \"\${src_dir}/libs/ffmpeg/bin\"
  \"\${src_dir}/libs/ffmpeg/bin64\"
  \"\${src_dir}/libs/ffmpeg/shared/bin\"
  \"\${src_dir}/libs/ffmpeg\"
  \"\${src_dir}/third_party/ffmpeg/bin\"
  \"\${src_dir}/third_party/ffmpeg\"
)
set(_copied 0)
foreach(_r IN LISTS _search_roots)
  if(EXISTS \"\${_r}\")
    file(GLOB_RECURSE _dlls \"\${_r}/*.dll\")
    foreach(_f IN LISTS _dlls)
      file(COPY \"\${_f}\" DESTINATION \"\${dst_dir}\")
      math(EXPR _copied \"\${_copied} + 1\")
    endforeach()
  endif()
endforeach()
message(STATUS \"stage_runtime: copied \${_copied} FFmpeg DLL(s) to \${dst_dir}\")
# 4) Проверка наличия www/index.html и config/*.json
if(NOT EXISTS \"\${dst_dir}/www/index.html\")
  message(WARNING \"stage_runtime: www/index.html not found near exe; UI fallback may be used.\")
endif()
foreach(_must IN ITEMS config.json streams.json settings.json)
  if(NOT EXISTS \"\${dst_dir}/config/\${_must}\")
    message(FATAL_ERROR \"stage_runtime: missing \${_must} in \${dst_dir}/config\")
  endif()
endforeach()
")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -Ddst_dir=$<TARGET_FILE_DIR:${PROJECT_NAME}>
                              -Dsrc_dir=${CMAKE_SOURCE_DIR}
                              -P "${_STAGE_SCRIPT}"
    COMMENT "Staging config/ (with defaults) and FFmpeg DLLs to runtime dir"
)
