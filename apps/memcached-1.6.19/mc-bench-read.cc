#include <arpa/inet.h>
#include <assert.h>
#include <atomic>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <numeric>
#include <random>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <getopt.h>

#include "proto.h"

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#define barrier()       asm volatile("" ::: "memory")

constexpr int kMaxBufLen = 2048;
constexpr int kMinValueLen = 4;
constexpr int kMaxValueLen = 1024;

using namespace std::chrono;

/* client-side stat */
struct cstat {
    double total_requests;
    double total_success;
    double total_time_us;
};

/* work unit structure */
struct work_unit {
    double start_us, duration_us;
    bool success;
    uint64_t timing;
    uint64_t idx;
    char *req;
    int req_len;
};

/* Global variables */
int threads = 1;
unsigned long raddr;
int experiment_duration = 10; // in seconds
int max_key_idx = 1000;
int slo = 1000; // in microseconds
bool verbose = false;
int port = 11211; // default memcached port

/* Function to generate random string */
void GenerateRandomString(char *buffer, int len, uint64_t hash) {
    int i;
    uint64_t tmp_hash = hash;

    for(i = 0; i < len; ++i) {
        buffer[i] = (tmp_hash % 94) + 33;
        tmp_hash = (tmp_hash >> 1);
    }
}

/* Function to create a TCP connection */
int TcpDial(unsigned long ip, uint16_t port) {
    int fd;
    struct sockaddr_in addr;

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Failed to create a socket\n");
        return -1;
    }
    bzero(&addr, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ip;
    addr.sin_port = htons(port);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr))) {
        fprintf(stderr, "Failed to connect: port = %u\n", port);
        return -1;
    }

    if (verbose) {
        std::cout << "Connected to server at " << inet_ntoa(addr.sin_addr) << " on port " << port << std::endl;
    }

    return fd;
}

/* Read full from TCP */
ssize_t TcpReadFull(int fd, void *buf, size_t len) {
    char *pos = reinterpret_cast<char *>(buf);
    size_t n = 0;
    while (n < len) {
        ssize_t ret = read(fd, pos + n, len - n);
        if (ret < 0) return ret;
        if (ret == 0) return n;
        n += ret;
    }
    assert(n == len);
    return n;
}

/* Write full to TCP */
ssize_t TcpWriteFull(int fd, const void *buf, size_t len) {
    const char *pos = reinterpret_cast<const char *>(buf);
    size_t n = 0;
    while (n < len) {
        ssize_t ret = send(fd, pos + n, len - n, 0);
        if (ret < 0) return ret;
        assert(ret > 0);
        n += ret;
    }
    assert(n == len);
    return n;
}

/* Function to pre-populate the memcached server */
void PrePopulateMemcached(int threads, int max_key_idx) {
    // For each key from 0 to max_key_idx, send a SET request
    std::vector<std::thread> th;
    for (int t = 0; t < threads; ++t) {
        th.emplace_back(std::thread([=] {
            int c = TcpDial(raddr, port);
            if (c < 0) {
                fprintf(stderr, "Cannot connect to server\n");
                return;
            }
            // Initialize random number generator for each thread
            std::mt19937 rg(time(NULL) + t);
            std::uniform_int_distribution<int> value_len_dist(kMinValueLen, kMaxValueLen);
            char value[kMaxValueLen];
            uint64_t total_data = 0;
            uint64_t data_limit = 10ULL * 1024 * 1024 * 1024; // 10GB
            for (int i = t; i < max_key_idx; i += threads) {
                std::string key = std::to_string(i);
                int value_len = value_len_dist(rg);
                GenerateRandomString(value, value_len, rg());
                char req[kMaxBufLen];
                int req_len = ConstructMemcachedSetReq(req, kMaxBufLen, 0, key.c_str(), key.length(), value, value_len);
                struct MemcachedHdr *hdr = reinterpret_cast<struct MemcachedHdr *>(req);
                hton(hdr);
                // Send SET request
                ssize_t ret = TcpWriteFull(c, req, req_len);
                if (ret < 0) {
                    fprintf(stderr, "Failed to send SET request for key %s\n", key.c_str());
                }
                // Read response (optional, can be omitted if not needed)
                char resp[4096];
                ret = TcpReadFull(c, resp, sizeof(MemcachedHdr));
                if (ret < 0) {
                    fprintf(stderr, "Failed to read response header for key %s\n", key.c_str());
                } else {
                    hdr = reinterpret_cast<struct MemcachedHdr *>(resp);
                    ntoh(hdr);
                    if (hdr->total_body_length > 0) {
                        ret = TcpReadFull(c, resp + sizeof(MemcachedHdr), hdr->total_body_length);
                        if (ret < 0) {
                            fprintf(stderr, "Failed to read response body for key %s\n", key.c_str());
                        }
                    }
                }
                total_data += value_len + key.length();
                if (total_data >= data_limit) {
                    break;
                }
            }
            close(c);
        }));
    }
    for (auto &t : th) t.join();
}

