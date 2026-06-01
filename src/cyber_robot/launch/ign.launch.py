import os
from launch import LaunchDescription
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch.actions import ExecuteProcess, DeclareLaunchArgument, SetEnvironmentVariable
from launch.substitutions import Command, LaunchConfiguration, PythonExpression
from launch.conditions import IfCondition, UnlessCondition
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    cyber_Robot_pkg = FindPackageShare(package="cyber_robot").find("cyber_robot")
    urdf_model_path = os.path.join(cyber_Robot_pkg,'urdf','cyber_robot.urdf.xacro')
    world_file = os.path.join(cyber_Robot_pkg,'worlds','wider_classroom.sdf')

    robot_desc = Command(["xacro ",urdf_model_path," use_ignition:=","true"])

    set_resource_path = SetEnvironmentVariable(
        name='IGN_GAZEBO_RESOURCE_PATH',
        value=os.environ.get('IGN_GAZEBO_RESOURCE_PATH', '') + ':src'
    )
    
    # ─── Launch Arguments ───────────────────────────────────────────────────────
    headless_arg = DeclareLaunchArgument(
        'headless',
        default_value='false',
        description='Run Ignition in headless server mode (no GUI, no display needed)'
    )
    headless = LaunchConfiguration('headless')

    # ─── Nodes ──────────────────────────────────────────────────────────────────
    robot_state_publisher = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        parameters=[
            {
                "robot_description": robot_desc,
                "use_sim_time": True,
            }
        ],
    )

    joint_state_publisher = Node(
        package="joint_state_publisher",
        executable="joint_state_publisher",
        name="joint_state_publisher",
        parameters=[{"use_sim_time": True}],
        remappings=[('/world/classroom/clock', '/fast_clock'),]
    )

    # Normal mode — with GUI
    ign_gazebo_normal = ExecuteProcess(
        cmd=["ign", "gazebo", "-r", world_file],
        output="screen",
        condition=UnlessCondition(headless)
    )

    # Headless mode — server only, no display required
    # -s  → server only (no GUI client)
    # --headless-rendering → no display/GPU needed
    ign_gazebo_headless = ExecuteProcess(
        cmd=["ign", "gazebo", "-r", "-s", "--headless-rendering", world_file],
        output="screen",
        condition=IfCondition(headless)
    )

    rviz = ExecuteProcess(
        cmd=["rviz2", "-d", "src/cyber_robot/config/cyber_truck.rviz"],
        output="screen",
    )

    spawn_entity = Node(
        package="ros_ign_gazebo",
        executable="create",
        arguments=["-name", "cyber_robot", "-topic", "robot_description",
                   '-x','0',
                   '-y', '-2.05',
                    '-z', '0.5',
                    '-Y', '1.5708'
                    ],
        output="screen",
        parameters=[{"use_sim_time": True}],
        remappings=[('/world/classroom/clock', '/fast_clock'),]
    )

    bridge = Node(
        package="ros_ign_bridge",
        executable="parameter_bridge",
        arguments=[
                    "/joint_states@sensor_msgs/msg/JointState@ignition.msgs.Model",
                    "/odom@nav_msgs/msg/Odometry@ignition.msgs.Odometry",
                    "/lidar@sensor_msgs/msg/LaserScan[ignition.msgs.LaserScan",
                    "/imu@sensor_msgs/msg/Imu[ignition.msgs.IMU",
                    "/cmd_vel@geometry_msgs/msg/Twist@ignition.msgs.Twist",
                    "/model/cyber_robot/tf@tf2_msgs/msg/TFMessage@ignition.msgs.Pose_V",
        ],
        parameters=[{"use_sim_time": True}],
        output="screen",
        remappings=[
            ('/model/cyber_robot/tf', '/tf'), ('/lidar','/scan'),
                  ('/world/classroom/clock', '/fast_clock')]
    )

    lidar_tf_static = Node(
        package="tf2_ros",
        executable="static_transform_publisher",
        name="static_lidar_tf",
        parameters=[{"use_sim_time": True}],
        arguments=["0","0","0","0","0","0","Lidar","cyber_robot/base_link/gpu_lidar"],
        remappings=[('/world/classroom/clock', '/fast_clock'),]
    )

    clock_bridge = Node(
        package="ros_ign_bridge",
        executable="parameter_bridge",
        name="clock_bridge",
        arguments=[
                    "/world/classroom/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock",
        ],
        output="screen",
        parameters=[{"use_sim_time": True}],
        remappings=[('/world/classroom/clock', '/fast_clock'),]
    )

    ld = LaunchDescription()
    ld.add_action(set_resource_path)
    ld.add_action(headless_arg)           # register the argument first
    ld.add_action(ign_gazebo_normal)      # runs when headless:=false (default)
    ld.add_action(ign_gazebo_headless)    # runs when headless:=true
    ld.add_action(rviz)
    ld.add_action(robot_state_publisher)
    ld.add_action(spawn_entity)
    ld.add_action(bridge)
    ld.add_action(clock_bridge)
    ld.add_action(lidar_tf_static)

    return ld
