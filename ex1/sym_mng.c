#define _POSIX_SOURCE
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

int cleanUp(int* childProcesses, int* stoppedCounters, int numberOfProcesses, int bound) {
	for (int i = 0; i < numberOfProcesses; i++) {
		if (stoppedCounters[i] < bound) {
			kill(childProcesses[i], SIGTERM);
		}
	}

	free(childProcesses);
	free(stoppedCounters);
	return errno;
}

int main(int argc, char* argv[]) {
	int bound = atoi(argv[3]);
	int patternLength = strlen(argv[2]);

	int* childProcesses = (int*) malloc(patternLength * sizeof(int));
	if (!childProcesses) {
		printf("Could not allocate memory for child process ids array.\n");
		raise(SIGTERM);
	}

	int* stoppedCounters = (int*) malloc(patternLength * sizeof(int));
	if (!stoppedCounters) {
		printf("Could not allocate memory for stopped counters array.\n");
		free(childProcesses);
		raise(SIGTERM);
	}

	char stringChar[2];
	char* processArguments[] = {"./sym_count", argv[1], NULL, NULL};
	int pid;

	for (int i = 0; i < patternLength; i++) {
		stringChar[0] = argv[2][i];
		processArguments[2] = stringChar;

		pid = fork();
		if (pid > 0) {
			childProcesses[i] = pid;
		}
		else if (pid == 0) {
			if (execvp(processArguments[0], processArguments) == -1) {
				printf("%s\n", strerror(errno));
				return cleanUp(childProcesses, stoppedCounters, patternLength, bound);
			}
		}
		else if (pid < 0) {
			printf("%s\n", strerror(errno));
			return cleanUp(childProcesses, stoppedCounters, patternLength, bound);
		}
	}

	int processesLeft = patternLength;
	int childStatus;

	while (processesLeft > 0) {
		sleep(1);

		for (int i = 0; i < patternLength; i++) {
			if (stoppedCounters[i] < bound &&
					(pid = waitpid(childProcesses[i], &childStatus, WNOHANG | WUNTRACED)) > 0) {
				if (WIFSTOPPED(childStatus)) {
					stoppedCounters[i]++;

					if (kill(childProcesses[i], stoppedCounters[i] < bound ? SIGCONT : SIGTERM) == -1 ||
							(stoppedCounters[i] == bound && kill(childProcesses[i], SIGCONT) == -1)) {
						printf("%s\n", strerror(errno));
						return cleanUp(childProcesses, stoppedCounters, patternLength, bound);
					}
				}
				else if (WIFEXITED(childStatus)) {
					stoppedCounters[i] = bound;
				}

				if (stoppedCounters[i] >= bound) {
					processesLeft--;
				}
			}
			else if (pid < 0) {
				processesLeft = 0;
			}
		}
	}

	return cleanUp(childProcesses, stoppedCounters, patternLength, bound);
}
