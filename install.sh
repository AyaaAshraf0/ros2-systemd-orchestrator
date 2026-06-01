#!/bin/bash

if [ "$EUID" -ne 0 ]; then 
  echo "Please run as root (sudo ./install.sh)"
  exit 1
fi

# 1. Setup Environment
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_SRC="$SCRIPT_DIR/services"
TARGET_DIR="/etc/systemd/system"

# 2. Identify the actual user to run the services
if [ -n "$SUDO_USER" ]; then
  ACTUAL_USER="$SUDO_USER"
else
  ACTUAL_USER=$(whoami)
fi
echo "Services will be configured to run as user: $ACTUAL_USER"

# 3. Create 'robot' group for hardware access
if ! getent group robot > /dev/null; then
  groupadd robot
  echo "Created 'robot' group."
fi

# Add the user to the robot group if not already a member
usermod -aG robot "$ACTUAL_USER"

# 4. Inject dynamic paths and user into service files
echo "Deploying services from $SERVICE_SRC..."
for file in "$SERVICE_SRC"/*.service; do
    filename=$(basename "$file")
    sed -e "s|REPLACE_ME_WORKSPACE|$SCRIPT_DIR|g" \
        -e "s|REPLACE_ME_USER|$ACTUAL_USER|g" "$file" > "$TARGET_DIR/$filename"
done

# 4. Set permissions
chmod 644 "$TARGET_DIR"/corebot_*.service

# 5. Reload and Start
systemctl daemon-reload
systemctl enable corebot_supervisor_node.service corebot_core_bridge.service
systemctl restart corebot_supervisor_node.service corebot_core_bridge.service

echo "Installation complete. System is now running in isolated mode."
