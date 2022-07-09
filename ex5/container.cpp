#include <sys/mount.h>
#include <sched.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cstring>
#include <fstream>
#include <iostream>
#define MAX_HOSTNAME 256
#define STACK_SIZE 8192
#define MKDIR_MOOD 0755

/**
 * struct that represent the arguments.
 */
struct Argv {
    char *new_hostname;
    char *new_fs_directory;
    int num_of_processes;
    char *program_to_run;
    char *program_args;
    int child_pid;
};

using namespace std;

/**
 * crete new directories and files, that limit the number of processes.
 * @param child_pid - new process child_pid.
 * @param num_of_processes - max number of processes.
 * @return 1 if success, -1 otherwise.
 */
int create_directories (int child_pid, int num_of_processes) {
  int dir_ret;
  dir_ret = mkdir ("/sys/fs", MKDIR_MOOD); // create fs directory
  if (dir_ret == 0 or (dir_ret == -1 and errno == EEXIST)) {
    dir_ret = mkdir ("/sys/fs/cgroup", MKDIR_MOOD); // create cgroup directory
    if (dir_ret == 0 or (dir_ret == -1 and errno == EEXIST)) {
      mkdir ("/sys/fs/cgroup/pids", MKDIR_MOOD); // create pids directory
    }
    else {
      return -1;
    }
  }
  else {
    return -1;
  }

  ofstream f_procs, f_max, f_release;

// open files
  f_procs.open ("/sys/fs/cgroup/pids/cgroup.procs");
  f_max.open ("/sys/fs/cgroup/pids/pids.max");
  f_release.open ("/sys/fs/cgroup/pids/notify_on_release");

// write to files
  f_procs << child_pid;
  f_max << num_of_processes;
  f_release << 1;

// close files
  f_max.close ();
  f_procs.close ();
  f_release.close ();
  return 1;
}

/**
 * function of child
 * @param args struct with arguments
 * @return 1 if success
 */
int run_child (void *args) {
  Argv *s = (Argv *) args;
  s->child_pid = getpid ();
  sethostname (s->new_hostname, strlen (s->new_hostname)); // change hostname
  chroot (s->new_fs_directory); // change filesystem root
  mount ("proc", "/proc", "proc", 0, nullptr); //change proc directory
  create_directories (s->child_pid, s->num_of_processes);

// run new program.
  execvp (s->program_to_run, (char *const *) s->program_args);
  return 1;
}

/**
 * delete all directories
 * @param args struct with arguments
 * @return 1 if success
 */
int delete_directories (void *args) {
  Argv *s = (Argv *) args;

// unlink files
  char *buff1[strlen (s->new_fs_directory) + 1];
  strcpy (reinterpret_cast<char *>(buff1), s->new_fs_directory);
  unlink (strcat (reinterpret_cast<char *>(buff1), "/sys/fs/cgroup/pids/cgroup.procs"));

  char *buff2[strlen (s->new_fs_directory) + 1];
  strcpy (reinterpret_cast<char *>(buff2), s->new_fs_directory);
  unlink (strcat (reinterpret_cast<char *>(buff2), "/sys/fs/cgroup/pids/pids.max"));

  char *buff3[strlen (s->new_fs_directory) + 1];
  strcpy (reinterpret_cast<char *>(buff3), s->new_fs_directory);
  unlink (strcat (reinterpret_cast<char *>(buff3), "/sys/fs/cgroup/pids/notify_on_release"));

// remove directories
  char *buff4[strlen (s->new_fs_directory) + 1];
  strcpy (reinterpret_cast<char *>(buff4), s->new_fs_directory);
  rmdir (strcat (reinterpret_cast<char *>(buff4), "/sys/fs/cgroup/pids"));

  char *buff5[strlen (s->new_fs_directory) + 1];
  strcpy (reinterpret_cast<char *>(buff5), s->new_fs_directory);
  rmdir (strcat (reinterpret_cast<char *>(buff5), "/sys/fs/cgroup"));

  char *buff6[strlen (s->new_fs_directory) + 1];
  strcpy (reinterpret_cast<char *>(buff6), s->new_fs_directory);
  rmdir (strcat (reinterpret_cast<char *>(buff6), "/sys/fs"));
  return 1;
}

/**
 * create new process
 * @param args struct with arguments
 * @return 1 if success
 */
int create_new_process (void *args) {
  void *stack = malloc (STACK_SIZE);
  if (stack == nullptr) {
    cerr << "system error: allocation failed\n";
    exit (1);
  }
  Argv *s = (Argv *) args;
  clone (run_child, (char *) stack + STACK_SIZE,
         CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS
         | SIGCHLD, args); // create and run child
  wait (nullptr);
  char *buff[strlen (s->new_fs_directory) + 1];
  strcpy (reinterpret_cast<char *>(buff), s->new_fs_directory);
  umount (strcat (reinterpret_cast<char *>(buff), "/proc"));
  delete_directories (args);
  return 1;
}

/**
 * main function
 * @param argc number of arguments
 * @param argv arguments
 * @return 1 if success, exit(1) otherwise
 */
int main (int argc, char *argv[]) {
  auto *argv_s = new Argv{};
  argv_s->new_hostname = argv[1];
  argv_s->new_fs_directory = argv[2];
  argv_s->num_of_processes = atoi (argv[3]);
  argv_s->program_to_run = argv[4];

// create file arguments
  char *program_args = (char *) (malloc (argc - 3));
  if (program_args == nullptr) {
    cerr << "system error: allocation failed\n";
    exit (1);
  }
  for (int i = 0; i < argc - 4; i++) {
    *(program_args + i) = *(argv[4 + i]);
  }
  *(program_args + argc - 4) = 0;
  argv_s->program_args = program_args;

// run main function
  create_new_process (argv_s);
  free (program_args);
  free (argv_s);

  return 1;
}


