#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <utime.h>
#include <math.h>
#include <errno.h>
#include <stdbool.h>
#include <dirent.h>
#include <getopt.h>
#include <ctype.h>
#include <limits.h>
#include <sys/wait.h>
#include <signal.h>




// Constants
#define MAX_PROCESSES   256  // BG processes



// Global Variables
char argsStr[2048 + 1];  // raw string input from command line
char* argV[512];  // 2D array of individual word arguments
int argC = 0;  // number of arguments

_Bool isAllSpace = 0;  // switch if user did not enter any but just spaces

_Bool isBG = 0;  // toggle if there is & at the end
int allowBG = 1;  // toggle for foreground-only

char curDirBuf[3000];  // for CD built-in

pid_t spawnpid = -5;  // holds the child's ID
int childStatus = 0;  // holds the child's status

int num_processes = 0;  // number of processes running in the background
int processes[MAX_PROCESSES] = {0};  // array that stores the BG process IDs






// Function prototypes list
int getCmds(char* argsStr);  // get and parse command line arguments
void execCmds(struct sigaction sa, struct sigaction saz);  // execute said arguments

void mainCmds(char** argV, struct sigaction sa, struct sigaction saz);  // commands other than the 3 built-ins

// the 3 built-ins
void cd_func();
void exit_func();
void status_func();

char *expand_token(char *s);  // for $$ variable expansion
char *get_pid_str();  // string version of pid, used by expand_token()

// for BG processes management
void add_process(int process);  // adds BG process ID into the log
void remove_process(int process);  // removes BG process ID from the log
void kill_active_processes();  // kills the processes and removes their IDs from the log
void kill_all_bg_processes();  // identify defuncts and removes their IDs from the log, then kills the rest of the processes

void reap_child();  // reap zombies always before giving the command line back to the user

// for both Ctrl C and Ctrl Z signals
struct sigaction sigint = {{0}};
struct sigaction sigtstp = {{0}};
void setup_signals();
void sighandler_sigtstp(int sig);  // for Ctrl Z
void sigsetmost(sigset_t* set);  // for Ctrl Z
void sighandler_null(int sig);  // for Ctrl Z
void checkFG(int status);  // prints status when an FG process is terminated by Ctrl C

void cleanStr();  // cleans the arrays for strings for the next command line arguments




int main(){

    setup_signals();  // for Ctrl Z and Ctrl C


    // keep prompting the user for a command
    while(1 == 1){

        getCmds(argsStr);  // get and parse command line arguments

        execCmds(sigint, sigtstp);  // execute said arguments


        reap_child();  // reap zombies always before giving the command line back to the user


        cleanStr();  // cleans the arrays for strings for the next command line arguments


    }


    return 0;

}







int getCmds(char* argsStr){

    isAllSpace = 0;  // reset to not assume that it's all white space
    argC = 0;  // reset number of arguments
    isBG = 0; // reset to FG by default



    printf(": ");
    fflush(stdout);

    fgets(argsStr, 2048 + 1, stdin);


    strtok(argsStr, "\n");  // removes the \n at the end of the line

    if (argsStr[strlen(argsStr) - 1] == '&'){
        strtok(argsStr, "&");  // removes the & at the end of the line
        if (allowBG == 1)
            isBG = 1;
    }


    argsStr = expand_token(argsStr);


    // get the first argument word
    char* tkn = strtok(argsStr, " ");

    if (tkn == NULL)
        isAllSpace = 1;


    // get the rest of the argument words
    while(tkn != NULL){

        argV[argC] = tkn;
        tkn = strtok(NULL, " ");  // gets the next token

        argC++;

    }

    return argC;
}






void execCmds(struct sigaction sa, struct sigaction saz){

    // ignore comments and blank lines
    // if it detected just \n, it means that the user didn't input anything and just pressed enter
    if (isAllSpace == 1 || argV[0][0] == '#' || argV[0][0] == '\n') 
        return;

    else if (strcmp(argV[0], "cd") == 0) 
		cd_func();
	
    else if (strcmp(argV[0], "exit") == 0) 
        exit_func();
    
    else if (strcmp(argV[0], "status") == 0)
        status_func();
    
    else 
        mainCmds(argV, sa, saz);
    

}




