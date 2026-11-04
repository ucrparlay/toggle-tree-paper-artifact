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


#define frac 0.98

// This value controls per-thread storage. Increase if the code fails assert
// in PKC and PKC_org
// NUM_THREADS is the minimum value that ensures the algorithm is correct
#define BUFF_MULT_FACTOR NUM_THREADS

// This can be set to 0 once you make sure there's adequate memory for your
// use-case
#define BUFF_ACCESS_ASSERTS 1

// Number of threads used
int NUM_THREADS = 1;

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

void read_env() {
#pragma omp parallel
{
#pragma omp master
    NUM_THREADS = omp_get_num_threads();
}
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

// Each thread has its own queue -- parallel PKC_org for k-core decomposition
int *PKC_org(graph_t *g) {
int *deg = (int *)malloc(g->n * sizeof(int));
assert(deg != NULL);
long n = g->n;
long visited = 0;

#pragma omp parallel
{
    int level = 0;

    long buff_size = (BUFF_MULT_FACTOR * n) / NUM_THREADS + 1;

    vid_t *buff = (vid_t *)malloc(buff_size * sizeof(vid_t));
    assert(buff != NULL);

    long start = 0, end = 0;

#pragma omp for schedule(static)
    for (long i = 0; i < n; i++) {
    deg[i] = (g->num_edges[i + 1] - g->num_edges[i]);
    }

    while (visited < g->n) {
#pragma omp for schedule(static)
    for (long i = 0; i < n; i++) {
        if (deg[i] == level) {
#if BUFF_ACCESS_ASSERTS
        assert(end < buff_size);
#endif
        buff[end] = i;
        end++;
        }
    }

    // Get work from curr queue and also add work after the current size
    while (start < end) {
        vid_t v = buff[start];
        start++;

        for (eid_t j = g->num_edges[v]; j < g->num_edges[v + 1]; j++) {
        vid_t u = g->adj[j];

        int deg_u = deg[u];

        if (deg_u > level) {
            int du = __sync_fetch_and_sub(&deg[u], 1);

            if (du == (level + 1)) {
#if BUFF_ACCESS_ASSERTS
            assert(end < buff_size);
#endif
            buff[end] = u;
            end++;
            // __sync_fetch_and_add(&end, 1);
            }

            if (du <= level) {
            __sync_fetch_and_add(&deg[u], 1);
            }
        }  // deg_u > level

        }  // visit adjacencies
    }  // end of while loop
    //   printf("end: %ld\n", end);
    __sync_fetch_and_add(&visited, end);

#pragma omp barrier
    start = 0;
    end = 0;
    level = level + 1;
    // printf("level: %d\n", level);
    }  // end of #visited < n

    free(buff);

}  // #end of parallel region
return deg;
}

// PKC -- Make a graph with remaining vertices, parallel k-core decomposition
int *PKC(graph_t *g) {
int *deg = (int *)malloc(g->n * sizeof(int));
assert(deg != NULL);
long n = g->n;
long reduceN = 0;
long Size = 0;
long visited = 0;

int *newDeg = NULL;
unsigned int *mapIndexToVtx = NULL;
vid_t *vertexToIndex = (vid_t *)malloc(n * sizeof(vid_t));
eid_t cumNumEdges[NUM_THREADS];
int part = 0;  // Thanks to J. Shi for reporting this bug
graph_t g_small;
g_small.num_edges = NULL;
g_small.adj = NULL;

#pragma omp parallel
{
    int level = 0;
    int tid = omp_get_thread_num();

    // resize if necessary
    long buff_size = (BUFF_MULT_FACTOR * n) / NUM_THREADS + 1;
    vid_t *buff = (vid_t *)malloc(buff_size * sizeof(vid_t));
    assert(buff != NULL);

    long start = 0, end = 0;
    int useSmallQ = 0;

#pragma omp for schedule(static)
    for (long i = 0; i < n; i++) {
    deg[i] = (g->num_edges[i + 1] - g->num_edges[i]);
    }

    while (visited < n) {
    //   printf("visited: %ld\n", visited);
    if ((useSmallQ == 0) && (visited >= (long)(n * frac))) {
        useSmallQ = 1;
        if (tid == 0) {
        reduceN = n - visited;
        newDeg = (int *)malloc(reduceN * sizeof(int));
        mapIndexToVtx =
            (unsigned int *)malloc(reduceN * sizeof(unsigned int));
        g_small.n = reduceN;
        g_small.num_edges = (eid_t *)malloc((reduceN + 1) * sizeof(eid_t));
        g_small.num_edges[0] = 0;

        part = reduceN / NUM_THREADS;
        }
#pragma omp barrier

#pragma omp for schedule(static)
        for (long i = 0; i < n; i++) {
        if (deg[i] >= level) {
#if BUFF_ACCESS_ASSERTS
            assert(end < buff_size);
#endif
            buff[end] = i;
            end++;
        }
        }

        // Now add them atomically
        long begin = __sync_fetch_and_add(&Size, end);

        for (long i = 0; i < end; i++) {
        newDeg[begin + i] = deg[buff[i]];
        mapIndexToVtx[begin + i] = buff[i];
        vertexToIndex[buff[i]] = begin + i;
        }

        end = 0;
#pragma omp barrier

        // Make a graph with reduceN vertices
        eid_t edgeCount = 0;

        if (tid == NUM_THREADS - 1) {
        for (long i = tid * part; i < Size; i++) {
            unsigned int v = mapIndexToVtx[i];
            eid_t prevEdgeCount = edgeCount;
            for (eid_t j = g->num_edges[v]; j < g->num_edges[v + 1]; j++) {
            if (deg[g->adj[j]] >= level) {
                edgeCount++;
            }
            }
            g_small.num_edges[i] = prevEdgeCount;
        }
        cumNumEdges[tid] = edgeCount;
        } else {
        for (long i = tid * part; i < (tid + 1) * part; i++) {
            unsigned int v = mapIndexToVtx[i];
            eid_t prevEdgeCount = edgeCount;
            for (eid_t j = g->num_edges[v]; j < g->num_edges[v + 1]; j++) {
            if (deg[g->adj[j]] >= level) {
                edgeCount++;
            }
            }
            g_small.num_edges[i] = prevEdgeCount;
        }
        cumNumEdges[tid] = edgeCount;
        }

#pragma omp barrier
        if (tid == 0) {
        long start0 = cumNumEdges[0];
        for (int i = 1; i < NUM_THREADS; i++) {
            eid_t prevEdgeCount = start0;
            start0 = start0 + cumNumEdges[i];
            cumNumEdges[i] = prevEdgeCount;
        }
        g_small.m = start0;
        g_small.num_edges[Size] = start0;

        g_small.adj = (vid_t *)malloc(g_small.m * sizeof(vid_t));
        cumNumEdges[0] = 0;
        }
#pragma omp barrier
        if (tid == (NUM_THREADS - 1)) {
        for (long i = tid * part; i < Size; i++) {
            g_small.num_edges[i] = g_small.num_edges[i] + cumNumEdges[tid];

            unsigned int v = mapIndexToVtx[i];
            for (eid_t j = g->num_edges[v]; j < g->num_edges[v + 1]; j++) {
            if (deg[g->adj[j]] >= level) {
                g_small.adj[g_small.num_edges[i]] = vertexToIndex[g->adj[j]];
                g_small.num_edges[i]++;
            }
            }
        }
        } else {
        for (long i = tid * part; i < (tid + 1) * part; i++) {
            g_small.num_edges[i] = g_small.num_edges[i] + cumNumEdges[tid];

            unsigned int v = mapIndexToVtx[i];
            for (eid_t j = g->num_edges[v]; j < g->num_edges[v + 1]; j++) {
            if (deg[g->adj[j]] >= level) {
                g_small.adj[g_small.num_edges[i]] = vertexToIndex[g->adj[j]];
                g_small.num_edges[i]++;
            }
            }
        }
        }
#pragma omp barrier
        // Now fix num_edges array
        if (tid == (NUM_THREADS - 1)) {
        for (long i = (Size - 1); i >= (tid * part + 1); i--) {
            g_small.num_edges[i] = g_small.num_edges[i - 1];
        }
        g_small.num_edges[tid * part] = cumNumEdges[tid];

        } else {
        for (long i = ((tid + 1) * part - 1); i >= (tid * part + 1);
            i--) {  // part can't be unsigned int here
            g_small.num_edges[i] = g_small.num_edges[i - 1];
        }
        g_small.num_edges[tid * part] = cumNumEdges[tid];
        }
#pragma omp barrier
    }

    if (useSmallQ == 0) {
#pragma omp for schedule(static)
        for (long i = 0; i < n; i++) {
        if (deg[i] == level) {
#if BUFF_ACCESS_ASSERTS
            assert(end < buff_size);
#endif
            buff[end] = i;
            end++;
        }
        }

        // Get work from curr queue and also add work after the current size
        while (start < end) {
        vid_t v = buff[start];
        start++;

        for (eid_t j = g->num_edges[v]; j < g->num_edges[v + 1]; j++) {
            vid_t u = g->adj[j];

            int deg_u = deg[u];

            if (deg_u > level) {
            int du = __sync_fetch_and_sub(&deg[u], 1);

            if (du == (level + 1)) {
#if BUFF_ACCESS_ASSERTS
                assert(end < buff_size);
#endif
                buff[end] = u;
                end++;
            }

            if (du <= level) {
                __sync_fetch_and_add(&deg[u], 1);
            }
            }  // deg_u > level

        }  // visit adjacencies
        }  // end of while loop
    } else {
#pragma omp for schedule(static)
        for (long i = 0; i < Size; i++) {
        if (newDeg[i] == level) {
            buff[end] = i;
            end++;
        }
        }

        // Get work from curr queue and also add work after the current size
        while (start < end) {
        vid_t v = buff[start];
        start++;

        for (eid_t j = g_small.num_edges[v]; j < g_small.num_edges[v + 1];
            j++) {
            vid_t u = g_small.adj[j];

            int deg_u = newDeg[u];

            if (deg_u > level) {
            int du = __sync_fetch_and_sub(&newDeg[u], 1);

            if (du == (level + 1)) {
#if BUFF_ACCESS_ASSERTS
                assert(end < buff_size);
#endif
                buff[end] = u;
                end++;
            }

            if (du <= level) {
                __sync_fetch_and_add(&newDeg[u], 1);
            }
            }  // deg_u > level
        }  // visit adjacencies
        }  // end of while loop
    }

    __sync_fetch_and_add(&visited, end);

#pragma omp barrier
    start = 0;
    end = 0;
    level = level + 1;
    // printf("level: %d\n", level);

    }  // end of #visited < n

    free(buff);

    // copy core values from newDeg to deg
#pragma omp for schedule(static)
    for (long i = 0; i < Size; i++) {
    deg[mapIndexToVtx[i]] = newDeg[i];
    }

}  // #end of parallel region

free(newDeg);
free(mapIndexToVtx);
free(vertexToIndex);
free_graph(&g_small);
return deg;
}
