#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <algorithm>
#include <thread>
#include <atomic>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "corebot_interfaces/srv/set_mode.hpp"
#include "corebot_interfaces/action/set_mode.hpp"

class RobotSupervisor : public rclcpp::Node {
public:
    RobotSupervisor() : Node("robot_supervisor") {

        // Endpoint 1: Power/Simulation
        toggle_srv_ = this->create_service<corebot_interfaces::srv::SetMode>(
            "/corebot/robot_turn_on_off",
            std::bind(&RobotSupervisor::handle_toggle_robot, this, std::placeholders::_1, std::placeholders::_2)
        );

        // Endpoint 2: Autonomy Modes (action server)
        using SetModeAction = corebot_interfaces::action::SetMode;

        action_server_ = rclcpp_action::create_server<SetModeAction>(
            this,
            "/corebot/set_mode",
            // goal callback
            [this](const rclcpp_action::GoalUUID & uuid, std::shared_ptr<const SetModeAction::Goal> goal) {
                (void)uuid;
                std::string mode = goal->mode;
                std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
                if (transitioning_.load()) {
                    RCLCPP_WARN(this->get_logger(), "Transition request rejected: another transition is in progress.");
                    return rclcpp_action::GoalResponse::REJECT;
                }
                transitioning_.store(true);
                return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
            },
            // cancel callback
            [this](std::shared_ptr<rclcpp_action::ServerGoalHandle<SetModeAction>> goal_handle) {
                (void)goal_handle;
                RCLCPP_WARN(this->get_logger(), "Cancel requested but transitions are not cancellable — rejecting.");
                return rclcpp_action::CancelResponse::REJECT;
            },
            // accepted callback
            [this](std::shared_ptr<rclcpp_action::ServerGoalHandle<SetModeAction>> goal_handle) {
                std::thread{std::bind(&RobotSupervisor::execute, this, std::placeholders::_1), goal_handle}.detach();
            }
        );

        RCLCPP_INFO(this->get_logger(), "ROS 2 C++ Supervisor Active: Unified Interface Mode.");
    }

private:
    // FIX 1: Block until systemctl actually finishes, then check its exit code.
    // The old WNOHANG returned immediately and always reported success=true,
    // even when the service failed to start or stop.
    bool call_systemctl(const std::string& action, const std::string& service_name) {
        RCLCPP_INFO(this->get_logger(), "Systemctl: sudo systemctl %s %s",
                    action.c_str(), service_name.c_str());

        pid_t pid = fork();
        if (pid < 0) {
            RCLCPP_ERROR(this->get_logger(), "fork() failed for: systemctl %s %s",
                         action.c_str(), service_name.c_str());
            return false;
        }

        if (pid == 0) {
            // Child: keep stderr so systemctl errors surface in journalctl
            freopen("/dev/null", "w", stdout);
            execlp("sudo", "sudo", "systemctl", action.c_str(), service_name.c_str(), nullptr);
            _exit(1); // execlp failed
        }

        // Parent: BLOCK until the child exits (no WNOHANG), then inspect the code.
        int status = 0;
        waitpid(pid, &status, 0);

        bool ok = WIFEXITED(status) && (WEXITSTATUS(status) == 0);
        if (!ok) {
            RCLCPP_ERROR(this->get_logger(), "systemctl %s %s failed (exit %d)",
                         action.c_str(), service_name.c_str(),
                         WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        }
        return ok;
    }

    void handle_toggle_robot(
        const std::shared_ptr<corebot_interfaces::srv::SetMode::Request> request,
        std::shared_ptr<corebot_interfaces::srv::SetMode::Response> response)
    {
        std::string action = request->mode;
        std::transform(action.begin(), action.end(), action.begin(), ::tolower);

        if (action == "on") {
            response->success = call_systemctl("start", "corebot_hardware.service");
            response->message = response->success
                ? "Hardware started successfully."
                : "Hardware START failed — check journalctl for details.";
        }
        else if (action == "off") {
            response->success = call_systemctl("stop", "corebot_hardware.service");
            response->message = response->success
                ? "Hardware stopped successfully."
                : "Hardware STOP failed — check journalctl for details.";
        }
        else {
            response->success = false;
            response->message = "Invalid input. Use 'on' or 'off'.";
        }
    }
    void execute(std::shared_ptr<rclcpp_action::ServerGoalHandle<corebot_interfaces::action::SetMode>> goal_handle)
    {
        using SetModeAction = corebot_interfaces::action::SetMode;
        auto goal = goal_handle->get_goal();
        std::string mode = goal->mode;
        std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);

        auto feedback = std::make_shared<SetModeAction::Feedback>();
        auto result = std::make_shared<SetModeAction::Result>();

        if (mode == "slam") {
            feedback->status = "Stopping Nav before starting SLAM...";
            goal_handle->publish_feedback(feedback);
            bool ok1 = call_systemctl("stop", "corebot_nav.service");

            feedback->status = "Starting SLAM...";
            goal_handle->publish_feedback(feedback);
            bool ok2 = call_systemctl("start", "corebot_slam.service");

            result->success = ok1 && ok2;
            result->message = result->success ? "SLAM mode active." : "SLAM transition failed — check journalctl.";
            if (result->success) goal_handle->succeed(result); else goal_handle->abort(result);
        }
        else if (mode == "nav") {
            feedback->status = "Stopping SLAM (map save in progress, up to 20s)...";
            goal_handle->publish_feedback(feedback);
            bool ok1 = call_systemctl("stop", "corebot_slam.service");

            feedback->status = "SLAM stopped. Starting Nav...";
            goal_handle->publish_feedback(feedback);
            bool ok2 = call_systemctl("start", "corebot_nav.service");

            result->success = ok1 && ok2;
            result->message = result->success ? "Navigation mode active." : "Nav transition failed — check journalctl.";
            if (result->success) goal_handle->succeed(result); else goal_handle->abort(result);
        }
        else if (mode == "idle") {
            feedback->status = "Stopping SLAM and Nav...";
            goal_handle->publish_feedback(feedback);
            bool s1 = call_systemctl("stop", "corebot_slam.service");
            bool s2 = call_systemctl("stop", "corebot_nav.service");
            result->success = s1 && s2;
            result->message = result->success ? "System IDLE." : "IDLE transition had errors — check journalctl.";
            if (result->success) goal_handle->succeed(result); else goal_handle->abort(result);
        }
        else {
            result->success = false;
            result->message = "Invalid mode. Use slam, nav, or idle.";
            goal_handle->abort(result);
        }

        transitioning_.store(false);
    }

    rclcpp::Service<corebot_interfaces::srv::SetMode>::SharedPtr toggle_srv_;
    rclcpp_action::Server<corebot_interfaces::action::SetMode>::SharedPtr action_server_;
    std::atomic<bool> transitioning_{false};
};

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<RobotSupervisor>();
    rclcpp::executors::SingleThreadedExecutor exec;
    exec.add_node(node);
    exec.spin();
    exec.remove_node(node);
    rclcpp::shutdown();
    return 0;
}