void cd_func(){

    if (argC == 1)
        chdir(getenv("HOME"));  // if no arguments provided, then just go to home directory
    else
        chdir(argV[1]);

    printf("%s\n", getcwd(curDirBuf, sizeof(curDirBuf)));
    fflush(stdout);
    
}


void exit_func(){
    kill_all_bg_processes();  // kills all BG processes made by smallsh before exiting
    exit(0);
}


void status_func(){

	int exitedStat = 0; 
    int signaledStat = 0; 

    int exitVal = 0;


	waitpid(getpid(), &childStatus, 0);		// Check the status of the last process


	if(WIFEXITED(childStatus)) 
        exitedStat = WEXITSTATUS(childStatus);	// Return the status of the normally terminated child

    else if(WIFSIGNALED(childStatus)) 
        signaledStat = WTERMSIG(childStatus);		// Return the status of an abnormally terminated child



    if (exitedStat + signaledStat == 0)  // if either was triggered, then exitVal = 1
        exitVal = 0;
    else
        exitVal = 1;



    if(signaledStat == 0) 
    	printf("exit value %d\n", exitVal);
    else {
    	printf("terminated by signal %d\n", signaledStat);  // prints with the signal number
    }

    fflush(stdout);


    // cleanup
	exitedStat = 0; 
    signaledStat = 0; 

    exitVal = 0;


}






// convert pid to string and return it
char *get_pid_str() {
    int pid = getpid();
    char *s = malloc(32);
    sprintf(s, "%d", pid);
    return s;
}



char *expand_token(char *s)
{
    const char* term = "$$";
	const char *replace = get_pid_str();
	
    // if no $$ is found, return the argument as it is
    if (!strstr(s, term)) {
        return strdup(s);
    }
  
    // line will be <= incoming line length, as len(^) < len(++)
    char *buf = malloc(4096);
    char* p = NULL;
    char* rest = s;    
    while ((p = strstr(rest, term))) {
        strncat(buf, rest, (size_t)(p - rest));
        strcat(buf, replace);
        rest = p + strlen(term);
    }    
    strcat(buf, rest);
    return buf;   
}








// commands other than the 3 built-ins
void mainCmds(char** argV, struct sigaction sa, struct sigaction saz){


    // If fork is successful, the value of spawnpid will be 0 in the child, the child's pid in the parent
    spawnpid = fork();

    switch (spawnpid){


        case -1: ;
            perror("fork() failed!");
            exit(1);
            break;


        case 0: ;  // Child's side


            char in_file_name[2048 + 1];
            char out_file_name[2048 + 1];
            _Bool has_in_fd = 0;
            _Bool has_out_fd = 0;



            // if FG
            if (isBG == 0){
                // deal with Ctrl C
                sa.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sa, NULL);

                // when FG process is hit with Ctrl Z, push the process into the BG
                saz.sa_handler = SIG_IGN;
                sigset_t newmask;
                sigsetmost(&newmask);
                saz.sa_mask = newmask;
                sigaction(SIGTSTP, &saz, NULL);
            }



            // check if there's ">" or "<"
            for (int i = 0; i < argC; i++){

                // if has <
                if ( strcmp(argV[i], "<") == 0 ){

                    has_in_fd = 1;
                    argV[i] = NULL;
                    strcpy(in_file_name, argV[i + 1]);
                    i++;  // i++ because we don't want to stay in a NULL value, because when strcmp hits a NULL later, it crashes

                }


                // if has >
                if ( strcmp(argV[i], ">") == 0 ){

                    has_out_fd = 1;
                    argV[i] = NULL;
                    strcpy(out_file_name, argV[i + 1]);
                    i++;  // i++ because we don't want to stay in a NULL value, because when strcmp hits a NULL later, it crashes

                }


            }






            // change std in
            if (has_in_fd == 1){

                int in_fd = 0;

                // if error
                if ( (in_fd = open(in_file_name, O_RDONLY)) < 0 ){
                    fprintf(stderr, "Something went wrong with opening %s\n", in_file_name);
                    fflush(stdout); 
                    exit(1); 
                }

                dup2(in_fd, 0);  // redirect

                close(in_fd);

            } // redirection for BG
            else if (isBG == 1 && has_in_fd == 0){
                int bg_in = 0;
                // if error
                if ( (bg_in = open("/dev/null", O_RDONLY)) < 0 ){
                    fprintf(stderr, "Something went wrong with opening bg_in\n");
                    fflush(stdout); 
                    exit(1); 
                }
                dup2(bg_in, 0);  // redirect
                close(bg_in);
            }



            // change std out
            if (has_out_fd == 1){

                int out_fd = 0;

                // if error
                if ( (out_fd = open(out_file_name, O_WRONLY | O_CREAT | O_TRUNC, 0777)) < 0 ){
                    fprintf(stderr, "Something went wrong with opening %s\n", out_file_name);
                    fflush(stdout); 
                    exit(1); 
                }

                dup2(out_fd, 1);  // redirect

                close(out_fd);

            } // redirection for BG
            else if (isBG == 1 && has_out_fd == 0){
                int bg_out = 0;
                // if error
                if ( (bg_out = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0777)) < 0 ){
                    fprintf(stderr, "Something went wrong with opening bg_out\n");
                    fflush(stdout); 
                    exit(1); 
                }
                dup2(bg_out, 1);  // redirect
                close(bg_out);
            }





            // finally executes the mainCmds
            if ( (execvp(argV[0], argV)) == -1 ){
                perror(argV[0]);
                exit(1); 
            }

            
            break;




        default: ;  // Parent's side

            if (isBG == 1){
                add_process(spawnpid);  // log the BG process
                printf("background pid is %d\n", spawnpid);
		        fflush(stdout);
            }
            else if (isBG == 0){  // if FG
                spawnpid = waitpid(spawnpid, &childStatus, 0);  
                checkFG(childStatus);  // prints status when an FG process is terminated by Ctrl C
            }


    }



}




