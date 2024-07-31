#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>

#include "hrperf_control.h"

#define REGION_SIZE (4L * 1024 * 1024 * 1024)  // 4GB


void* scan_memory(void* arg) {
    hrperf_start();
    uint8_t* region = (uint8_t*)arg;
    for (size_t i = 0; i < REGION_SIZE; i++) {
        // Simple operation: read the memory
        uint8_t value = region[i];
        region[i] = (uint8_t)1;
    }
    hrperf_pause();
    return NULL;
}

int main() {
    // Allocate and zero-initialize 4GB region
    uint8_t* region = (uint8_t*)calloc(REGION_SIZE, sizeof(uint8_t));
    if (!region) {
        perror("Failed to allocate memory");
        return EXIT_FAILURE;
    }

    pthread_t thread;
    pthread_attr_t attr;
    cpu_set_t cpuset;

    // Initialize thread attributes
    pthread_attr_init(&attr);

    // Set CPU affinity to core 2
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset);
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);

    // Create the thread
    if (pthread_create(&thread, &attr, scan_memory, (void*)region) != 0) {
        perror("Failed to create thread");
        free(region);
        return EXIT_FAILURE;
    }

    // Wait for the thread to finish
    pthread_join(thread, NULL);

    // Clean up
    free(region);
    pthread_attr_destroy(&attr);

    printf("Memory scan completed.\n");
    return EXIT_SUCCESS;
}