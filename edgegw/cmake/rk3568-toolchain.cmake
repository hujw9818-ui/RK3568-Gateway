# ======================================================================
# RK3568 Buildroot 交叉编译工具链
# 用法: cmake -S . -B build/arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/rk3568-toolchain.cmake
# ======================================================================
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# buildroot 工具链路径 (由 SDK 编译生成)
set(BUILDROOT_DIR /home/ubuntu/rk356x_linux/buildroot/output)
set(TOOLCHAIN_DIR ${BUILDROOT_DIR}/host/bin)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu-g++)

# sysroot: 头文件 + 库都在这
set(CMAKE_SYSROOT ${BUILDROOT_DIR}/host/aarch64-buildroot-linux-gnu/sysroot)

# 让 find_package / find_library 在 sysroot 里找
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 交叉编译时需要 host 平台的工具 (如 pkg-config)
set(PKG_CONFIG_EXECUTABLE /usr/bin/pkg-config)
