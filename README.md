This program, developed in C, mimics the basic functions of a shell in Linux. It will:

-Provide a prompt for running commands
-Handle blank lines and comments, which are lines beginning with the # character
-Provide expansion for the variable $$
-Execute 3 commands exit, cd, and status via code built into the shell
-Execute other commands by creating new processes using a function from the exec family of functions
-Support input and output redirection
-Support running commands in foreground and background processes
-Implement custom handlers for 2 signals, SIGINT and SIGTSTP

To compile, run:
  gcc -std=c11 -Wall -Werror -g3 -O0 -o smallsh smallsh.c
