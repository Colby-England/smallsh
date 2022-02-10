#define _POSIX_C_SOURCE  200809L

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdbool.h>

// based on the specifications define max input string length and maximum number of arguments
#define MAX_LENGTH       2048
#define MAX_ARGS         512

// Define global variables
bool foreground_only = false;
char* input_file[1];
char* output_file[1];
bool background_process = false;
char* input_args[512];


// Removes the newline character at the end of the input string
void removeNewLine(char* input) {
    input[strcspn(input, "\n")] = '\0';
}

// Reset variables for new user input
void reset_variables() {
    for (int i = 0; i < 512; i++) {
        input_args[i] = NULL;
    }

    input_file[0] = NULL;
    output_file[0] = NULL;
    background_process = false;
}

// Expands $$ into the pid of the parent process
char* expandVariables(char* command, int pid) {
        
        // Create a string from the pid of the parent process
        char pid_str[80];
        sprintf(pid_str, "%d", pid);
        int pidLen = strlen(pid_str);
        
        // initialize variables 
        char* expansion_string = "$$";
        char* command_string = strdup(command);
        int originalLength = strlen(command_string);
        int expansion_count = 0;
        int expansion_len = strlen(expansion_string);

        // count the number of distince occurrences of '$$' in the input string
        while ((command_string = strstr(command_string, expansion_string))) {
            command_string += expansion_len;
            expansion_count++;
        }

        // create new string to hold expanded command
        char* expanded = (char*)malloc(originalLength + (expansion_count * (pidLen - 2) + 1));


        int i = 0;
        command_string = strdup(command);

        // replace occurences of $$ with pid_string
        while(*command_string) {
            if (strstr(command_string, expansion_string) == command_string) {
                strcpy(&expanded[i], pid_str);
                i += pidLen;
                command_string += 2;
            }
            else {
                expanded[i++] = *command_string++;
            }
        }

        return expanded;
}

// This function will parse the user's input and store each command and argument
// in an array.
void getUserInput(char* arr[], int pid, char* input_path[], char* output_path[], bool* background) {

    // Initialize variables to flag if the input or output symbol is found
    bool input = false;
    bool output = false;
    int doNotAdd = 0;
    char userInput[MAX_LENGTH];

    // Get input from user
    printf(": ");
    fflush(stdout);
    fgets(userInput, MAX_LENGTH, stdin);
    removeNewLine(userInput);

    // if stdtstp is sent clear the error on stdin and reset the variables to await next input
    if(ferror(stdin)) {
        clearerr(stdin);
        reset_variables();
        return;
    }

    // Check if userInput is blank
    if(strcmp(userInput, "") == 0) {
        return;
    }

    // Check if userInput is a comment
    if (strcmp(userInput, "#") == 0) {
        return;
    }

    // Split userInput into invidiuald commands and arguments
    char* delimeter = strtok(userInput, " ");
    int j = 0;

    while (delimeter != NULL) {
        
        // expand v$$ into parent PID
        char* expanded = expandVariables(delimeter, pid);

        // If the input or output flag was set on the previous iteration then copy the file name 
        // into the appropriate string.
        if (input) {
            input_path[0] = strdup(expanded);
            input = 0;
            doNotAdd++;
        }
        if (output) {
            output_path[0] = strdup(expanded);
            output = 0;
            doNotAdd++;
        }

        // check for input or output symbol and set flags
        if (strcmp(expanded, "<") == 0) {
            input = true;
            doNotAdd++;
        } else if (strcmp(expanded, ">") == 0) {
            output = true;
            doNotAdd++;
        }

        // check for background command and set flag.
        if (strcmp(expanded, "&") == 0) {
            *background = true;
            doNotAdd++;
        }

        // Ignore inputs that aren't actual commands, add actual commands to argument array
        if (doNotAdd > 0) {
            delimeter = strtok(NULL, " ");
        } else {
            arr[j] = strdup(expanded);
            delimeter = strtok(NULL, " ");
            j = j + 1;
        }

        // free memory and reset doNotAdd flag
        doNotAdd = 0;
        free(expanded);
    }
}

// Prints the exit status of the child process depending on if the exit was from a status or a signal
// code is modeled from example in module 'Process API - Monitoring Child Processes'
void printStatus(int childExitStatus) {

    if (WIFEXITED(childExitStatus)) {
        printf("exit value %d\n", WEXITSTATUS(childExitStatus));
    } else {
        printf("terminated by signal %d\n", WTERMSIG(childExitStatus));
    }
}

