#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void run_child_process(int process_id) {
  srand(time(NULL) + process_id); // Seed the random number generator with the
                                  // current time and process ID

  if (process_id <= 2) {
    // Set operation processes
    while (1) {
      int random_number =
          rand() % 100 + 1; // Generate a random number between 1 and 100
      char command[20];
      sprintf(command, "./kvdb set apple %d", random_number);
      printf("Process %d: %s\n", process_id, command);
      system(command);
      usleep(rand() % 1000000); // Sleep for a random period (up to 1 second)
    }
  } else if (process_id == 3) {
    // Get operation process
    while (1) {
      printf("Process %d: Running get operation\n", process_id);
      system("./kvdb get apple");
      usleep(rand() % 1000000); // Sleep for a random period (up to 1 second)
    }
  } else if (process_id == 4) {
    // Del operation process
    while (1) {
      printf("Process %d: Running del operation\n", process_id);
      system("./kvdb del apple");
      sleep(10); // Sleep for 10 seconds
    }
  } else if (process_id == 5) {
    // Ts operation process
    while (1) {
      printf("Process %d: Running ts operation\n", process_id);
      system("./kvdb ts apple");
      usleep(rand() % 1000000); // Sleep for a random period (up to 1 second)
    }
  }
}

/**
 * @brief
 * A simple unit test, that create 5 processes.
 * The first and second processes will run set apple <random number>
 * The third process will run get apple
 * The forth process will run del apple
 * The fifth process will run ts apple
 *
 * @return int
 */

int main() {
  for (int i = 1; i <= 5; i++) {
    pid_t pid = fork();

    if (pid == -1) {
      printf("Error forking child process\n");
      exit(1);
    } else if (pid == 0) {
      // Child process
      run_child_process(i);
      exit(0);
    }
  }

  // Sleep for 60 seconds
  sleep(60);

  // Terminate all child processes
  for (int i = 0; i < 5; i++) {
    kill(0, SIGTERM);
  }

  // Wait for all child processes to finish
  for (int i = 0; i < 5; i++) {
    wait(NULL);
  }

  return 0;
}
