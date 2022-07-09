#ifndef MAPREDUCEFRAMEWORK_H
#define MAPREDUCEFRAMEWORK_H

#include "MapReduceClient.h"

/**
 * an identifier of a running job.
 * Returned when starting a job and used by other framework functions
 */
typedef void *JobHandle;

enum stage_t {
    UNDEFINED_STAGE = 0, MAP_STAGE = 1, SHUFFLE_STAGE = 2, REDUCE_STAGE = 3
};

/**
 *  a struct which quantizes the state of a job.
 *  stage – an enum (0-Undefined, 1-Map, 2-Shuffle, 3-Reduce)
 *  percentage – job progress of current stage
 */
typedef struct {
    stage_t stage;
    float percentage;
} JobState;

void emit2 (K2 *key, V2 *value, void *context);
void emit3 (K3 *key, V3 *value, void *context);

JobHandle startMapReduceJob (const MapReduceClient &client,
                             const InputVec &inputVec, OutputVec &outputVec,
                             int multiThreadLevel);

void waitForJob (JobHandle job);
void getJobState (JobHandle job, JobState *state);
void closeJobHandle (JobHandle job);

#endif //MAPREDUCEFRAMEWORK_H
