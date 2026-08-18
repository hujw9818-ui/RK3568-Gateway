# ======================================================================
# edgegw - RK3568 边缘网关主程序
# C++14 + Mongoose + libmosquitto + yaml-cpp + sqlite3
#
# 构建方式:
#   host 本机调试:   cmake -S . -B build/host && cmake --build build/host
#   arm64 交叉编译:  cmake -S . -B build/arm64 \
#                      -DCMAKE_TOOLCHAIN_FILE=cmake/rk3568-toolchain.cmake
#                    cmake --build build/arm64
# ======================================================================
cmake_minimum_required(VERSION 3.16)
project(edgegw LANGUAGES C CXX)

# ---------- 标准 ----------
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -g)

# ---------- 第三方: mongoose (单文件库, 直接编入) ----------
set(MONGOOSE_DIR ${CMAKE_SOURCE_DIR}/third_party/mongoose)

# ---------- 源文件 ----------
file(GLOB_RECURSE EDGEGW_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/*.cpp
)

# ---------- 可执行文件 ----------
add_executable(edgegw
  ${EDGEGW_SOURCES}
  ${MONGOOSE_DIR}/mongoose.c
)

# 头文件搜索路径
target_include_directories(edgegw PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${MONGOOSE_DIR}
  # rapidjson 是 header-only, 头文件已由 buildroot 安装到 sysroot
)

# ---------- 链接库 ----------
find_package(Threads REQUIRED)

target_link_libraries(edgegw PRIVATE
  Threads::Threads
  mosquitto        # buildroot: libmosquitto (MQTT 客户端)
  yaml-cpp         # buildroot: libyaml-cpp (配置解析)
  sqlite3          # buildroot: libsqlite3 (数据库)
  spdlog           # buildroot: libspdlog (日志)
  fmt              # buildroot: libfmt (spdlog 依赖)
)

# ---------- 交叉编译时自动指向 buildroot sysroot ----------
# toolchain 文件里已设置 CMAKE_SYSROOT, 这里不需要额外处理
# 本机 host 构建时若无以上库, 需先安装:
#   sudo apt install libmosquitto-dev libyaml-cpp-dev libsqlite3-dev

# ---------- 安装 ----------
install(TARGETS edgegw RUNTIME DESTINATION bin)
