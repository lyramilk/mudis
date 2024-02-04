cmake_minimum_required(VERSION 3.0)
cmake_policy(SET CMP0048 NEW)
#定义工程名及版本
project(mudis VERSION 3.0.0)
set(soversion 3.0)

# 包含公共代码*
if(NOT SUBMODULE)
	set(SUBMODULE ${PROJECT_SOURCE_DIR}/3rd_party)
endif()
include(${SUBMODULE}/libmilk/libmilk/libmilk.cmake)

# 设置环境变量*
libmilk_cmake_init_env()

# 搜索代码
unset(codes)
unset(headers)

aux_source_directory(src codes)
file(GLOB headers RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} include/mudis/*.h)

#搜索子模块
set(CMAKE_CXX_STANDARD 11)
set(milk-minimal ON)

if(NOT TARGET milk)
	if(shared)
		add_subdirectory(${SUBMODULE}/libmilk/libmilk ${SUBMODULE_BIN}/libmilk)
	else()
		add_subdirectory(${SUBMODULE}/libmilk/libmilk ${SUBMODULE_BIN}/libmilk EXCLUDE_FROM_ALL)
	endif()
endif()

#定义目标*
libmilk_cmake_def(${PROJECT_NAME} ${PROJECT_NAME}-shared mudis codes headers ${PROJECT_NAME}-alone)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} include/mudis)

if(${PROJECT_NAME}-shared)
	target_link_libraries(${PROJECT_NAME} PUBLIC milk)
else()
	target_link_libraries(${PROJECT_NAME} PUBLIC milk)
endif()

#定义主程序
add_executable(mudisd src/app/mudisd.cc)
install(TARGETS mudisd DESTINATION bin)
#target_link_libraries(mudisd PUBLIC -Wl,--whole-archive milk ${PROJECT_NAME} -Wl,--no-whole-archive ${LIBS_DEPENDS})
target_link_libraries(mudisd PUBLIC milk ${PROJECT_NAME} ${LIBS_DEPENDS})