// Fork and run external commands
void externalCommand(char* args[], char input_path[], char output_path[], bool background_process, struct sigaction sigint, struct sigaction sigtstp, int* exitStatus) {

    // initialize variables
    int source, target, result;
    pid_t childpid = -5;
    childpid = fork();


    switch(childpid) {
    case -1:                            // fork failed to execute
        perror("fork() failed!");
        exit(1);
        break;

    case 0:                             /// fork success
        
        // Child process uses default action for SIGINT
        sigint.sa_handler = SIG_DFL;
        sigaction(SIGINT, &sigint, NULL);

        // Child process ignroe SIGTSTP
        sigtstp.sa_handler = SIG_IGN;
        sigaction(SIGTSTP, &sigtstp, NULL);

        // Input redirection
        if (input_path != NULL) {
            source = open(input_path, O_RDONLY);

            // error if the source is not a valid file
            if (source == -1) {
                perror("source open()");
                exit(1);
            }

            // redirect
            result = dup2(source, 0);
            if (result == -1) {
                perror("source dup2()");
                exit(2);
            }
        }

        // Output redirection
        if (output_path != NULL) {
            target = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);

            // error
            if (target == -1) {
                perror("target open()");
                exit(1);
            }

            // redirect
            result = dup2(target, 1);
            if (result == -1) {
                perror("source dup2()");
                exit(2);
            }
        }

        // print error if execvp fails, I tried to use the format from the module
        // without the if loop, but if execvp is succesful then the break statment is never reached
        if (execvp(args[0], (char* const*)args)) {
            printf("%s: no such file or directory\n", args[0]);
            fflush(stdout);
            exit(2);
        }

        break;

    // parent process
    default:
        // check background flags and run process in background if set
        if (!foreground_only && background_process) {
            printf("background pid is %d\n", childpid);
            fflush(stdout);
            waitpid(childpid, exitStatus, WNOHANG);
        // run process in foreground
        } else {
            waitpid(childpid, exitStatus, 0);
        }
    
    // check for terminated background processes. Using pid of -1 means it will check for any terminated
    // process and out put the termination message and the exit status. 
    while ((childpid = waitpid(-1, exitStatus, WNOHANG)) > 0){
        printf("background process: %d terminated.\n", childpid);
        printStatus(*exitStatus);
        fflush(stdout);
    }
    }
}

// custom handler for SIGSTP
void handle_SIGSTP() {
    
    // If not in foreground_only mode set flag and print message to user
    if (foreground_only == false) {
        foreground_only = true;
        printf("\nEntering foreground-only mode (& is now ignored)\n");
        fflush(stdout);
    // if in foreground_only mode clear flag and print message to user
    } else {
        foreground_only = false;
        printf("\nExiting foreground-only mode.\n");
        fflush(stdout);
    }

    // reset_variables for next user_input
    reset_variables();
}

// Main function contains main loop and built-in commands
int main() {
    
    // initialize variables
    bool running = true;  
    int pid = getpid();
    int status = 0;

    // make sure all variables are reset at beginning
    reset_variables();

    // signal handlers taken from example in module
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handle_SIGSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Main loop to recieve input from user and handle built-in commands
    do {

        // get input from user and parse
        getUserInput(input_args, pid, input_file, output_file, &background_process);

        if (input_args[0] == NULL) {                        // Handles null input
            reset_variables();
            continue;
        }else if (strncmp(input_args[0], "#", 1) == 0) {    // Handles Comments
            reset_variables();
            continue;
        }else if (strcmp(input_args[0], "") == 0) {         // Handles Blank lines
            reset_variables();
            continue;
        }else if (strcmp(input_args[0], "exit") == 0) {     // Handles the built-in exit command
            running = 0;
        }else if (strcmp(input_args[0], "cd") == 0) {       // Handles the built-in cd command

            // attempt to change the directory
            int dir_status = chdir(input_args[1]);
            
            // check if path was specified print error if the path doesn't exist
            if (input_args[1] != NULL) {
                if (dir_status == -1) {
                    printf("smallsh: cd: %s: No such file or directory\n", input_args[1]);
                    fflush(stdout);
                }
            } else {
                chdir(getenv("HOME"));          // if no path change to home directory
                }
        } else if (strcmp(input_args[0], "status") == 0)  {
            printStatus(status);                                // print status
        } else {
            externalCommand(input_args, input_file[0], output_file[0], background_process, SIGINT_action, SIGTSTP_action, &status);
        }

        // reset variables for next input
        reset_variables();


    } while(running);

    return(0);
}
