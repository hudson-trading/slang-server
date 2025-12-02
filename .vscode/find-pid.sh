#!/bin/bash

# Define the path to your server executable
SERVER_PATH="${1}"

# Loop until a PID is found
PID=$(pgrep -f "${SERVER_PATH}" | grep -v $$ | head -n 1)
while [ -z "$PID" ]; do
    sleep 1
    PID=$(pgrep -f "${SERVER_PATH}" | grep -v $$ | head -n 1)
done

# Print the PID to standard output
echo "$PID"