/* Worker function for each thread */
void ClientWorker(int id, cstat &stats) {
    srand(time(NULL) * (id+1));
    int c = TcpDial(raddr, port);
    if (c < 0) {
        fprintf(stderr, "Cannot connect to server\n");
        return;
    }

    // Prepare GET request template
    char req_template[kMaxBufLen];
    int req_len;
    std::string key = std::to_string(rand() % max_key_idx);
    req_len = ConstructMemcachedGetReq(req_template, kMaxBufLen, 0, key.c_str(), key.length());
    struct MemcachedHdr *hdr = reinterpret_cast<struct MemcachedHdr *>(req_template);
    hton(hdr);

    char resp[4096];
    ssize_t ret;

    auto start_time = high_resolution_clock::now();
    auto end_time = start_time + seconds(experiment_duration);

    uint64_t total_requests = 0;
    uint64_t total_success = 0;

    while (high_resolution_clock::now() < end_time) {
        // Generate a random key
        std::string key = std::to_string(rand() % max_key_idx);
        req_len = ConstructMemcachedGetReq(req_template, kMaxBufLen, 0, key.c_str(), key.length());
        hdr = reinterpret_cast<struct MemcachedHdr *>(req_template);
        hton(hdr);

        // Send GET request
        ret = TcpWriteFull(c, req_template, req_len);
        if (ret < 0) {
            fprintf(stderr, "Failed to send GET request for key %s\n", key.c_str());
            break;
        }
        total_requests++;

        // Read response header
        ret = TcpReadFull(c, resp, sizeof(MemcachedHdr));
        if (ret <= 0) {
            fprintf(stderr, "Failed to read response header\n");
            break;
        }
        hdr = reinterpret_cast<struct MemcachedHdr *>(resp);
        ntoh(hdr);

        // Read response body
        ret = TcpReadFull(c, resp + sizeof(MemcachedHdr), hdr->total_body_length);
        if (ret < 0) {
            fprintf(stderr, "Failed to read response body\n");
            break;
        }
        total_success++;

        // Here we could measure latency if needed
    }

    auto actual_end_time = high_resolution_clock::now();
    double elapsed_us = duration_cast<microseconds>(actual_end_time - start_time).count();

    stats.total_requests = total_requests;
    stats.total_success = total_success;
    stats.total_time_us = elapsed_us;

    close(c);
}

/* Main function */
int main(int argc, char *argv[]) {
    /* Parse command line arguments */
    int option_index = 0;
    int c;

    static struct option long_options[] = {
        {"threads", required_argument, 0, 't'},
        {"server", required_argument, 0, 's'},
        {"duration", required_argument, 0, 'd'},
        {"max-key", required_argument, 0, 'k'},
        {"slo", required_argument, 0, 'l'},
        {"verbose", no_argument, 0, 'v'},
        {"port", required_argument, 0, 'p'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "t:s:d:k:l:vp:h", long_options, &option_index)) != -1) {
        switch (c) {
            case 't':
                threads = atoi(optarg);
                break;
            case 's':
                raddr = inet_addr(optarg);
                break;
            case 'd':
                experiment_duration = atoi(optarg);
                break;
            case 'k':
                max_key_idx = atoi(optarg);
                break;
            case 'l':
                slo = atoi(optarg);
                break;
            case 'v':
                verbose = true;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'h':
            default:
                std::cout << "Usage: " << argv[0] << " [options]\n"
                          << "Options:\n"
                          << "  -t, --threads <num>      Number of worker threads (default: 1)\n"
                          << "  -s, --server <ip>        Memcached server IP address\n"
                          << "  -d, --duration <sec>     Experiment duration in seconds (default: 10)\n"
                          << "  -k, --max-key <num>      Maximum key index (default: 1000)\n"
                          << "  -l, --slo <us>           Service Level Objective in microseconds (default: 1000)\n"
                          << "  -p, --port <num>         Memcached server port (default: 11211)\n"
                          << "  -v, --verbose            Verbose output\n"
                          << "  -h, --help               Show this help message\n";
                exit(0);
        }
    }

    if (raddr == INADDR_NONE) {
        std::cerr << "Invalid server IP address\n";
        exit(EXIT_FAILURE);
    }

    std::cout << "Starting benchmark with the following parameters:\n"
              << "Threads: " << threads << "\n"
              << "Server IP: " << inet_ntoa(*(in_addr *)&raddr) << "\n"
              << "Server Port: " << port << "\n"
              << "Experiment Duration: " << experiment_duration << " seconds\n"
              << "Max Key Index: " << max_key_idx << "\n"
              << "SLO: " << slo << " us\n";

    // Pre-populate the memcached server
    std::cout << "Pre-populating memcached server with data...\n";
    PrePopulateMemcached(threads, max_key_idx);
    std::cout << "Pre-population complete.\n";

    // Launch worker threads
    std::vector<std::thread> worker_threads;
    std::vector<cstat> thread_stats(threads);

    for (int i = 0; i < threads; ++i) {
        worker_threads.emplace_back(std::thread(ClientWorker, i, std::ref(thread_stats[i])));
    }

    // Wait for threads to finish
    for (auto &t : worker_threads) t.join();

    // Aggregate statistics
    uint64_t total_requests = 0;
    uint64_t total_success = 0;
    double total_time_us = 0;

    for (int i = 0; i < threads; ++i) {
        total_requests += thread_stats[i].total_requests;
        total_success += thread_stats[i].total_success;
        total_time_us += thread_stats[i].total_time_us;
    }

    double avg_rps = (total_success / total_time_us) * 1e6;
    double avg_rps_per_thread = avg_rps / threads;

    // Output results
    std::cout << "Benchmark Results:\n";
    std::cout << "Total Requests Sent: " << total_requests << "\n";
    std::cout << "Total Successful Responses: " << total_success << "\n";
    std::cout << "Total Time: " << (total_time_us / 1e6) << " seconds\n";
    std::cout << "Average RPS (Total): " << avg_rps << "\n";
    std::cout << "Average RPS per Thread: " << avg_rps_per_thread << "\n";

    return 0;
}
