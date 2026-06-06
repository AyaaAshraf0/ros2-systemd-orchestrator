cmake_minimum_required(VERSION 3.8)
project(corebot_interfaces)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -std=c++17)
endif()

# Find dependencies required for generating interfaces
find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(action_msgs REQUIRED)

# Register your custom service file
rosidl_generate_interfaces(${PROJECT_NAME}
  "srv/SetMode.srv"
  "action/SetMode.action"
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
