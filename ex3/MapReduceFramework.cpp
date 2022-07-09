#include <atomic>
#include "MapReduceFramework.h"
#include "pthread.h"
#include <mutex>
#include <iostream>
#include <algorithm>
#include "Barrier.cpp"
#include "Barrier.h"
#include "map"

using namespace std;

struct JobContext {
    const MapReduceClient *client{};
    pthread_t *threads{}; // threads list
    pthread_t *first_thread{}; // the first thread
    JobState *state = new JobState; // state of job
    pthread_mutex_t state_mutex{}; // mutex for update state
    pthread_mutex_t emit_mutex{}; // mutex for emit2 - emit_map
    pthread_mutex_t shuffle_mutex{}; // mutex for shuffle_map
    pthread_mutex_t output_mutex{}; // mutex for emit3 - OutputVec
    int multiThreadLevel{}; // num of threads
    bool is_wait = false; // boolean var represent wait for job
    const vector<InputPair> *InputVec{};
    vector<OutputPair> *OutputVec = new vector<OutputPair>;
    atomic<int> *emit_2_counter = new atomic<int> (0); // atomic counter
    atomic<int> *curr_state = new atomic<int> (UNDEFINED_STAGE); // atomic variable for curr state
    atomic<int> *emit_3_counter = new atomic<int> (0); // atomic counter
    atomic<int> *map_phase_counter = new atomic<int>; // atomic counter
    atomic<int> *reduce_phase_counter = new atomic<int> (0); // atomic counter
    atomic<float> *cur_percent = new atomic<float> (0);
    map<pthread_t, IntermediateVec *> emit_map = *(new map<pthread_t, IntermediateVec *>); // map for emit 2 func
    map<K2 *, IntermediateVec *> shuffle_map = *(new map<K2 *, IntermediateVec *>);
    Barrier *barrier{};
    atomic<float> *shuffle_map_len = new atomic<float> (0);
    atomic<int> *emit_map_len = new atomic<int> (0);
};

//void setJobState (JobHandle job, JobState *state);

/**
 * locks a given mutex
 * @param mutex
 */
void lock_mutex (pthread_mutex_t *mutex) {
  if (pthread_mutex_lock (mutex) != 0) {
    std::cerr << "system error: lock mutex error\n" << std::endl;
    exit (EXIT_FAILURE);
  }
}

/**
 * unlocks a given mutex
 * @param mutex
 */
void unlock_mutex (pthread_mutex_t *mutex) {
  if (pthread_mutex_unlock (mutex) != 0) {
    std::cerr << "system error: unlock mutex error\n" << std::endl;
    exit (EXIT_FAILURE);
  }
}

/**
 * init a given mutex
 * @param mutex
 */
void init_mutex (pthread_mutex_t *mutex) {
  if (pthread_mutex_init (mutex, nullptr) != 0) {
    std::cerr << "system error: init mutex error\n" << std::endl;
    exit (EXIT_FAILURE);
  }
}

/**
 * destroy a given mutex
 * @param mutex
 */
void destroy_mutex (pthread_mutex_t *mutex) {
  if (pthread_mutex_destroy (mutex) != 0) {
    std::cerr << "system error: destroy mutex error\n" << std::endl;
    exit (EXIT_FAILURE);
  }
}

