/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "MapReduceFramework.h"
#include <iostream>
#include <unordered_map>
#include <map>
#include <vector>
#include <signal.h>
#include <sys/time.h>
#include <fstream>

#define TO_NANO(s, us) s*1000000000 + us*1000
#define CALCULATE_TIME(start, end) (end - start)
#define CHUNK_SIZE 10

using namespace std;

/*
 * the struct for the comparator.
 */
struct cmp
{
    bool operator()(k2Base* a, k2Base* b) const
    {
        return *a < *b;
    }
};

typedef pair<k2Base*, v2Base*> OUT_MAP_ITEM;
typedef vector<OUT_MAP_ITEM> OUT_MAP_ITEMS_LIST;
typedef pair<k2Base*, V2_LIST> OUT_SHUFFLE_ITEM;
typedef map<k2Base*, V2_LIST, cmp> OUT_SHUFFLE_ITEMS_LIST;

/*the queue for the given items*/
vector<IN_ITEM> itemsQueue;

/* the map containers foe each thread*/
unordered_map<pthread_t, OUT_MAP_ITEMS_LIST> mapContainers;

/*the output items of the suffle*/
OUT_SHUFFLE_ITEMS_LIST shuffledItemsMap;

/*the pairs to convert the map to vector*/
vector<OUT_SHUFFLE_ITEM> shuffledItemToVector;

/*the reduce containers for each pthread*/
unordered_map<pthread_t, OUT_ITEMS_LIST> reduceContainers;

/*the mutexes*/
pthread_mutex_t em_map_mutex;
pthread_mutex_t log_mutex;
pthread_mutex_t counter_map_threads_mutex;
pthread_mutex_t index_mutex;
unordered_map<pthread_t, pthread_mutex_t> mutexes_per_containers;
pthread_mutex_t rm_map_mutex;


int index;
int counterMapThreads;
pthread_cond_t cvShuffle;

/*was created to measure time for the log*/
char buffer[100];
time_t rawtime;
struct tm* timeInfo;
fstream log;

/**
 * Checks if the system call result succeed
 * @param res The result
 * @param functionName The function to check
 */
void checkResults(int res, string functionName){
    if(res != 0){
        cerr<< "MapReduceFramework Failure: " << functionName << " failed.";
        exit(1);
    }
}

/**
 * The start routine for the execMap threads.
 * call the map function from the user on each (k1,v1) of the input.
 * @param mapReduce object containing the map function from the user
 * @return void
 */
void* execMap(void* mapReduce){
    //mutex for the execMap threads
    checkResults(pthread_mutex_lock(&em_map_mutex), "pthread_mutex_lock");
    checkResults(pthread_mutex_unlock(&em_map_mutex), "pthread_mutex_unlock");
    while(true) {
        //no more elements in the vector
        if((int)(itemsQueue.size()) <= index){

            //mutex to reduce number of threads
            checkResults(pthread_mutex_lock(&counter_map_threads_mutex),
                         "pthread_mutex_lock");
            counterMapThreads--;
            checkResults(pthread_mutex_unlock(&counter_map_threads_mutex),
                         "pthread_mutex_unlock");

            //the log mutex
            checkResults(pthread_mutex_lock(&log_mutex), "pthread_mutex_lock");
            time(&rawtime);
            timeInfo = localtime(&rawtime);
            strftime(buffer, 100, "%d.%m.%Y %I:%M:%S", timeInfo);
            log<< "Thread ExecMap terminated ["<<buffer<<"]\n";
            checkResults(pthread_mutex_unlock(&log_mutex),
                         "pthread_mutex_unlock");

            pthread_exit(nullptr);
        }

        checkResults(pthread_mutex_lock(&index_mutex), "pthread_mutex_lock");
        int chunkSize = min((int)(itemsQueue.size()) - index, CHUNK_SIZE);
        int start = index;
        index += chunkSize;
        checkResults(pthread_mutex_unlock(&index_mutex),
                     "pthread_mutex_unlock");

        // Send each pair form the chunk to the map function
        MapReduceBase* map = (MapReduceBase*)mapReduce;
        for(int i = start; i < start + chunkSize; i++) {
            map->Map(itemsQueue[i].first, itemsQueue[i].second);
        }
    }
}

