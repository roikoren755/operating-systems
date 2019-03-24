#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BLOCK_SIZE (1024 * 1024)

int outputFileDescriptor; // So all threads can write to it
char xoredBuffer[BLOCK_SIZE] = { 0 }; // Initialized to all 0s
int currentBlock = 0;
int bytesXored = 0;
pthread_t* threads;
int* threadsLeft;
pthread_mutex_t mutex;
pthread_cond_t* conditions;

void cleanUp(int maximumFileBlocks) { // Free memory and destroy mutexes and condition variables
	free(threads);
	free(threadsLeft);
	for (int i = 0; i < maximumFileBlocks; i++) {
		pthread_cond_destroy(&conditions[i]);
	}
	pthread_mutex_destroy(&mutex);
	free(conditions);
}

void* threadFileReader(void* thread_param) {
	if (!thread_param) { // Safety
		exit(1);
	}

	char* inputFile = (char*) thread_param;
	int inputFileDescriptor = open(inputFile, O_RDONLY); // Open file for read
	if (inputFileDescriptor == -1) {
		printf("ERROR: Could not open %s.\n", inputFile);
		exit(1);
	}

	char buffer[BLOCK_SIZE];
	int readBytes;
	int writtenBytes;
	int block = 0;
	// As long as there's still something to read
	// (0 inclusive, to let others know the thread's finished)
	while ((readBytes = read(inputFileDescriptor, buffer, BLOCK_SIZE)) >= 0) {
		block++; // Read one more block
		if (pthread_mutex_lock(&mutex)) { // Lock for xoring
			printf("ERROR: Could not (even try) to lock mutex for %s's thread.\n", inputFile);
			exit(1);
		}
		threadsLeft[block - 1]--; // Shared, counter of running threads
		for (int i = 0; i < readBytes; i++) {
			xoredBuffer[i] ^= buffer[i]; // XOR
		}

		if (readBytes > bytesXored) {
			bytesXored = readBytes; // How many were XOR-ed
		}
		// Other threads are waiting to start the same block in their files
		if (threadsLeft[block - 1]) {
			if (pthread_cond_signal(&conditions[block - 1])) { // Get them ready
				printf("ERROR: Could not signal condition variable for %d-th block.\n", block);
				exit(1);
			}

			if (!readBytes) { // Not sticking around
				if (pthread_mutex_unlock(&mutex)) { // Let them race
					printf("ERROR: Could not unlock mutex by %s's thread.\n", inputFile);
					exit(1);
				}
				if (close(inputFileDescriptor)) { // Close file
					printf("ERROR: Could not close %s.\n", inputFile);
					exit(1);
				}

				pthread_exit(NULL); // All done!
			}

			threadsLeft[block]++; // Still here for next stage

			while (currentBlock != block) { // In case we wake up early
				if (pthread_cond_wait(&conditions[block], &mutex)) { // Just keep on waiting... *Waiting...*
					printf("ERROR: Could not wait on condition variable for %d-th block.\n", block + 1);
					exit(1);
				}
			}
		}
		else { // Last thread to run this block - it's XOR-ing time!
			if ((writtenBytes = write(outputFileDescriptor, xoredBuffer, bytesXored)) < bytesXored) { // Oopsie
				printf("ERROR: Could not write whole buffer for %d-th block.\n", block);
				exit(1);
			}

			// Clean up for next block
			bytesXored = 0;
			for (int i = 0; i < BLOCK_SIZE; i++) {
				xoredBuffer[i] = 0;
			}
			currentBlock++;
			if (pthread_cond_signal(&conditions[block])) { // Get ready
				printf("ERROR: Could not signal condition variable for %d-th block.\n", block);
			}

			if (!readBytes) { // But I'm done
				if (pthread_mutex_unlock(&mutex)) { // GO!
					printf("ERROR: Could not unlock mutex by %s's thread.\n", inputFile);
					exit(1);
				}
				if (close(inputFileDescriptor)) {
					printf("ERROR: Could not close %s.\n", inputFile);
					exit(1);
				}
				pthread_exit(NULL); // Bye bye :)
			}

			threadsLeft[block]++; // Still here

			while (currentBlock != block) { // Wait my turn
				if (pthread_cond_wait(&conditions[block], &mutex)) { // Waiting on the world to change...
					printf("ERROR: Could not wait on condition variable for %d-th block.\n", block + 1);
					exit(1);
				}
			}
		}

		if (pthread_mutex_unlock(&mutex)) { // Done, thank you :)
			printf("ERROR: Could not unlock mutex by %s's thread.\n", inputFile);
			exit(1);
		}
	}

	// If we got here, an error occurred while reading
	printf("ERROR: Could not read %d-th block of %s.\n", block + 1, inputFile);
	exit(1);
}

