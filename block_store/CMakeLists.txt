cmake_minimum_required (VERSION 2.8)
project(block_store)

set(CMAKE_C_FLAGS "-std=c99 -Wall -Werror")
set(CMAKE_BUILD_TYPE RelWithDebInfo)

include_directories(${bitmap_INCLUDE_DIRS})
add_library(${PROJECT_NAME} SHARED src/${PROJECT_NAME}.c)
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(${PROJECT_NAME} bitmap)

install(TARGETS ${PROJECT_NAME} DESTINATION lib)
install(FILES include/${PROJECT_NAME}.h DESTINATION include)

set(${PROJECT_NAME}_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/include
	CACHE INTERNAL "${PROJECT_NAME}: Include Directories" FORCE)



# generator stuff

set(CMAKE_CXX_FLAGS "-std=c++11 -Wall -Werror -O3")

add_executable(generate_drive src/generate_drive.cpp)



# tester gibberish

set(CMAKE_BUILD_TYPE Debug)
enable_testing()
add_executable(block_store_tester test/test.c)
target_link_libraries(block_store_tester bitmap)
add_test(tester block_store_tester)
