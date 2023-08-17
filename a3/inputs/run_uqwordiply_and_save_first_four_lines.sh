#!/bin/sh
# Usage: run_uqwordiply_and_save_first_four_lines.sh [args]
# Runs uqwordiply with the given arguments
# Only the first four lines from standard output will be saved (output
# to standard output)

# Run the program and only output the first 2 lines. 
${uqwordiply:=./uqwordiply} "$@" | head -4 
# There is a possibility uqwordiply gets a SIGPIPE. We just exit
# with exit status 0 here.
exit 0

