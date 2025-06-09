cmake_minimum_required(VERSION 3.10)
project(afp C CXX OBJCXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DENABLED_DIAGNOSE)

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

# 扫描所有源文件
file(GLOB_RECURSE SOURCE_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/signature/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/signature/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/debugger/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/debugger/*.h"
)
list(FILTER SOURCE_FILES EXCLUDE REGEX ".*fft/.*")

# 添加FFT接口文件
list(APPEND SOURCE_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/fft/fft_interface.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/fft/fft_factory.cpp"
)
# 根据平台添加特定的FFT实现
if(IOS OR APPLE)
    list(APPEND SOURCE_FILES 
        "${CMAKE_CURRENT_SOURCE_DIR}/fft/fft_accelerate.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/fft/fft_accelerate.mm"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/fft/fft_accelerate.mm"
        PROPERTIES 
            COMPILE_FLAGS "-fobjc-arc"
            LANGUAGE OBJCXX
    )
elseif(ANDROID)
    list(APPEND SOURCE_FILES 
        "${CMAKE_CURRENT_SOURCE_DIR}/fft/fft_ne10.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/fft/fft_ne10.cpp"
    )
else()
    if(WIN32)
        list(APPEND SOURCE_FILES 
            "${CMAKE_CURRENT_SOURCE_DIR}/fft/fft_mkl.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/fft/fft_mkl.cpp"
        )
    endif()
endif()

# 添加静态库
add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})

# 设置源文件的目录结构
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_FILES})

# 包含头文件目录
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(IOS OR APPLE)
    find_library(ACCELERATE_FRAMEWORK Accelerate)
    target_link_libraries(${PROJECT_NAME} PUBLIC ${ACCELERATE_FRAMEWORK})
    
    # 为Apple平台添加post-build命令，创建xcframework
    if(APPLE)
        # 设置输出目录
        set(XCFRAMEWORK_OUTPUT_DIR "${CMAKE_BINARY_DIR}/xcframework")
        set(XCFRAMEWORK_PATH "${XCFRAMEWORK_OUTPUT_DIR}/lib${PROJECT_NAME}.xcframework")
        
        # 确保输出目录存在
        file(MAKE_DIRECTORY ${XCFRAMEWORK_OUTPUT_DIR})
        
        # 获取构建后的静态库完整路径
        set(LIBRARY_PATH "$<TARGET_FILE:${PROJECT_NAME}>")
        set(HEADERS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/include")
        
        # 添加post-build命令创建xcframework
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Creating xcframework for ${PROJECT_NAME}..."
            # 首先删除现有的xcframework，如果存在的话
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${XCFRAMEWORK_PATH}
            # 然后创建新的xcframework
            COMMAND xcodebuild -create-xcframework
                    -library ${LIBRARY_PATH}
                    -headers ${HEADERS_PATH}
                    -output ${XCFRAMEWORK_PATH}
            COMMENT "Packaging lib${PROJECT_NAME}.a into xcframework"
        )
        
        # 消息提示xcframework的位置
        message(STATUS "XCFramework will be created at: ${XCFRAMEWORK_OUTPUT_DIR}/lib${PROJECT_NAME}.xcframework")
    endif()
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