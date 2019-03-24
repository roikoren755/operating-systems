#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

int patternLength, processId, *childProcesses, *pipeDescriptors; // For the clean up function, that might be called from the SIGPIPE handler

int cleanUp() {
	for (int i = 0; i < patternLength; i++) {
		if (pipeDescriptors[i]) { // If the pipe is open
			kill(childProcesses[i], SIGTERM); // Kill the kid
			close(pipeDescriptors[i]); // And close the pipe
		}
	}

	free(childProcesses); // Free dynamic allocations of memory
	free(pipeDescriptors);
	return errno; // 0 if no error
}

void sigPipeHandler(int signal) {
	if (signal == SIGPIPE) {
		printf("SIGPIPE for manager process %d. Leaving.\n", processId);
		raise(cleanUp());
	}
}

int main(int argc, char* argv[]) {
	processId = getpid(); // For SIGPIPE handler
	patternLength = strlen(argv[2]);

	childProcesses = (int*) malloc(patternLength * sizeof(int));
	if (!childProcesses) {
		printf("Could not allocate memory for child process ids array.\n");
		raise(SIGTERM);
	}

	pipeDescriptors = (int*) malloc(patternLength * sizeof(int));
	if (!pipeDescriptors) {
		printf("Could not allocate memory for stopped counters array.\n");
		free(pipeDescriptors);
		raise(SIGTERM);
	}

	char stringChar[2];
	char* processArguments[] = {"./sym_count", argv[1], NULL, NULL}; // Arguments for child
	int pid;

	for (int i = 0; i < patternLength; i++) {
		int pipeFds[2]; // Prepeare for pipe
		if (pipe(pipeFds) == -1) { // Open (?) pipe
			printf("%s\n", strerror(errno));
			return cleanUp();
		}

		stringChar[0] = argv[2][i]; // Next character in pattern
		processArguments[2] = stringChar;

		pid = fork();
		if (pid > 0) { // Father
			childProcesses[i] = pid; // Register child
			pipeDescriptors[i] = pipeFds[0]; // Remember pipe
			close(pipeFds[1]); // Close writing end of it
		}
		else if (pid == 0) { // Child
			dup2(pipeFds[1], STDOUT_FILENO); // Child's stdout is pipe!
			close(pipeFds[0]); // Close pipe
			close(pipeFds[1]);
			if (execvp(processArguments[0], processArguments) == -1) { // Run sym_count
				printf("%s\n", strerror(errno)); // Error happened
				return cleanUp();
			}
		}
		else if (pid < 0) { // Error forking
			printf("%s\n", strerror(errno));
			return cleanUp();
		}
	}

	int processesLeft = patternLength;
	int childStatus;
	char buffer[20];
	int readBytes;

	while (processesLeft > 0) { // Child still running
		sleep(1);

		for (int i = 0; i < patternLength; i++) {
			if (pipeDescriptors[i] && // Pipe isn't closed
				(pid = waitpid(childProcesses[i], &childStatus, WNOHANG)) > 0) { // And child finished
				if (WIFEXITED(childStatus)) { // Exited normally?
					while ((readBytes = read(pipeDescriptors[i], buffer, 19)) > 0) { // Read a from appropriate pipe
						buffer[readBytes] = '\0';
						printf("%s", buffer); // Print it!
					}

					if (close(pipeDescriptors[i]) == -1) { // Close pipe
						printf("%s\n", strerror(errno));
						return cleanUp();
					}

					pipeDescriptors[i] = 0; // Bye bye
					processesLeft--; // One down
				}

				else { // Exited not normally?
					return cleanUp();
				}
			}
			else if (pid < 0) { // Error waiting on pid
				processesLeft = 0;
			}
		}
	}

	return cleanUp();
}
