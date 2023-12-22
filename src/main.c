#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

pthread_mutex_t mutex;

//Solução do exercício 3, com o BARRIER em falta, e com um bug, onde o output é repetido pelo número das threads dado.
//De forma a testar independentemente o exercício 1 e 2 do exercício 3, substituir o main pelo main2.c dentro do folder
//"Exercício 1 e 2" e substituir o operations2.c e operations2.h pelos operations.c e operations.h.

struct ThreadArgs {
    char *filename;
    int thread_id;
    int line_number;
    int max_threads;
    int out_fd;
};
void *process_command_file(void *arg) {
  struct ThreadArgs *args = (struct ThreadArgs *)arg;
  char *filename = args->filename;
  int thread_id = args->thread_id;
  int line_number = args->line_number;
  int max_threads = args->max_threads;
  int input_fd = open(filename, O_RDONLY);
  int out_fd = args->out_fd;

  unsigned int reference_threadid;
  unsigned int event_id, delay;
  size_t num_rows, num_columns, num_coords;
  size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
  while(1){
    switch (get_next(input_fd)) {
      case CMD_CREATE:
        pthread_mutex_lock(&mutex);
        if (line_number % max_threads != thread_id && line_number % max_threads != 0) {
          break;
        }
        // Process CMD_CREATE
        if (parse_create(input_fd, &event_id, &num_rows, &num_columns) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
        }

        if (ems_create(event_id, num_rows, num_columns)) {
          fprintf(stderr, "Failed to create event\n");
        }
        pthread_mutex_unlock(&mutex);
        break;


      case CMD_RESERVE:
        pthread_mutex_lock(&mutex);
        if (line_number % max_threads != thread_id && line_number % max_threads != 0) {
          break;
        }
        num_coords = parse_reserve(input_fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

        if (num_coords == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
        }

        if (ems_reserve(event_id, num_coords, xs, ys)) {
          fprintf(stderr, "Failed to reserve seats\n");
        }
        pthread_mutex_unlock(&mutex);
        break;

      case CMD_SHOW:
        pthread_mutex_lock(&mutex);
        if (line_number % max_threads != thread_id && line_number % max_threads != 0) {
          break;
        }


        if (parse_show(input_fd, &event_id) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
        } else if (ems_show(event_id, out_fd)) {
          fprintf(stderr, "Failed to show event\n");
        }


        pthread_mutex_unlock(&mutex);
        break;

      case CMD_LIST_EVENTS:
        pthread_mutex_lock(&mutex);
        if (line_number % max_threads != thread_id && line_number % max_threads != 0) {
          break;
        }
        {
          if (ems_list_events(out_fd)) {
            fprintf(stderr, "Failed to list events\n");
          }
        }
        pthread_mutex_unlock(&mutex);
        break;

      case CMD_WAIT:
        if (parse_wait(input_fd, &delay, &reference_threadid) == -1) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
        }
        if (parse_wait(input_fd, &delay, &reference_threadid) == 0) {
          if (delay > 0) {
            printf("Waiting...\n");
            ems_wait(delay);
          }
          break;
        }
        if (parse_wait(input_fd, &delay, &reference_threadid) == 1) {
          if((unsigned int)thread_id == reference_threadid){
            if (delay > 0) {
              printf("Waiting...\n");
              ems_wait(delay);
            }
            break;
          }
        }
        break;

      case CMD_INVALID:
        if (line_number % max_threads != thread_id && line_number % max_threads != 0) {
          break;
        }
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        break;

      case CMD_HELP:
        if (line_number % max_threads != thread_id && line_number % max_threads != 0) {
          break;
        }
        {
          write(out_fd, "Available commands:\n"
                        "  CREATE <event_id> <num_rows> <num_columns>\n"
                        "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
                        "  SHOW <event_id>\n"
                        "  LIST\n"
                        "  WAIT <delay_ms> [thread_id]\n"
                        "  BARRIER\n"
                        "  HELP\n", 189);
        }
        break;

      case CMD_BARRIER:
      case CMD_EMPTY:
        if (line_number % max_threads != thread_id && line_number % max_threads != 0) {
          continue;
        }
        break;
      case EOC:
        close(input_fd);
        if(thread_id == max_threads){
          free(args->filename);  // Free the allocated filename
          free(args);  // Free the allocated ThreadArgs
        }
        pthread_exit(NULL);
    }
  }
}

