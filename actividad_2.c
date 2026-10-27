#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "lab_common.h"

#define DEFAULT_SIZE 1000LL

typedef struct {
    int rank;
    int thread_count;
    long long size;
    const double *matrix;
    const double *vector;
    double *result;
} ThreadArgs;

static void matrix_vector_sequential(long long size,
                                     const double *matrix,
                                     const double *vector,
                                     double *result) {
    for (long long row = 0; row < size; row++) {
        double sum = 0.0;
        for (long long col = 0; col < size; col++) {
            sum += matrix[(size_t)row * (size_t)size + (size_t)col] *
                   vector[col];
        }
        result[row] = sum;
    }
}

static void *matrix_vector_worker(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    long long base = args->size / args->thread_count;
    long long remainder = args->size % args->thread_count;
    long long first = (long long)args->rank * base +
                      (args->rank < remainder ? args->rank : remainder);
    long long end = first + base + (args->rank < remainder ? 1 : 0);

    for (long long row = first; row < end; row++) {
        double sum = 0.0;
        for (long long col = 0; col < args->size; col++) {
            sum += args->matrix[(size_t)row * (size_t)args->size +
                                (size_t)col] *
                   args->vector[col];
        }
        args->result[row] = sum;
    }

    return NULL;
}

int main(int argc, char **argv) {
    int thread_count;
    long long size = DEFAULT_SIZE;
    size_t cells;
    double *matrix = NULL;
    double *vector = NULL;
    double *sequential = NULL;
    double *parallel = NULL;
    pthread_t *threads = NULL;
    ThreadArgs *args = NULL;
    int created = 0;
    int result = EXIT_FAILURE;
    double start;
    double sequential_time;
    double parallel_time;
    double max_error = 0.0;

    if (argc < 2 || argc > 3 ||
        lab_parse_positive_int(argv[1], &thread_count) != 0 ||
        (argc == 3 && lab_parse_positive_ll(argv[2], &size) != 0)) {
        fprintf(stderr, "Uso: %s <threads> [dimension-cuadrada]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if ((unsigned long long)size >
        (unsigned long long)SIZE_MAX / (unsigned long long)size) {
        fprintf(stderr, "La dimension es demasiado grande.\n");
        return EXIT_FAILURE;
    }
    cells = (size_t)size * (size_t)size;
    if (cells > SIZE_MAX / sizeof(double)) {
        fprintf(stderr, "La dimension es demasiado grande.\n");
        return EXIT_FAILURE;
    }

    matrix = (double *)malloc(cells * sizeof(*matrix));
    vector = (double *)malloc((size_t)size * sizeof(*vector));
    sequential = (double *)malloc((size_t)size * sizeof(*sequential));
    parallel = (double *)malloc((size_t)size * sizeof(*parallel));
    threads = (pthread_t *)malloc((size_t)thread_count * sizeof(*threads));
    args = (ThreadArgs *)malloc((size_t)thread_count * sizeof(*args));
    if (matrix == NULL || vector == NULL || sequential == NULL ||
        parallel == NULL || threads == NULL || args == NULL) {
        fprintf(stderr, "No se pudo reservar memoria para la dimension %lld.\n",
                size);
        goto cleanup;
    }

    for (long long row = 0; row < size; row++) {
        vector[row] = (double)(row % 13 + 1) / 13.0;
        for (long long col = 0; col < size; col++) {
            matrix[(size_t)row * (size_t)size + (size_t)col] =
                (double)((row * 3 + col * 7) % 101) / 101.0;
        }
    }

    start = lab_now_seconds();
    if (start < 0.0) {
        goto cleanup;
    }
    matrix_vector_sequential(size, matrix, vector, sequential);
    sequential_time = lab_now_seconds() - start;

    start = lab_now_seconds();
    if (start < 0.0) {
        goto cleanup;
    }
    for (int i = 0; i < thread_count; i++) {
        args[i].rank = i;
        args[i].thread_count = thread_count;
        args[i].size = size;
        args[i].matrix = matrix;
        args[i].vector = vector;
        args[i].result = parallel;
        if (pthread_create(&threads[i], NULL, matrix_vector_worker, &args[i]) !=
            0) {
            fprintf(stderr, "No se pudo crear el thread %d.\n", i);
            goto join_threads;
        }
        created++;
    }

join_threads:
    for (int i = 0; i < created; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "Error al esperar el thread %d.\n", i);
            created = -1;
        }
    }
    parallel_time = lab_now_seconds() - start;
    if (created < 0 || created != thread_count) {
        goto cleanup;
    }

    for (long long row = 0; row < size; row++) {
        double error = fabs(sequential[row] - parallel[row]);
        if (error > max_error) {
            max_error = error;
        }
    }

    printf("Matriz: %lld x %lld | Threads: %d\n", size, size, thread_count);
    printf("Error maximo entre resultados: %.3e\n", max_error);
    printf("Verificacion: %s\n", max_error < 1e-10 ? "correcta" : "FALLO");
    lab_print_metrics(sequential_time, parallel_time, thread_count);
    result = max_error < 1e-10 ? EXIT_SUCCESS : EXIT_FAILURE;

cleanup:
    free(args);
    free(threads);
    free(parallel);
    free(sequential);
    free(vector);
    free(matrix);
    return result;
}