void *runMapReduce (void *args) {

  auto jc = (JobContext *) args;
  int curr_index = (*(jc->map_phase_counter))++;
  while (curr_index < (int) jc->InputVec->size ()) {
    lock_mutex (&jc->state_mutex);
    jc->curr_state->store (MAP_STAGE);
    jc->cur_percent->store ((float) jc->emit_2_counter->load () * 100
                            / (float) jc->InputVec->size ());
    jc->state->stage = (stage_t) jc->curr_state->load ();
    jc->state->percentage = jc->cur_percent->load ();
    unlock_mutex (&jc->state_mutex);
    if (curr_index < (int) jc->InputVec->size ()) {
      InputPair pair = (*(jc->InputVec))[curr_index];
      jc->client->map (pair.first, pair.second, jc);
      curr_index = (*(jc->map_phase_counter))++;
      continue;
    }
    break;
  }

//    sort phase
  lock_mutex (&jc->emit_mutex);
  unsigned long tid = pthread_self ();
  IntermediateVec *curr_vec = jc->emit_map[tid];
  if (curr_vec != nullptr) {
    sort (curr_vec->begin (), curr_vec->end ());
  }
  unlock_mutex (&jc->emit_mutex);


//    barrier - wait until all threads end to run
  jc->barrier->barrier ();

  //  count num of pairs in emit_map
  if (pthread_equal (pthread_self (), *(jc->first_thread))) {
    for (auto map_elem: jc->emit_map) {
      if (map_elem.second != nullptr) {
        jc->emit_map_len->fetch_add ((int) map_elem.second->size ());
      }
    }
  }

  jc->barrier->barrier ();

//    shuffle phase
  if (pthread_equal (pthread_self (), *(jc->first_thread))) {
    float num_of_pairs = 0;
    lock_mutex (&jc->emit_mutex);
    lock_mutex (&jc->shuffle_mutex);
    lock_mutex (&jc->state_mutex);
    jc->curr_state->store (SHUFFLE_STAGE);
    jc->cur_percent->store (0);
    jc->state->stage = (stage_t) jc->curr_state->load ();
    jc->state->percentage = jc->cur_percent->load ();
    unlock_mutex (&jc->state_mutex);
    map<K2 *, IntermediateVec *> &shuffle_map = *(new map<K2 *, IntermediateVec *>);
    bool flag = false;
    for (auto map_elem: jc->emit_map) {
      if (map_elem.second != nullptr) {
        for (IntermediatePair pair: *map_elem.second) {
          flag = false;
          if (!shuffle_map.empty ()) {
            for (auto p: shuffle_map) {
              if (!(*p.first < *pair.first)
                  and !(*pair.first < *p.first)) { // if k in map
                shuffle_map[p.first]->push_back (pair);
                flag = true;
                break;
              }
            }
          }
          if (!flag) {
            auto *new_vec = new IntermediateVec;
            shuffle_map[pair.first] = new_vec;
            shuffle_map[pair.first]->push_back (pair);
          }
          lock_mutex (&jc->state_mutex);
          jc->curr_state->store (SHUFFLE_STAGE);
          jc->cur_percent->store (
              (++num_of_pairs) * 100 / (float) jc->emit_map_len->load ());
          jc->state->stage = (stage_t) jc->curr_state->load ();
          jc->state->percentage = 100;
          jc->state->percentage = jc->cur_percent->load ();
          unlock_mutex (&jc->state_mutex);
        }
      }
    }
    jc->shuffle_map = shuffle_map;
    jc->shuffle_map_len->store ((float) shuffle_map.size ());
    unlock_mutex (&jc->shuffle_mutex);
    unlock_mutex (&jc->emit_mutex);
  }

  jc->barrier->barrier ();

//    reduce phase
  while (!jc->shuffle_map.empty ()) {
    lock_mutex (&jc->shuffle_mutex);
    if (!jc->shuffle_map.empty ()) {
      auto vec = jc->shuffle_map[jc->shuffle_map.begin ()->first];
      jc->client->reduce (vec, jc);
      jc->reduce_phase_counter->fetch_add (1);
      lock_mutex (&jc->state_mutex);
      jc->curr_state->store (REDUCE_STAGE);
      jc->state->stage = (stage_t) jc->curr_state->load ();
      jc->cur_percent->store ((float) jc->emit_3_counter->load () * 100
                              / jc->shuffle_map_len->load ());
      jc->state->percentage = jc->cur_percent->load ();
      unlock_mutex (&jc->state_mutex);
      jc->shuffle_map.erase (jc->shuffle_map.begin ()->first);
      unlock_mutex (&jc->shuffle_mutex);
      continue;
    }
    unlock_mutex (&jc->shuffle_mutex);
    break;
  }

  return nullptr;
}

/**
 * starts running the MapReduce algorithm (with several threads)
 * @param client - The implementation of MapReduceClient or in other
 * words the task that the framework should run
 * @param inputVec – a vector of type std::vector<std::pair<K1*, V1*>>, the input elements.
 * @param outputVec – a vector of type std::vector<std::pair<K3*, V3*>>, to which the output elements will be added before returning.
 * @param multiThreadLevel – the number of worker threads to be used for running the algorithm.
 * @return - The function returns JobHandle that will be used for monitoring the job.
 */
