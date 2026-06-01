import os
import signal
import subprocess

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def launch_setup(context, *args, **kwargs):
    use_sim_time = LaunchConfiguration("use_sim_time")
    params_file = LaunchConfiguration("params_file")

    output_dir = LaunchConfiguration("output_dir").perform(context)
    map_name = LaunchConfiguration("map_name").perform(context)
    map_stem = os.path.join(output_dir, map_name)

    os.makedirs(output_dir, exist_ok=True)

    original_sigint = signal.getsignal(signal.SIGINT)

    def sigint_handler(sig, frame):
        print(f"\n[slam_launch] Saving map to {map_stem} ...")

        result = subprocess.run(
            [
                "ros2",
                "run",
                "nav2_map_server",
                "map_saver_cli",
                "-f",
                map_stem,
                "--ros-args",
                "-r",
                "/clock:=/fast_clock",
                "-p",
                "save_map_timeout:=20.0",
            ]
        )

        if result.returncode == 0:
            print("[slam_launch] Map saved successfully.")
        else:
            print("[slam_launch] Map save FAILED.")

        signal.signal(signal.SIGINT, original_sigint)
        signal.raise_signal(signal.SIGINT)

    signal.signal(signal.SIGINT, sigint_handler)

    slam_node = Node(
        package="slam_toolbox",
        executable="async_slam_toolbox_node",
        name="slam_toolbox",
        output="screen",
        parameters=[params_file, {"use_sim_time": use_sim_time}],
        remappings=[("/clock", "/fast_clock")],
        prefix="setsid",
    )

    return [slam_node]


def generate_launch_description():
    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "use_sim_time",
                default_value="true",
                description="Use simulation clock",
            ),
            DeclareLaunchArgument(
                "map_name",
                default_value="slam_map",
                description="Stem name for the saved .pgm / .yaml files",
            ),
            DeclareLaunchArgument(
                "output_dir",
                default_value=PathJoinSubstitution(
                    [FindPackageShare("navigation"), "maps"]
                ),
                description="Directory where the map will be saved on shutdown",
            ),
            DeclareLaunchArgument(
                "params_file",
                default_value=PathJoinSubstitution(
                    [FindPackageShare("navigation"), "config", "mapper_params.yaml"]
                ),
            ),
            OpaqueFunction(function=launch_setup),
        ]
    )
