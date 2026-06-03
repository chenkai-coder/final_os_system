cmake_minimum_required(VERSION 3.10)

project(myfs_stage1 LANGUAGES CXX)

# C++ 标准，后续使用 STL 建议 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 头文件目录
include_directories(include)

# 底层核心源码
set(MYFS_CORE_SRC
        src/core/disk.cpp
        src/core/layout.cpp
        src/core/superblock.cpp
        src/core/mount.cpp
        src/util/error.cpp
        src/core/inode.cpp
        include/block_alloc.h
        src/core/block_alloc.cpp
        include/block_map.h
        src/core/block_map.cpp
        include/raw_file.h
        src/core/raw_file.cpp
        include/cache.h
        include/sync.h
        src/core/cache.cpp
        src/core/sync.cpp
        include/fsck.h
        src/core/fsck.cpp
        include/journal.h
        src/core/journal.cpp
        include/debug.h
        src/core/debug.cpp
        include/physical_api.h
        src/core/physical_api.cpp
)

add_library(myfs_core ${MYFS_CORE_SRC})

# 测试程序
add_executable(physical_test1
        src/tests/physical_test1.cpp
)

target_link_libraries(physical_test1 myfs_core)

add_executable(physical_stage2_full_test
        src/tests/physical_stage2_full_test.cpp
)

target_link_libraries(physical_stage2_full_test myfs_core)

add_executable(block_alloc_test1
        src/tests/block_alloc_test1.cpp
)
target_link_libraries(block_alloc_test1 myfs_core)

add_executable(block_map_test1
        src/tests/block_map_test1.cpp
)
target_link_libraries(block_map_test1 myfs_core)

add_executable(raw_file_test1
        src/tests/raw_file_test1.cpp
)
target_link_libraries(raw_file_test1 myfs_core)

add_executable(cache_test1
        src/tests/cache_test1.cpp
)
target_link_libraries(cache_test1 myfs_core)

add_executable(fsck_test1
        src/tests/fsck_test1.cpp
)
target_link_libraries(fsck_test1 myfs_core)

add_executable(journal_test1
        src/tests/journal_test1.cpp
)
target_link_libraries(journal_test1 myfs_core)

add_executable(debug_test1
        src/tests/debug_test1.cpp
)
target_link_libraries(debug_test1 myfs_core)

add_executable(physical_api_test1
        src/tests/physical_api_test1.cpp
)
target_link_libraries(physical_api_test1 myfs_core)

add_executable(vfs_upper
        src/vfs_upper.cpp
)
target_link_libraries(vfs_upper myfs_core)