/* MANAGING BG PROCESSES */

void add_process(int process){  // adds BG process ID into the log
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processes[i] == 0)
        {
            fflush(stdout);
            processes[i] = process;
            break;
        }
    }
}

void remove_process(int process){  // removes BG process ID from the log
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processes[i] == process)
        {
            fflush(stdout);
            processes[i] = 0;
            break;
        }
    }
}

void kill_active_processes(){  // kills the processes and removes their IDs from the log
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processes[i] != 0)
        {
            fflush(stdout);
            kill(processes[i], SIGKILL);
            processes[i] = 0;
        }
    }
}

void kill_all_bg_processes(){  // identify defuncts and removes them from the log, then kills the rest of the processes
    int status;
    int pid;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0){
        remove_process(pid);
    }
    kill_active_processes();
}



void reap_child(){  // reap zombies, print that it has terminated, and remove its ID from the log

    while( (spawnpid = waitpid(-1, &childStatus, WNOHANG)) > 0){
        printf("background pid %d is done: ", spawnpid);
        fflush(stdout);
        status_func();
        remove_process(spawnpid);
    }
        
}






void setup_signals(){

    // Add Ctrl C handler
    sigint.sa_handler = SIG_IGN;
    sigint.sa_flags = 0;
    sigaction(SIGINT, &sigint, NULL);


    // Add Ctrl Z handler
    sigtstp.sa_handler = &sighandler_sigtstp;
    sigtstp.sa_flags = 0;
    sigaction(SIGTSTP, &sigtstp, NULL);
    
}


/* REFERED TO SO THAT WHEN CTRL Z HITS A RUNNING FG, IT IS PUSHED INTO BG */
void sigsetmost(sigset_t* set){
        sigfillset(set);
        sigdelset(set,SIGBUS);
        sigdelset(set,SIGFPE);
        sigdelset(set,SIGILL);
        sigdelset(set,SIGSEGV);
}
void sighandler_null(int sig){}

// signal handler function for Ctrl Z
void sighandler_sigtstp(int sig){

    // FG toggle
    if (allowBG == 0){

        allowBG = 1;

        char* message = "\nExiting foreground-only mode\n";
		write(1, message, 32);

    }
    else {  // if allowBG == 1, switch to 0 so that it's foreground

        allowBG = 0;

        char* message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(1, message, 51);

    }

}


// prints status when an FG process is terminated by Ctrl C
void checkFG(int status){
    if(!WIFEXITED(status)){
        printf("\nterminated by signal %d\n", WTERMSIG(status));
    }
}




// cleans the arrays for strings for the next command line arguments
void cleanStr(){

    memset(argV, '\0', sizeof(argV));
    memset(argsStr, '\0', 2048 + 1);

}