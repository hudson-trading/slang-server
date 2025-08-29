#!/usr/bin/env python3

import subprocess
import sys
import threading


def relay_input(stdin_pipe, stdin_file, process_stdin):
    with open(stdin_file, "wb") as f:
        while True:
            chunk = stdin_pipe.read(1)
            if not chunk:
                break
            f.write(chunk)
            f.flush()
            process_stdin.write(chunk)
            process_stdin.flush()


def run_command(command):
    stdin_file = "/tmp/slang-server.stdin"
    stdout_file = "/tmp/slang-server.stdout"
    stderr_file = "/tmp/slang-server.stderr"

    process = subprocess.Popen(
        command,
        shell=True,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0,  # Line-buffered
    )

    stdin_thread = threading.Thread(
        target=relay_input, args=(sys.stdin.buffer, stdin_file, process.stdin)
    )
    stdin_thread.start()

    stdout_thread = threading.Thread(
        target=write_and_print_binary,
        args=(process.stdout, sys.stdout.buffer, stdout_file),
    )
    stdout_thread.start()

    stderr_thread = threading.Thread(
        target=write_and_print_binary,
        args=(process.stderr, sys.stderr.buffer, stderr_file),
    )
    stderr_thread.start()

    stdout_thread.join()
    stderr_thread.join()
    process.wait()
    stdin_thread.join()

    # Reading stdout.txt to skip the first line and print the rest
    with open(stdout_file, "r") as f:
        lines = f.readlines()

    if lines:
        # Skipping the first line
        sys.stdout.write("".join(lines[1:]))
        sys.stdout.flush()


def write_and_print_binary(pipe, dest, filename):
    with open(filename, "wb") as f:
        while True:
            chunk = pipe.read(1)
            if not chunk:
                break
            f.write(chunk)
            f.flush()
            dest.write(chunk)
            dest.flush()


if __name__ == "__main__":
    sys.argv.pop(0)
    command = "rr record " + " ".join(sys.argv)
    run_command(command)
