#pragma once

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef unsigned int vid_t;
typedef uint64_t eid_t;  // 64-bit edge offsets (upstream: unsigned int)

typedef struct {
long n;
long m;

vid_t *adj;
eid_t *num_edges;
} graph_t;

int free_graph(graph_t *g) {
if (g->num_edges != NULL) free(g->num_edges);

if (g->adj != NULL) free(g->adj);

return 0;
}

/*********************** READ INPUT FILE
* ************************************************************/
int vid_compare(const void *a, const void *b) {
return (*(vid_t *)a - *(vid_t *)b);
}

// .bin layout (CSR): uint64 n, uint64 m, uint64 sizes, uint64 offsets[n+1], uint32 edges[m].
// Adjacency lists are stored contiguously and already sorted, so the arrays are copied as-is (in parallel).
int load_graph_from_binary(char *filename, graph_t *g) {
g->num_edges = NULL;
g->adj = NULL;

int fd = open(filename, O_RDONLY);
if (fd == -1) {
    fprintf(stderr, "Error: could not open input file: %s.\n Exiting ...\n", filename);
    return -1;
}

struct stat sb;
if (fstat(fd, &sb) == -1) {
    fprintf(stderr, "Error: unable to acquire file stat\n");
    close(fd);
    return -1;
}
size_t len = sb.st_size;

char *data = (char *)mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
if (data == MAP_FAILED) {
    fprintf(stderr, "Error: mmap failed\n");
    close(fd);
    return -1;
}

const uint64_t *header = (const uint64_t *)data;
g->n = (long)header[0];
g->m = (long)header[1];  // total count of edges in both directions
printf("n: %ld, m: %ld\n", g->n, g->m);

const uint64_t *offsets = header + 3;
const vid_t *edges = (const vid_t *)(offsets + g->n + 1);
size_t expected = 3 * sizeof(uint64_t) + (size_t)(g->n + 1) * sizeof(uint64_t) + (size_t)g->m * sizeof(vid_t);
if (len < expected || offsets[g->n] != (uint64_t)g->m) {
    fprintf(stderr, "Error: malformed graph file: %s\n", filename);
    munmap(data, len);
    close(fd);
    return -1;
}

g->num_edges = (eid_t *)malloc((g->n + 1) * sizeof(eid_t));
g->adj = (vid_t *)malloc(g->m * sizeof(vid_t));
if (g->num_edges == NULL || g->adj == NULL) {
    fprintf(stderr, "Error: could not allocate memory for the graph\n");
    free_graph(g);
    munmap(data, len);
    close(fd);
    return -1;
}

#pragma omp parallel
{
#pragma omp for schedule(static) nowait
    for (long i = 0; i <= g->n; i++) g->num_edges[i] = offsets[i];
#pragma omp for schedule(static)
    for (long i = 0; i < g->m; i++) g->adj[i] = edges[i];
}

munmap(data, len);
close(fd);
return 0;
}

/************************************************************************************************/
/********************************   PARALLEL k-core
* ********************************************/
/************************************************************************************************/

void scan(long n, int *deg, int level, vid_t *curr, long *currTailPtr) {
// Size of cache line
const long BUFFER_SIZE_BYTES = 2048;
const long BUFFER_SIZE = BUFFER_SIZE_BYTES / sizeof(vid_t);

vid_t buff[BUFFER_SIZE];
long index = 0;

#pragma omp for schedule(static)
for (long i = 0; i < n; i++) {
    if (deg[i] == level) {
    buff[index] = i;
    index++;

    if (index >= BUFFER_SIZE) {
        long tempIdx = __sync_fetch_and_add(currTailPtr, BUFFER_SIZE);

        for (long j = 0; j < BUFFER_SIZE; j++) {
        curr[tempIdx + j] = buff[j];
        }
        index = 0;
    }
    }
}

if (index > 0) {
    long tempIdx = __sync_fetch_and_add(currTailPtr, index);

    for (long j = 0; j < index; j++) curr[tempIdx + j] = buff[j];
}

#pragma omp barrier
}

void processSubLevel(graph_t *g, vid_t *curr, long currTail, int *deg,
                    int level, vid_t *next, long *nextTailPtr) {
// Size of cache line
const long BUFFER_SIZE_BYTES = 2048;
const long BUFFER_SIZE = BUFFER_SIZE_BYTES / sizeof(vid_t);

vid_t buff[BUFFER_SIZE];
long index = 0;

#pragma omp for schedule(static)
for (long i = 0; i < currTail; i++) {
    vid_t v = curr[i];

    // Check the adj list of vertex v
    for (eid_t j = g->num_edges[v]; j < g->num_edges[v + 1]; j++) {
    vid_t u = g->adj[j];
    int deg_u = deg[u];

    if (deg_u > level) {
        int du = __sync_fetch_and_sub(&deg[u], 1);

        if (du == (level + 1)) {
        buff[index] = u;
        index++;

        if (index >= BUFFER_SIZE) {
            long tempIdx = __sync_fetch_and_add(nextTailPtr, BUFFER_SIZE);

            for (long bufIdx = 0; bufIdx < BUFFER_SIZE; bufIdx++)
            next[tempIdx + bufIdx] = buff[bufIdx];
            index = 0;
        }
        }
    }
    }
}

if (index > 0) {
    long tempIdx = __sync_fetch_and_add(nextTailPtr, index);
    ;
    for (long bufIdx = 0; bufIdx < index; bufIdx++)
    next[tempIdx + bufIdx] = buff[bufIdx];
}

#pragma omp barrier

#pragma omp for schedule(static)
for (long i = 0; i < *nextTailPtr; i++) {
    vid_t u = next[i];
    if (deg[u] != level) deg[u] = level;
}

#pragma omp barrier
}

// ParK to compute k core decompsitions in parallel
int *ParK(graph_t *g) {
int *deg = (int *)malloc(g->n * sizeof(int));
assert(deg != NULL);
long n = g->n;

vid_t *curr = (vid_t *)malloc(n * sizeof(vid_t));
assert(curr != NULL);

vid_t *next = (vid_t *)malloc(n * sizeof(vid_t));
assert(next != NULL);

long currTail = 0;
long nextTail = 0;

#pragma omp parallel
{
    int tid = omp_get_thread_num();
    long todo = n;
    int level = 0;

#pragma omp for schedule(static)
    for (long i = 0; i < n; i++) {
    deg[i] = (g->num_edges[i + 1] - g->num_edges[i]);
    }

    while (todo > 0) {
    scan(n, deg, level, curr, &currTail);
    //   printf("tid: %d, level: %d, currTail: %ld\n", tid, level, currTail);
    //   printf("todo: %ld\n", todo);
    while (currTail > 0) {
        todo = todo - currTail;

        processSubLevel(g, curr, currTail, deg, level, next, &nextTail);

        if (tid == 0) {
        vid_t *tempCurr = curr;
        curr = next;
        next = tempCurr;

        currTail = nextTail;
        nextTail = 0;
        }

#pragma omp barrier
    }

    level = level + 1;

#pragma omp barrier
    }
}

free(curr);
free(next);
return deg;
}
