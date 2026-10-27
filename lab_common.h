#ifndef LAB_COMMON_H
#define LAB_COMMON_H

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static inline int lab_parse_positive_ll(const char *text, long long *value) {
    char *end = NULL;
    long long parsed;

    errno = 0;
    parsed = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed <= 0) {
        return -1;
    }

    *value = parsed;
    return 0;
}

static inline int lab_parse_positive_int(const char *text, int *value) {
    long long parsed;

    if (lab_parse_positive_ll(text, &parsed) != 0 || parsed > INT_MAX) {
        return -1;
    }

    *value = (int)parsed;
    return 0;
}

static inline double lab_now_seconds(void) {
    struct timespec now;

    if (timespec_get(&now, TIME_UTC) != TIME_UTC) {
        fprintf(stderr, "No se pudo leer el reloj del sistema.\n");
        return -1.0;
    }

    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static inline void lab_print_metrics(double sequential_time,
                                     double parallel_time,
                                     int thread_count) {
    double speedup = parallel_time > 0.0 ? sequential_time / parallel_time : 0.0;
    double efficiency = speedup / (double)thread_count * 100.0;

    printf("Tiempo secuencial: %.9f s\n", sequential_time);
    printf("Tiempo paralelo:   %.9f s\n", parallel_time);
    printf("Speedup: %.3fx | Eficiencia: %.2f%%\n", speedup, efficiency);
}

#endif