/**
 * This function adds a new (k2,v2) pair to the thread container
 * @param key
 * @param value
 */
void Emit2 (k2Base* key, v2Base* value) {

    OUT_MAP_ITEM item(key, value);

    //mutex for pthread container
    checkResults(pthread_mutex_lock(&mutexes_per_containers[pthread_self()]),
                 "pthread_mutex_lock");
    mapContainers[pthread_self()].push_back(item);
    if (mapContainers[pthread_self()].size() == CHUNK_SIZE) {
        checkResults(pthread_cond_signal(&cvShuffle), "pthread_cond_signal");
    }
    checkResults(pthread_mutex_unlock(&mutexes_per_containers[pthread_self()]),
                 "pthread_mutex_unlock");
}

/**
 * The start routine for the Shuffle thread.
 * collect all the v2 elements with the same k2.
 *
 * @param arg
 * @return  void
 */
void* shuffle(void* arg){
    (void) arg;
    struct timespec ts;
    struct timeval tv;
    int flagLastRound = 0;

    while(true){

        checkResults(gettimeofday(&tv, NULL), "gettimeofday");
        ts.tv_sec = tv.tv_sec;
        ts.tv_nsec = tv.tv_usec * 1000;
        ts.tv_nsec += 100;

        //mutex for the reduce number of threads
        checkResults(pthread_mutex_lock(&counter_map_threads_mutex),
                     "pthread_mutex_lock");
        // mutexCounterMapThreads released
        int res = pthread_cond_timedwait(&cvShuffle, &counter_map_threads_mutex,
                                         &ts);

        if(res != 0 && res != ETIMEDOUT){
            cerr<< "MapReduceFramework Failure: pthread_cond_timedwait failed.";
            perror("here is the error: ");
            exit(1);
        }

        //checks if no more threads exist. run last time to empty the containers.
        if (counterMapThreads == 0) {
            flagLastRound = 1;
        }
        checkResults(pthread_mutex_unlock(&counter_map_threads_mutex),
                     "pthread_mutex_unlock");
        //run through all the containers
        for(auto it = mapContainers.begin(); it != mapContainers.end(); ++it){
            //mutex for the thread container
            checkResults(pthread_mutex_lock(&mutexes_per_containers[it->first]),
                         "pthread_mutex_lock");
            while (it->second.size() > 0) {
                OUT_MAP_ITEM item = it->second.back();
                shuffledItemsMap[item.first].push_back(item.second);
                it->second.pop_back();
            }
            checkResults(pthread_mutex_unlock
                                 (&mutexes_per_containers[it->first]),
                         "pthread_mutex_unlock");
        }
        // no more threads, no more items in containers. kill the shuffle thread
        if (flagLastRound) {
            checkResults(pthread_mutex_lock(&log_mutex), "pthread_mutex_lock");
            time(&rawtime);
            timeInfo = localtime(&rawtime);
            strftime(buffer, 100, "%d.%m.%Y %I:%M:%S", timeInfo);
            log<< "Thread Shuffle terminated ["<<buffer<<"]\n";
            checkResults(pthread_mutex_unlock(&log_mutex),
                         "pthread_mutex_unlock");
            pthread_exit(nullptr);

        }
    }
}

/**
 * The start routine for the ExecReduce thread.
 * call the reduce function from the user on each (k2,v2List) pair.
 * @param mapReduce
 * @return void
 */
void* execReduce(void* mapReduce){
    checkResults(pthread_mutex_lock(&rm_map_mutex), "pthread_mutex_lock");
    checkResults(pthread_mutex_unlock(&rm_map_mutex), "pthread_mutex_unlock");

    while(true) {
        //no more elements in the vector
        if((int)(shuffledItemToVector.size()) == index){

            //mutex log
            checkResults(pthread_mutex_lock(&log_mutex), "pthread_mutex_lock");
            time(&rawtime);
            timeInfo = localtime(&rawtime);
            strftime(buffer, 100, "%d.%m.%Y %I:%M:%S", timeInfo);
            log<< "Thread ExecReduce terminated ["<<buffer<<"]\n";
            checkResults(pthread_mutex_unlock(&log_mutex),
                         "pthread_mutex_unlock");

            pthread_exit(nullptr);
        }

        checkResults(pthread_mutex_lock(&index_mutex), "pthread_mutex_lock");
        int chunkSize = min((int)(shuffledItemToVector.size()) - index,
                            CHUNK_SIZE);
        int start = index;
        index += chunkSize;
        checkResults(pthread_mutex_unlock(&index_mutex), "pthread_mutex_unlock");

        // Send each pair form the chunk to the reduce function
        MapReduceBase* map = (MapReduceBase*)mapReduce;
        for(int i = start; i < start + chunkSize; i++) {
            map->Reduce(shuffledItemToVector[i].first,
                        shuffledItemToVector[i].second);
        }
    }
}
/**
 * Add a new (k3,v3) pair to the execReduce thread container
 * @param key
 * @param value
 */
