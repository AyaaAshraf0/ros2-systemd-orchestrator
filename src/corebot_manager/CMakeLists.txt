cmake_minimum_required(VERSION 3.8)
project(corebot_manager)

# Use C++17 for modern, clean syntax
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -std=c++17)
endif()

# Find all required dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(corebot_interfaces REQUIRED)

# Define the node executable
add_executable(supervisor_node src/supervisor_node.cpp)

# Link the libraries (this registers your services)
ament_target_dependencies(supervisor_node 
  rclcpp 
  corebot_interfaces
)

# Install the binary to the ROS 2 lib directory
install(TARGETS
  supervisor_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()