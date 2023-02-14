cmake_minimum_required(VERSION 3.10)

project(WebServer)

# 指定为 C++11 版本
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)

# 设置用到的第三方库路径
set(MYINCLUDE "/usr/include")
set(MYLIB "/usr/lib/x86_64-linux-gnu/")

# 查找当前目录下的所有源文件
# 并将名称保存到 DIR_SRCS 变量
aux_source_directory(. DIRSRCS)

# 添加子目录构建系统
add_subdirectory(http)
add_subdirectory(log)
add_subdirectory(database)
add_subdirectory(timer)

# 添加头文件搜索路径
include_directories(./ ./threadpool ./http ./log ./database ./timer ${MYINCLUDE})

# 导入可执行目标文件
add_executable(WebServer ${DIRSRCS})

# 将目标文件与库文件进行链接
target_link_libraries(WebServer http log database timer ${MYLIB}/libmysqlclient.so pthread)