int main(int argc, char* argv[]) {
	if (argc < 3) { // Can you repeat that?
		printf("USAGE: ./hw4 <OUTPUT_FILE_PATH> <INPUT_FILE_PATH> [...]\n");
		exit(1);
	}

	int numberOfInputFiles = argc - 2;
	char* outputFileName = argv[1];
	printf("Hello, creating %s from %d input files\n", outputFileName, numberOfInputFiles);

	// creat(...) === open(O_WRONLY|O_CREAT|O_TRUNC,...)
	outputFileDescriptor = creat(argv[1], S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (outputFileDescriptor == -1) { // Could not creat
		printf("ERROR: Could not open output file.\n");
		exit(1);
	}

	// So we can join them later
	threads = (pthread_t*) malloc(sizeof(pthread_t) * numberOfInputFiles);
	if (!threads) {
		printf("ERROR: Could not allocate memory for thread management.\n");
	}

	struct stat st;
	int fileBlocks;
	int maximumFileBlocks = 0;
	for (int i = 0; i < numberOfInputFiles; i++) {
		if (stat(argv[i + 2], &st)) { // For file size...
			printf("ERROR: Could not calculate %d-th file size.\n", i + 1);
			exit(1);
		}

		fileBlocks = st.st_size / BLOCK_SIZE;
		if (fileBlocks > maximumFileBlocks) {
			maximumFileBlocks = fileBlocks; // So we can know how many blocks will be
		}
	}

	// Counters, to keep track of how many threads are waiting on each stage
	threadsLeft = (int*) calloc(sizeof(int) * (maximumFileBlocks + 1), sizeof(int));
	if (!threads) {
		printf("ERROR: Could not allocate memory for stage management.\n");
		exit(1);
	}
	threadsLeft[0] = numberOfInputFiles; // On the first one - ALL OF THEM

	if (pthread_mutex_init(&mutex, NULL)) { // Init mutex
		printf("ERROR: Could not initiate mutex.\n");
		exit(1);
	}

	// Condition variables - one for each block
	conditions = (pthread_cond_t*) malloc(sizeof(pthread_cond_t) * (maximumFileBlocks + 1));
	if (!conditions) {
		printf("ERROR: Could not allocate memory for condition variables.\n");
		exit(1);
	}

	// Init condition variables
	for (int i = 0; i < maximumFileBlocks + 1; i++) {
		if (pthread_cond_init(&conditions[i], NULL)) {
			printf("ERROR: Could not initiate condition variable for %d-th block.\n", i + 1);
			exit(1);
		}
	}

	int ret;
	for (int i = 0; i < numberOfInputFiles; i++) { // Create the threads
		pthread_t thread_id;
		ret = pthread_create(&thread_id, NULL, threadFileReader, (void*) argv[i + 2]);
		if (ret) {
			printf("ERROR: Could not create thread for %d-th input file.\n", i + 1);
			exit(1);
		}

		threads[i] = thread_id; // And keep track of them
	}

	for (int i = 0; i < numberOfInputFiles; i++) { // So we can join them
		ret = pthread_join(threads[i], NULL);
		if (ret) {
			printf("ERROR: Could not join thread for %d-th input file.\n", i + 1);
			exit(1);
		}
	}

	if (fstat(outputFileDescriptor, &st)) { // Get finished file size
		printf("ERROR: Could not calculate output file's length.\n");
		exit(1);
	}

	close(outputFileDescriptor); // Close file
	printf("Created %s with size %ld bytes\n", outputFileName, st.st_size);

	cleanUp(maximumFileBlocks); // Cleanliness is next to holiness

	pthread_exit(NULL); // Finished, thank you very much!
}