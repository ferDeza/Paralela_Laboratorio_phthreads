#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "lab_common.h"

#define DEFAULT_STEPS 30000000LL
#define PI_REFERENCE 3.14159265358979323846

typedef struct {
    int rank;
    int thread_count;
    long long steps;
} ThreadArgs;

static pthread_mutex_t sum_mutex;
static double global_sum;

static double pi_sequential(long long steps) {
    double step = 1.0 / (double)steps;
    double sum = 0.0;

    for (long long i = 0; i < steps; i++) {
        double x = ((double)i + 0.5) * step;
        //integral of 4/(1+x^2) from 0 to 1 is pi
        sum += 4.0 / (1.0 + x * x);
    }

    return sum * step;
}

static void *pi_worker(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    long long base = args->steps / args->thread_count;
    long long remainder = args->steps % args->thread_count;
    long long first = (long long)args->rank * base +
                      (args->rank < remainder ? args->rank : remainder);
    long long count = base + (args->rank < remainder ? 1 : 0);
    double step = 1.0 / (double)args->steps;
    double local_sum = 0.0;

    for (long long i = first; i < first + count; i++) {
        double x = ((double)i + 0.5) * step;
        local_sum += 4.0 / (1.0 + x * x);
    }

    if (pthread_mutex_lock(&sum_mutex) != 0) {
        return (void *)1;
    }
    global_sum += local_sum;
    if (pthread_mutex_unlock(&sum_mutex) != 0) {
        return (void *)1;
    }

    return NULL;
}

int main(int argc, char **argv) {
    int thread_count;
    long long steps = DEFAULT_STEPS;
    pthread_t *threads = NULL;
    ThreadArgs *args = NULL;
    int created = 0;
    int result = EXIT_FAILURE;
    double start;
    double sequential_time;
    double parallel_time;
    double pi_seq;
    double pi_par;

    if (argc < 2 || argc > 3 ||
        lab_parse_positive_int(argv[1], &thread_count) != 0 ||
        (argc == 3 && lab_parse_positive_ll(argv[2], &steps) != 0)) {
        fprintf(stderr, "Uso: %s <threads> [iteraciones]\n", argv[0]);
        return EXIT_FAILURE;
    }

    start = lab_now_seconds();
    if (start < 0.0) {
        return EXIT_FAILURE;
    }
    pi_seq = pi_sequential(steps);
    sequential_time = lab_now_seconds() - start;

    threads = (pthread_t *)malloc((size_t)thread_count * sizeof(*threads));
    args = (ThreadArgs *)malloc((size_t)thread_count * sizeof(*args));
    if (threads == NULL || args == NULL) {
        perror("No se pudieron reservar los arreglos de threads");
        goto cleanup;
    }

    if (pthread_mutex_init(&sum_mutex, NULL) != 0) {
        fprintf(stderr, "No se pudo inicializar el mutex.\n");
        goto cleanup;
    }
    global_sum = 0.0;
    start = lab_now_seconds();
    if (start < 0.0) {
        pthread_mutex_destroy(&sum_mutex);
        goto cleanup;
    }

    for (int i = 0; i < thread_count; i++) {
        args[i].rank = i;
        args[i].thread_count = thread_count;
        args[i].steps = steps;
        if (pthread_create(&threads[i], NULL, pi_worker, &args[i]) != 0) {
            fprintf(stderr, "No se pudo crear el thread %d.\n", i);
            goto join_threads;
        }
        created++;
    }

join_threads:
    for (int i = 0; i < created; i++) {
        void *worker_result = NULL;
        if (pthread_join(threads[i], &worker_result) != 0 || worker_result != NULL) {
            fprintf(stderr, "Error al esperar el thread %d.\n", i);
            created = -1;
        }
    }
    parallel_time = lab_now_seconds() - start;
    pthread_mutex_destroy(&sum_mutex);
    if (created < 0 || created != thread_count) {
        goto cleanup;
    }

    pi_par = global_sum / (double)steps;
    printf("Iteraciones: %lld | Threads: %d\n", steps, thread_count);
    printf("Pi secuencial: %.15f\n", pi_seq);
    printf("Pi paralelo:   %.15f\n", pi_par);
    printf("Referencia:    %.15f\n", PI_REFERENCE);
    printf("Error absoluto (paralelo): %.3e\n", pi_par > PI_REFERENCE
               ? pi_par - PI_REFERENCE
               : PI_REFERENCE - pi_par);
    lab_print_metrics(sequential_time, parallel_time, thread_count);
    result = EXIT_SUCCESS;

cleanup:
    free(args);
    free(threads);
    return result;
}