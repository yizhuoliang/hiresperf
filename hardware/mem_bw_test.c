#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include <immintrin.h> // Include for AVX intrinsics

#include "hrperf_api.h"
#define MB_SIZE ((uint64_t)1024 * 1024 * 1024) // 1GB per thread
#define NUM_THREADS 30 // Number of threads used for the test

typedef struct {
    int thread_id;
    char *buffer;
    size_t size;
} ThreadData;

void *memory_read_test(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    struct timespec start, end;
    double elapsed_time;
    size_t i;
    __m256i chunk; // Use AVX 256-bit wide type

    clock_gettime(CLOCK_MONOTONIC, &start); // Record start time

    // Sequentially read data in chunks of 256 bits (32 bytes)
    for (i = 0; i < data->size; i += 32) {
        chunk = _mm256_load_si256((__m256i *)(data->buffer + i));
    }

    clock_gettime(CLOCK_MONOTONIC, &end); // Record end time

    // Calculate elapsed time in seconds
    elapsed_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double bandwidth_gb_s = (double)data->size / (1024 * 1024 * 1024) / elapsed_time;

    printf("Thread %d: Read %zu bytes in %f seconds. Bandwidth: %f GB/s\n",
           data->thread_id, data->size, elapsed_time, bandwidth_gb_s);

    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    int i;
    char *buffer = aligned_alloc(32, NUM_THREADS * (uint64_t)MB_SIZE); // Use aligned memory for AVX

    if (buffer == NULL) {
        fprintf(stderr, "Failed to allocate memory.\n");
        return EXIT_FAILURE;
    }

    memset(buffer, 1, NUM_THREADS * (uint64_t)MB_SIZE); // Initialize the buffer

    //hrperf_start();

    // Create threads
    for (i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].buffer = buffer + (i * MB_SIZE);
        thread_data[i].size = MB_SIZE;
        if (pthread_create(&threads[i], NULL, memory_read_test, &thread_data[i])) {
            fprintf(stderr, "Error creating thread\n");
            free(buffer);
            return EXIT_FAILURE;
        }
    }

    // Wait for threads to complete
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    //hrperf_pause();

    free(buffer); // Free allocated memory
    return EXIT_SUCCESS;
}