void Emit3 (k3Base* key, v3Base* value) {
    OUT_ITEM item(key, value);
    reduceContainers[pthread_self()].push_back(item);
}

/**
 * comparator for sorting the results of the reduce stage.
 * @param item1
 * @param item2
 * @return true if item1 < item2
 */
bool comparator(OUT_ITEM item1, OUT_ITEM item2) {
    return *(item1.first) < *(item2.first);
}

/**
 * Run the mapReduceFramework with the given map and reduce functions and
 * itemsList.
 * creates multiThreadsLevel execMap threads, the shuffle thread and
 * multiThreadsLevel execMapReduce threads.
 *
 * @param mapReduce
 * @param itemsList
 * @param multiThreadLevel
 * @return sorted OUT_ITEMS_LIST
 */
OUT_ITEMS_LIST runMapReduceFramework(MapReduceBase& mapReduce,
                                     IN_ITEMS_LIST& itemsList,
                                     int multiThreadLevel) {

    //time to measure for the log file
    struct timeval t1;
    struct timeval t2;
    checkResults(gettimeofday(&t1, NULL), "gettimeofday");

    // start log
    log.open(".MapReduceFramework.log", fstream::app | fstream::out);
    log<< "runMapReduceFramework started with " << multiThreadLevel <<
            " threads\n";

    /*initailize mutexes*/
    checkResults(pthread_mutex_init(&em_map_mutex, NULL), "pthread_mutex_init");
    checkResults(pthread_mutex_init(&log_mutex, NULL), "pthread_mutex_init");
    checkResults(pthread_mutex_init(&counter_map_threads_mutex, NULL),
                 "pthread_mutex_init");
    checkResults(pthread_mutex_init(&index_mutex, NULL), "pthread_mutex_init");
    checkResults(pthread_mutex_init(&rm_map_mutex, NULL), "pthread_mutex_init");
    checkResults(pthread_cond_init(&cvShuffle, NULL), "pthread_cond_init");

    // -------------------------- MAP STAGE ----------------------------

    // Convert itemsList input to vector
    itemsQueue.reserve(itemsList.size());
    copy(itemsList.begin(), itemsList.end(), back_inserter(itemsQueue));

    /*an array list of the execMap threads*/
    pthread_t execMapThreads[multiThreadLevel];
    /*the shuffle thread*/
    pthread_t shuffleThread;

    index = 0;
    counterMapThreads = multiThreadLevel; // How much active threads left


    //lock the execMap threads
    checkResults(pthread_mutex_lock(&em_map_mutex), "pthread_mutex_lock");
    //create the execMap threads
    for(int i = 0; i < multiThreadLevel; i++){
        checkResults(pthread_create(&execMapThreads[i], NULL, execMap,
                                    (void*)&mapReduce), "pthread_create");

        time(&rawtime);
        timeInfo = localtime(&rawtime);
        strftime(buffer, 100, "%d.%m.%Y %I:%M:%S", timeInfo);
        log<< "Thread ExecMap created ["<<buffer<<"]\n";
    }

    for (int j = 0; j < multiThreadLevel; ++j) {
        mapContainers[execMapThreads[j]] = OUT_MAP_ITEMS_LIST();
        checkResults(pthread_mutex_init
                             (&mutexes_per_containers[execMapThreads[j]], NULL),
                     "pthread_mutex_init");
    }

    checkResults(pthread_mutex_unlock(&em_map_mutex), "pthread_mutex_unlock");
    //create the shuffle
    checkResults(pthread_create(&shuffleThread, NULL, shuffle, NULL),
                 "pthread_create");

    //log mutex
    checkResults(pthread_mutex_lock(&log_mutex), "pthread_mutex_lock");
    time(&rawtime);
    timeInfo = localtime(&rawtime);
    strftime(buffer, 100, "%d.%m.%Y %I:%M:%S", timeInfo);
    log<< "Thread Shuffle created ["<<buffer<<"]\n";
    checkResults(pthread_mutex_unlock(&log_mutex), "pthread_mutex_unlock");

    //waiting for the execMap
    for (int i = 0; i < multiThreadLevel; i++) {
        checkResults(pthread_join(execMapThreads[i], NULL), "pthread_join");
    }

    checkResults(pthread_join(shuffleThread, NULL), "pthread_join");

    checkResults(gettimeofday(&t2, NULL), "gettimeofday");
    long s1 = CALCULATE_TIME(t1.tv_sec, t2.tv_sec);
    long us1 = CALCULATE_TIME(t1.tv_usec, t2.tv_usec);

    // -------------------------- REDUCE STAGE ----------------------------
    struct timeval t3;
    struct timeval t4;
    checkResults(gettimeofday(&t3, NULL), "gettimeofday");
    /*create the reduce threads*/
    pthread_t execReduceThreads[multiThreadLevel];

    for (auto it = shuffledItemsMap.begin();
         it != shuffledItemsMap.end(); ++it) {
        OUT_SHUFFLE_ITEM item(it->first, it->second);
        shuffledItemToVector.push_back(item);
    }

    index = 0;
    checkResults(pthread_mutex_lock(&rm_map_mutex), "pthread_mutex_lock");

    /*create the reduce threads*/
    for(int i = 0; i < multiThreadLevel; i++){
        checkResults(pthread_create(&execReduceThreads[i], NULL, execReduce,
                                    (void*)&mapReduce), "pthread_create");

        time(&rawtime);
        timeInfo = localtime(&rawtime);
        strftime(buffer, 100, "%d.%m.%Y %I:%M:%S", timeInfo);
        log<< "Thread ExecReduce created ["<<buffer<<"]\n";
    }

    for (int k = 0; k < multiThreadLevel; ++k) {
        reduceContainers[execReduceThreads[k]] = OUT_ITEMS_LIST();
    }
    checkResults(pthread_mutex_unlock(&rm_map_mutex), "pthread_mutex_unlock");

    //wait for all the reduce threads to exit
    for (int i = 0; i < multiThreadLevel; i++) {
        checkResults(pthread_join(execReduceThreads[i], NULL), "pthread_join");
    }

    // ---------------------------- FINAL STAGE -----------------------------
    OUT_ITEMS_LIST outItems;
    for (auto it = reduceContainers.begin();
         it != reduceContainers.end(); ++it) {
        outItems.merge(it->second);
    }

    outItems.sort(comparator);

    checkResults(pthread_mutex_destroy(&em_map_mutex), "pthread_mutex_destroy");
    checkResults(pthread_mutex_destroy(&log_mutex), "pthread_mutex_destroy");
    checkResults(pthread_mutex_destroy(&counter_map_threads_mutex),
                 "pthread_mutex_destroy");
    checkResults(pthread_mutex_destroy(&index_mutex), "pthread_mutex_destroy");
    checkResults(pthread_mutex_destroy(&rm_map_mutex), "pthread_mutex_destroy");

    for(auto it = mutexes_per_containers.begin();
        it != mutexes_per_containers.end(); ++it){
        checkResults(pthread_mutex_destroy(&(it->second)),
                     "pthread_mutex_destroy");
    }
    itemsQueue.clear();
    mapContainers.clear();
    shuffledItemsMap.clear();
    shuffledItemToVector.clear();
    reduceContainers.clear();

    checkResults(gettimeofday(&t4, NULL), "gettimeofday");
    long s2 = CALCULATE_TIME(t3.tv_sec, t4.tv_sec);
    long us2 = CALCULATE_TIME(t3.tv_usec, t4.tv_usec);

    log<< "Map and Shuffle took " << TO_NANO(s1, us1) << " ns\n";
    log<< "Reduce took " << TO_NANO(s2, us2) << " ns\n";
    log<<"runMapReduceFramework finished\n";

    log.close();

    return outItems;
}
