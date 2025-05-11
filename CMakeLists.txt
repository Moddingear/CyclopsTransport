cmake_minimum_required(VERSION 3.10)

# Collect all .cpp source files in the 'source' directory
file(GLOB_RECURSE CYCLOPS_SOURCES CONFIGURE_DEPENDS
	 "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp")

add_subdirectory("modules/GameNetworkingSockets")
# Define the library target
add_library(CyclopsTransport STATIC ${CYCLOPS_SOURCES})

# Specify include directories for the library
target_include_directories(CyclopsTransport PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(CyclopsTransport
	GameNetworkingSockets)