JobHandle startMapReduceJob (const MapReduceClient &client,
                             const InputVec &inputVec, OutputVec &outputVec,
                             int multiThreadLevel) {

  auto *jc = new JobContext{};
  jc->threads = new pthread_t[multiThreadLevel];
  jc->client = &client;
  jc->state->stage = (stage_t) jc->curr_state->load ();
  jc->state->percentage = jc->cur_percent->load ();
  init_mutex (&jc->emit_mutex);
  init_mutex (&jc->output_mutex);
  init_mutex (&jc->shuffle_mutex);
  init_mutex (&jc->state_mutex);
  jc->multiThreadLevel = multiThreadLevel;
  jc->InputVec = &inputVec;
  jc->OutputVec = &outputVec;
  jc->map_phase_counter->store (0);
  jc->barrier = new Barrier (multiThreadLevel);
  for (int i = 0; i < multiThreadLevel; ++i) {
    if (pthread_create (jc->threads + i, nullptr, &runMapReduce, jc) != 0) {
      std::cerr << "system error: pthread_create error\n" << std::endl;
      exit (EXIT_FAILURE);
    }
    if (i == 0) { jc->first_thread = &jc->threads[0]; }
  }
  return static_cast<JobHandle> (jc);
}
/**
 * gets JobHandle returned by startMapReduceFramework and waits until it is finished.
 * @param job - job to wait until it is finish.
 */
void waitForJob (JobHandle job) {
  auto jc = (JobContext *) job;
  if (!jc->is_wait) {
    for (int i = 0; i < jc->multiThreadLevel; ++i) {
      if (pthread_join (jc->threads[i], nullptr) != 0) {
        std::cerr << "system error: pthread_join error\n" << endl;
        exit (EXIT_FAILURE);
      }
    }
    jc->is_wait = true;
  }
}

/**
 * this function gets a JobHandle and updates the state of the job into the given JobState struct.
 * @param job
 * @param state
 */
void getJobState (JobHandle job, JobState *state) {
  auto jc = (JobContext *) job;
  lock_mutex (&jc->state_mutex);
  state->stage = (stage_t) jc->curr_state->load ();
  state->percentage = jc->cur_percent->load ();
  unlock_mutex (&jc->state_mutex);
}

/**
 * Releasing all resources of a job.
 * @param job
 */
void closeJobHandle (JobHandle job) {
  auto jc = (JobContext *) job;
  waitForJob (job);
  destroy_mutex (&jc->emit_mutex);
  destroy_mutex (&jc->shuffle_mutex);
  destroy_mutex (&jc->output_mutex);
  destroy_mutex (&jc->state_mutex);
  for (auto elem: jc->emit_map) {
    delete elem.second;
  }
  for (auto elem: jc->shuffle_map) {
    delete elem.second;
  }
  delete[] jc->threads;
  delete jc->emit_2_counter;
  delete jc->emit_3_counter;
  delete jc->curr_state;
  delete jc->map_phase_counter;
  delete jc->reduce_phase_counter;
  delete jc->cur_percent;
  delete jc->emit_map_len;
  delete jc->shuffle_map_len;
  delete jc->barrier;
  delete jc->state;
  jc->threads = nullptr;
  delete jc;
}

/**
 * saves the intermediary element in the context data structures.
 * In addition, the function updates the number of intermediary elements
 * using atomic counter.
 * @param key
 * @param value
 * @param context - contains data structure of the thread that created
 *        the intermediary element.
 */
void emit2 (K2 *key, V2 *value, void *context) {
  auto jc = (JobContext *) context;
  lock_mutex (&(jc->emit_mutex));
  unsigned long tid = pthread_self ();
  if (jc->emit_map.find (tid) == jc->emit_map.end ()) { // if tid not in map
    jc->emit_map[tid] = new IntermediateVec;
    jc->emit_2_counter->fetch_add (1);
  }
  jc->emit_map[tid]->push_back (IntermediatePair (key, value));
  unlock_mutex (&(jc->emit_mutex));
}

/**
 * The function saves the output element in the context data structures (output vector).
 * In addition, the function updates the number of output elements using atomic counter.
 * @param key
 * @param value
 * @param context - contains data structure of the thread that created the output element
 */
void emit3 (K3 *key, V3 *value, void *context) {
  auto jc = (JobContext *) context;
  lock_mutex (&jc->output_mutex);
  jc->OutputVec->push_back (OutputPair (key, value));
  jc->emit_3_counter->fetch_add (1);
  unlock_mutex (&jc->output_mutex);
}
