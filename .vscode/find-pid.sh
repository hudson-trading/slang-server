#!/bin/bash

# Define the path to your server executable
SERVER_PATH="${1}/build/vscode/bin/slang-server"

# Loop until a PID is found
PID=$(pgrep -f "${SERVER_PATH}")
while [ -z "$PID" ]; do
    sleep 1
    PID=$(pgrep -f "${SERVER_PATH}")
done

# Print the PID to standard output
echo "$PID"