int main(int argc, char *argv[]) {
  DIR *dir;
  struct dirent *entry;
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

  dir = opendir(argv[1]);

  if (dir == NULL) {
    perror("Error opening jobs directory");
    return 1;
  }

  int max_proc = atoi(argv[2]);
  int max_threads = atoi(argv[3]);
  if (max_proc <= 0 || max_threads <= 0) {
    fprintf(stderr, "Invalid max_proc or max_threads value\n");
    return 1;
  }

  if (argc > 3) {
    char *endptr;
    unsigned long int delay = strtoul(argv[3], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_ms = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }
  pthread_mutex_init(&mutex, NULL);

  int proc_count = 0;  // Keep track of the number of child processes

  while ((entry = readdir(dir)) != NULL && proc_count < max_proc) {
    char filename[PATH_MAX - 10];
    snprintf(filename, PATH_MAX - 10, "jobs/%s", entry->d_name);

    // Check if the entry is a regular file
    struct stat file_stat;
    if (stat(filename, &file_stat) == 0 && S_ISREG(file_stat.st_mode) &&
        strstr(entry->d_name, ".jobs") != NULL) {

      pid_t child_pid = fork();

      if (child_pid == -1) {
        perror("Error creating child process");
        exit(1);
      } else if (child_pid == 0) {
        // Child process
        struct ThreadArgs *args_array[max_threads];
        pthread_t threads[max_threads];
        int line_number = 0;
        char out_filename[PATH_MAX];
        char formatted_filename[PATH_MAX - 4];

        snprintf(formatted_filename, PATH_MAX - 4, "%s", filename);

        // Find the last occurrence of '.'
        char *dot_position = strrchr(formatted_filename, '.');

        // If a dot is found, replace the extension with ".out"
        if (dot_position != NULL) {
          *dot_position = '\0'; // Nullify the dot, removing the extension
        }

        // Append ".out" to the formatted filename
        snprintf(out_filename, PATH_MAX, "%s.out", formatted_filename);

        // Open the file and pass the file descriptor to the args struct
        int out_fd = open(out_filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (out_fd == -1) {
          perror("Error opening out file");
          fprintf(stderr, "Filename: %s\n", out_filename);
          exit(1);
        }

        for (int thread_count = 0; thread_count < max_threads; ++thread_count) {
          args_array[thread_count] = malloc(sizeof(struct ThreadArgs));
          args_array[thread_count]->filename = strdup(filename);
          args_array[thread_count]->thread_id = thread_count + 1;
          line_number++;
          args_array[thread_count]->line_number = line_number;
          args_array[thread_count]->max_threads = max_threads;
          // Create out_file for the thread
          args_array[thread_count]->out_fd = out_fd;

          pthread_create(&threads[thread_count], NULL, process_command_file, args_array[thread_count]);
        }

        // Wait for all threads to finish
        for (int i = 0; i < max_threads; ++i) {
          pthread_join(threads[i], NULL);
        }
        close(out_fd);


        // Exit the child process after threads finish
        exit(0);
      } else {
        // Parent process
        printf("Child process for file %s started with PID %d\n", entry->d_name, child_pid);
        proc_count++;
      }
    }
  }

  // Close the directory
  closedir(dir);

  // Wait for all child processes to finish
  int status;
  pid_t wpid;
  while ((wpid = waitpid(-1, &status, 0)) > 0) {
    // Handle the termination of each child process
    printf("Child process %d terminated with status %d\n", wpid, status);
  }

  ems_terminate();

  return 0;
}