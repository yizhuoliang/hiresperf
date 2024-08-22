#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <immintrin.h>

#include "hrperf_api.h"

#define NUM_ITERATIONS 1000000000 // Adjust this based on your CPU to get a significant load

void *heavy_computation(void *arg) {
    int core_id = *(int *)arg;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    // Pin the thread to a specified core
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    hrperf_start();

    // Initial sleep to cool down the CPU
    sleep(1);

    // First computation
    struct timespec start_time, end_time_first, end_time_second;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);

    volatile int result = 0;  // Prevent optimizing out the loop
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        result += i * i;
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &end_time_first);

    // Second computation immediately after the first
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        result += i * i;
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &end_time_second);

    hrperf_pause();

    // Calculate and print the elapsed times
    double elapsed_first = (end_time_first.tv_sec - start_time.tv_sec) +
                           (end_time_first.tv_nsec - start_time.tv_nsec) / 1e9;
    double elapsed_second = (end_time_second.tv_sec - end_time_first.tv_sec) +
                            (end_time_second.tv_nsec - end_time_first.tv_nsec) / 1e9;

    printf("Start time (seconds): %ld.%09ld\n", start_time.tv_sec, start_time.tv_nsec);
    printf("Elapsed time of first run (seconds): %.9f\n", elapsed_first);
    printf("Elapsed time of second run (seconds): %.9f\n", elapsed_second);

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <cpu_core_id>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int core_id = atoi(argv[1]);
    pthread_t thread;

    if (pthread_create(&thread, NULL, heavy_computation, &core_id)) {
        fprintf(stderr, "Error creating thread\n");
        return EXIT_FAILURE;
    }

    pthread_join(thread, NULL);
    return EXIT_SUCCESS;
}