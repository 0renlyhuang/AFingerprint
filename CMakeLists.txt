cmake_minimum_required(VERSION 3.10)
project(AFingerprint C CXX OBJCXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加编译优化选项（根据构建类型）
if(MSVC)
    # MSVC 编译器
    string(REPLACE "/O2" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Ob1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    # 为Debug配置添加/Od（禁用优化）
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /Zi /RTC1")
    # 为Release配置保留优化
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
else()
    # GCC/Clang 编译器
    string(REPLACE "-O3" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    # 为Debug配置添加-O0（禁用优化）和-g（生成调试信息）
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
    # 为Release配置设置高优化
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native")
endif()

# 启用多线程编译
if(MSVC)
    # MSVC 使用 /MP 选项
    add_compile_options(/MP)
else()
    # 其他编译器使用 -j 参数
    if(NOT CMAKE_BUILD_PARALLEL_LEVEL)
        # 如果没有设置，则使用系统CPU核心数
        include(ProcessorCount)
        ProcessorCount(N)
        if(NOT N EQUAL 0)
            set(CMAKE_BUILD_PARALLEL_LEVEL ${N})
        endif()
    endif()
endif()

# 允许IDE组织文件结构与实际目录结构相同
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# 首先添加afp库子目录
add_subdirectory(src/libafp)

# 仅包含主程序源文件
file(GLOB_RECURSE MAIN_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
)

# 添加可执行文件
add_executable(${PROJECT_NAME} ${MAIN_SOURCE_FILES})

# 设置源文件的目录结构
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${MAIN_SOURCE_FILES})

# 包含头文件目录
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/libafp/include
)

# 链接AFP静态库
target_link_libraries(${PROJECT_NAME} PRIVATE afp)
