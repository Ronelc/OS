
#include "osm.h"
#include <sys/time.h>
#include <math.h>
#include <iostream>

#define FAILURE -1

/* Calculate the time.
   */
double time_converting (timeval before, timeval after) {
  double start = (after.tv_usec - before.tv_usec) * pow (10, 3);
  double end = (after.tv_sec - before.tv_sec) * pow (10, 9);
  return start + end;
}

/* Time measurement function for a simple arithmetic operation.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_operation_time (unsigned int iterations) {
  if (iterations == 0) {
    return FAILURE;
  }
  int x_1 = 1, x_2 = 2, x_3 = 3, x_4 = 4, x_5 = 5, x_6 = 6,
  x_7 = 7, x_8 = 8, x_9 = 9, x_10 = 10;
  //initialize time
  struct timeval before{}, after{};
  int time = gettimeofday (&before, nullptr);
  if (time == FAILURE) { return FAILURE; }
  //forloop
  unsigned int i = 0;
  while (i < iterations) {
    x_1 += x_2;
    x_2 += x_3;
    x_3 += x_4;
    x_4 += x_5;
    x_5 += x_6;
    x_7 += x_8;
    x_8 += x_9;
    x_9 += x_10;
    x_10 += 1;
    i += 10;

  }
  //calculate time
  int time2 = gettimeofday (&after, nullptr);
  if (time2 == FAILURE) { return FAILURE; }
  return time_converting (before, after) / iterations;
}

/* Empty function */
void empty_function () { return; }

/* Time measurement function for an empty function call.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_function_time (unsigned int iterations) {
  if (iterations == 0) {
    return FAILURE;
  }
  //initialize time
  timeval before, after;
  int time = gettimeofday (&before, nullptr);
  if (time == FAILURE) { return FAILURE; }
  //forloop
  unsigned int i = 0;
  while (i < iterations) {
    empty_function ();
    empty_function ();
    empty_function ();
    empty_function ();
    empty_function ();
    empty_function ();
    empty_function ();
    empty_function ();
    empty_function ();
    empty_function ();
    i += 10;
  }

  //calculate time
  int time2 = gettimeofday (&after, nullptr);
  if (time2 == FAILURE) { return FAILURE; }
  return time_converting (before, after) / iterations;
}

/* Time measurement function for an empty trap into the operating system.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_syscall_time (unsigned int iterations) {
  if (iterations == 0) {
    return FAILURE;
  }
  //initialize time
  timeval before, after;
  int time = gettimeofday (&before, nullptr);
  if (time == FAILURE) { return FAILURE; }
  //forloop
  unsigned int i = 0;
  while (i < iterations) {
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    OSM_NULLSYSCALL;
    i += 10;
  }
  //calculate time
  int time2 = gettimeofday (&after, nullptr);
  if (time2 == FAILURE) { return FAILURE; }
  return time_converting (before, after) / iterations;
}



