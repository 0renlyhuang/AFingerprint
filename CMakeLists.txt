cmake_minimum_required(VERSION 3.10)
project(AFingerprint C CXX OBJCXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加编译优化选项
if(MSVC)
    add_compile_options(/O2)
else()
    add_compile_options(-O3 -march=native)
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

# 扫描所有源文件
file(GLOB_RECURSE SOURCE_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)
list(FILTER SOURCE_FILES EXCLUDE REGEX ".*src/fft/.*")

# 添加FFT接口文件
list(APPEND SOURCE_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fft/fft_interface.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fft/fft_factory.cpp"
)
# 根据平台添加特定的FFT实现
if(IOS OR APPLE)
    list(APPEND SOURCE_FILES 
        "${CMAKE_CURRENT_SOURCE_DIR}/src/fft/fft_accelerate.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/fft/fft_accelerate.mm"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/fft/fft_accelerate.mm"
        PROPERTIES 
            COMPILE_FLAGS "-fobjc-arc"
            LANGUAGE OBJCXX
    )
elseif(ANDROID)
    list(APPEND SOURCE_FILES 
        "${CMAKE_CURRENT_SOURCE_DIR}/src/fft/fft_ne10.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/fft/fft_ne10.cpp"
    )
else()
    if(WIN32)
        list(APPEND SOURCE_FILES 
            "${CMAKE_CURRENT_SOURCE_DIR}/src/fft/fft_mkl.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/fft/fft_mkl.cpp"
        )
    endif()
endif()

# 添加可执行文件
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 设置源文件的目录结构
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_FILES})

# 包含头文件目录
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(IOS OR APPLE)
    find_library(ACCELERATE_FRAMEWORK Accelerate)
    target_link_libraries(${PROJECT_NAME} PUBLIC ${ACCELERATE_FRAMEWORK})
elseif(ANDROID)
    # 添加 Ne10
    add_subdirectory(third_party/Ne10)
    target_link_libraries(${PROJECT_NAME} PUBLIC Ne10)
else()
    # Windows 平台使用 MKL
    if(WIN32)
        find_package(MKL CONFIG REQUIRED)
        target_link_libraries(${PROJECT_NAME} PUBLIC MKL::MKL)
    else()
        # 其他平台使用 KissFFT
        add_subdirectory(third_party/kissfft)
        target_link_libraries(${PROJECT_NAME} PUBLIC kissfft)
    endif()
endif() 