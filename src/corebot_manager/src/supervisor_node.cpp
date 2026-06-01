#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <algorithm>

#include "rclcpp/rclcpp.hpp"
#include "corebot_interfaces/srv/set_mode.hpp"

class RobotSupervisor : public rclcpp::Node {
public:
    RobotSupervisor() : Node("robot_supervisor") {

        // Endpoint 1: Power/Simulation
        toggle_srv_ = this->create_service<corebot_interfaces::srv::SetMode>(
            "/corebot/robot_turn_on_off",
            std::bind(&RobotSupervisor::handle_toggle_robot, this, std::placeholders::_1, std::placeholders::_2)
        );

        // Endpoint 2: Autonomy Modes
        mode_srv_ = this->create_service<corebot_interfaces::srv::SetMode>(
            "/corebot/set_mode",
            std::bind(&RobotSupervisor::handle_set_mode, this, std::placeholders::_1, std::placeholders::_2)
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

    void handle_set_mode(
        const std::shared_ptr<corebot_interfaces::srv::SetMode::Request> request,
        std::shared_ptr<corebot_interfaces::srv::SetMode::Response> response)
    {
        std::string target = request->mode;
        std::transform(target.begin(), target.end(), target.begin(), ::tolower);

        if (target == "slam") {
            // FIX 2: Explicitly stop Nav and WAIT for it to fully die before
            // starting SLAM. Without this, systemd's async Conflicts= can leave
            // Nav still running when SLAM tries to start, causing SLAM to fail.
            RCLCPP_INFO(this->get_logger(), "Stopping Nav before starting SLAM...");
            call_systemctl("stop", "corebot_nav.service"); // blocks until nav is dead

            response->success = call_systemctl("start", "corebot_slam.service");
            response->message = response->success
                ? "SLAM mode active."
                : "SLAM START failed — check journalctl for details.";
        }
        else if (target == "nav") {
            // FIX 2 (mirror): Stop SLAM and wait before starting Nav.
            // SLAM's KillSignal=SIGINT + TimeoutStopSec=20 means it can take up
            // to 20s to save the map — this call will block for that full duration,
            // which is correct: we must not start Nav until the map is saved.
            RCLCPP_INFO(this->get_logger(), "Stopping SLAM before starting Nav (map save in progress)...");
            call_systemctl("stop", "corebot_slam.service"); // blocks, may take up to 20s

            response->success = call_systemctl("start", "corebot_nav.service");
            response->message = response->success
                ? "Navigation mode active."
                : "Nav START failed — check journalctl for details.";
        }
        else if (target == "idle") {
            // Stop both and report combined success.
            bool s1 = call_systemctl("stop", "corebot_slam.service");
            bool s2 = call_systemctl("stop", "corebot_nav.service");
            response->success = s1 && s2;
            response->message = response->success
                ? "System IDLE."
                : "IDLE transition had errors — check journalctl for details.";
        }
        else {
            response->success = false;
            response->message = "Invalid mode. Use 'slam', 'nav', or 'idle'.";
        }
    }

    rclcpp::Service<corebot_interfaces::srv::SetMode>::SharedPtr toggle_srv_;
    rclcpp::Service<corebot_interfaces::srv::SetMode>::SharedPtr mode_srv_;
};

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<RobotSupervisor>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}