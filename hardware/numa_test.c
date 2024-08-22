#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include <immintrin.h>
#include <numa.h>
#include <numaif.h>
#include <errno.h>

#define MB_SIZE ((uint64_t)1024 * 1024 * 1024) // 1GB per thread
#define NUM_THREADS 1 // Adjust the number of threads if necessary

typedef struct {
    int thread_id;
    char *buffer;
    size_t size;
    int node;
} ThreadData;

void check_numa_error(int err, const char *message) {
    if (err < 0) {
        fprintf(stderr, "Error: %s - %s\n", message, strerror(errno));
    }
}

void *memory_read_test(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    struct timespec start, end;
    double elapsed_time;
    size_t i;
    __m256i chunk;

    // Set CPU affinity to the node's CPUs
    struct bitmask *bm = numa_allocate_cpumask();
    if (!bm || numa_node_to_cpus(data->node, bm) != 0) {
        fprintf(stderr, "Failed to allocate or set CPU mask for node %d\n", data->node);
        return NULL;
    }

    if (numa_sched_setaffinity(0, bm) < 0) {
        check_numa_error(-1, "numa_sched_setaffinity failed");
        numa_free_cpumask(bm);
        return NULL;
    }
    numa_free_cpumask(bm);

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0; i < data->size; i += 32) {
        chunk = _mm256_load_si256((__m256i *)(data->buffer + i));
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double bandwidth = (double)data->size / (1024 * 1024 * 1024) / elapsed_time;

    printf("Thread %d on node %d: Read %zu bytes in %.6f seconds. Bandwidth: %.6f GB/s\n",
           data->thread_id, data->node, data->size, elapsed_time, bandwidth);

    return NULL;
}

int run_tests(int node, int target_node) {
    pthread_t threads[NUM_THREADS];
    ThreadData thread_data[NUM_THREADS];
    int i;

    // Allocate memory on the specified node
    numa_set_preferred(node);
    char *buffer = numa_alloc_onnode(NUM_THREADS * MB_SIZE, node);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory on node %d\n", node);
        return EXIT_FAILURE;
    }
    memset(buffer, 1, NUM_THREADS * MB_SIZE);

    // Create and run threads
    for (i = 0; i < NUM_THREADS; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].buffer = buffer + i * MB_SIZE;
        thread_data[i].size = MB_SIZE;
        thread_data[i].node = target_node;
        if (pthread_create(&threads[i], NULL, memory_read_test, &thread_data[i])) {
            fprintf(stderr, "Error creating thread %d\n", i);
            numa_free(buffer, NUM_THREADS * MB_SIZE);
            return EXIT_FAILURE;
        }
    }

    // Wait for all threads to complete
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    numa_free(buffer, NUM_THREADS * MB_SIZE);
    return EXIT_SUCCESS;
}

int main() {
    if (numa_available() == -1) {
        fprintf(stderr, "NUMA is not available on this system.\n");
        return EXIT_FAILURE;
    }

    printf("Testing local memory access on node 0...\n");
    run_tests(0, 0);

    printf("Testing remote memory access from node 0 to node 1...\n");
    run_tests(1, 0);

    printf("Testing local memory access on node 1...\n");
    run_tests(1, 1);

    printf("Testing remote memory access from node 1 to node 0...\n");
    run_tests(0, 1);

    return EXIT_SUCCESS;
}