#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lab_common.h"

typedef enum {
    BARRIER_BUSY,
    BARRIER_SEMAPHORE,
    BARRIER_CONDITION
} BarrierKind;

typedef struct {
    int participants;
    int count;
    unsigned int generation;
    pthread_mutex_t mutex;
} BusyBarrier;

typedef struct {
    int participants;
    int count;
    sem_t count_sem;
    sem_t *round_semaphores;
    int rounds;
} SemaphoreBarrier;

typedef struct {
    int participants;
    int count;
    unsigned int generation;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
} ConditionBarrier;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int ready;
    int participants;
    int open;
    int abort;
} StartGate;

typedef struct {
    int rank;
    int rounds;
    int failed;
    int failure_round;
    int observed_arrivals;
    atomic_int *arrivals;
} ThreadArgs;

static BusyBarrier busy_barrier;
static SemaphoreBarrier semaphore_barrier;
static ConditionBarrier condition_barrier;
static StartGate start_gate;
static BarrierKind selected_barrier;

static int semaphore_wait(sem_t *semaphore) {
    int status;
    do {
        status = sem_wait(semaphore);
    } while (status != 0 && errno == EINTR);
    return status;
}

static int busy_barrier_init(BusyBarrier *barrier, int participants) {
    barrier->participants = participants;
    barrier->count = 0;
    barrier->generation = 0;
    return pthread_mutex_init(&barrier->mutex, NULL);
}

static int busy_barrier_wait(BusyBarrier *barrier) {
    unsigned int generation =
        __atomic_load_n(&barrier->generation, __ATOMIC_RELAXED);

    if (pthread_mutex_lock(&barrier->mutex) != 0) {
        return -1;
    }
    barrier->count++;
    if (barrier->count == barrier->participants) {
        barrier->count = 0;
        __atomic_add_fetch(&barrier->generation, 1, __ATOMIC_RELEASE);
        return pthread_mutex_unlock(&barrier->mutex);
    }
    if (pthread_mutex_unlock(&barrier->mutex) != 0) {
        return -1;
    }
    while (__atomic_load_n(&barrier->generation, __ATOMIC_ACQUIRE) ==
           generation) {
    }
    return 0;
}

static int semaphore_barrier_init(SemaphoreBarrier *barrier,
                                  int participants,
                                  int rounds) {
    barrier->participants = participants;
    barrier->count = 0;
    barrier->rounds = rounds;
    barrier->round_semaphores =
        (sem_t *)malloc((size_t)rounds * sizeof(*barrier->round_semaphores));
    if (barrier->round_semaphores == NULL) {
        return -1;
    }
    if (sem_init(&barrier->count_sem, 0, 1) != 0) {
        free(barrier->round_semaphores);
        barrier->round_semaphores = NULL;
        return -1;
    }
    for (int i = 0; i < rounds; i++) {
        if (sem_init(&barrier->round_semaphores[i], 0, 0) != 0) {
            while (i > 0) {
                sem_destroy(&barrier->round_semaphores[--i]);
            }
            sem_destroy(&barrier->count_sem);
            free(barrier->round_semaphores);
            barrier->round_semaphores = NULL;
            return -1;
        }
    }
    return 0;
}

static int semaphore_barrier_wait(SemaphoreBarrier *barrier, int round) {
    sem_t *round_semaphore = &barrier->round_semaphores[round];

    if (semaphore_wait(&barrier->count_sem) != 0) {
        return -1;
    }
    if (barrier->count == barrier->participants - 1) {
        barrier->count = 0;
        if (sem_post(&barrier->count_sem) != 0) {
            return -1;
        }
        for (int i = 0; i < barrier->participants - 1; i++) {
            if (sem_post(round_semaphore) != 0) {
                return -1;
            }
        }
    } else {
        barrier->count++;
        if (sem_post(&barrier->count_sem) != 0 ||
            semaphore_wait(round_semaphore) != 0) {
            return -1;
        }
    }
    return 0;
}

static int condition_barrier_init(ConditionBarrier *barrier,
                                  int participants) {
    barrier->participants = participants;
    barrier->count = 0;
    barrier->generation = 0;
    if (pthread_mutex_init(&barrier->mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&barrier->condition, NULL) != 0) {
        pthread_mutex_destroy(&barrier->mutex);
        return -1;
    }
    return 0;
}

static int condition_barrier_wait(ConditionBarrier *barrier) {
    unsigned int generation;

    if (pthread_mutex_lock(&barrier->mutex) != 0) {
        return -1;
    }
    generation = barrier->generation;
    barrier->count++;
    if (barrier->count == barrier->participants) {
        barrier->count = 0;
        barrier->generation++;
        if (pthread_cond_broadcast(&barrier->condition) != 0) {
            pthread_mutex_unlock(&barrier->mutex);
            return -1;
        }
    } else {
        while (generation == barrier->generation) {
            if (pthread_cond_wait(&barrier->condition, &barrier->mutex) != 0) {
                pthread_mutex_unlock(&barrier->mutex);
                return -1;
            }
        }
    }
    return pthread_mutex_unlock(&barrier->mutex);
}

static int start_gate_init(StartGate *gate, int participants) {
    gate->ready = 0;
    gate->participants = participants;
    gate->open = 0;
    gate->abort = 0;
    if (pthread_mutex_init(&gate->mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&gate->condition, NULL) != 0) {
        pthread_mutex_destroy(&gate->mutex);
        return -1;
    }
    return 0;
}

static int start_gate_wait(StartGate *gate) {
    if (pthread_mutex_lock(&gate->mutex) != 0) {
        return -1;
    }
    gate->ready++;
    pthread_cond_broadcast(&gate->condition);
    while (!gate->open) {
        if (pthread_cond_wait(&gate->condition, &gate->mutex) != 0) {
            pthread_mutex_unlock(&gate->mutex);
            return -1;
        }
    }
    {
        int aborted = gate->abort;
        if (pthread_mutex_unlock(&gate->mutex) != 0) {
            return -1;
        }
        return aborted ? 1 : 0;
    }
}

static int start_gate_cancel(StartGate *gate) {
    if (pthread_mutex_lock(&gate->mutex) != 0) {
        return -1;
    }
    gate->abort = 1;
    gate->open = 1;
    if (pthread_cond_broadcast(&gate->condition) != 0) {
        pthread_mutex_unlock(&gate->mutex);
        return -1;
    }
    return pthread_mutex_unlock(&gate->mutex);
}

static int start_gate_release(StartGate *gate) {
    if (pthread_mutex_lock(&gate->mutex) != 0) {
        return -1;
    }
    while (gate->ready != gate->participants) {
        if (pthread_cond_wait(&gate->condition, &gate->mutex) != 0) {
            pthread_mutex_unlock(&gate->mutex);
            return -1;
        }
    }
    gate->open = 1;
    if (pthread_cond_broadcast(&gate->condition) != 0) {
        pthread_mutex_unlock(&gate->mutex);
        return -1;
    }
    return pthread_mutex_unlock(&gate->mutex);
}

static int barrier_wait(int round) {
    switch (selected_barrier) {
        case BARRIER_BUSY:
            return busy_barrier_wait(&busy_barrier);
        case BARRIER_SEMAPHORE:
            return semaphore_barrier_wait(&semaphore_barrier, round);
        case BARRIER_CONDITION:
            return condition_barrier_wait(&condition_barrier);
    }
    return -1;
}

static void *barrier_worker(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    int gate_status = start_gate_wait(&start_gate);

    if (gate_status < 0) {
        args->failed = 1;
        return NULL;
    }
    if (gate_status > 0) {
        return NULL;
    }
    for (int round = 0; round < args->rounds; round++) {
        int observed;
        atomic_fetch_add_explicit(&args->arrivals[round], 1,
                                  memory_order_acq_rel);
        if (barrier_wait(round) != 0) {
            args->failed = 1;
            args->failure_round = round;
            args->observed_arrivals = -1;
            return NULL;
        }
        observed = atomic_load_explicit(&args->arrivals[round],
                                        memory_order_acquire);
        if (observed != start_gate.participants) {
            args->failed = 1;
            args->failure_round = round;
            args->observed_arrivals = observed;
            return NULL;
        }
    }
    return NULL;
}

static int run_barrier(BarrierKind kind,
                       const char *name,
                       int thread_count,
                       int rounds) {
    pthread_t *threads = NULL;
    ThreadArgs *args = NULL;
    atomic_int *arrivals = NULL;
    int created = 0;
    int initialized = 0;
    double start = 0.0;
    double elapsed;
    int started = 0;
    int result = -1;

    selected_barrier = kind;
    if (kind == BARRIER_BUSY) {
        initialized = busy_barrier_init(&busy_barrier, thread_count) == 0;
    } else if (kind == BARRIER_SEMAPHORE) {
        initialized =
            semaphore_barrier_init(&semaphore_barrier, thread_count, rounds) ==
            0;
    } else {
        initialized =
            condition_barrier_init(&condition_barrier, thread_count) == 0;
    }
    if (!initialized) {
        fprintf(stderr, "No se pudo inicializar la barrera %s.\n", name);
        return -1;
    }
    if (start_gate_init(&start_gate, thread_count) != 0) {
        fprintf(stderr, "No se pudo inicializar la compuerta de inicio.\n");
        goto cleanup_barrier;
    }

    threads = (pthread_t *)malloc((size_t)thread_count * sizeof(*threads));
    args = (ThreadArgs *)malloc((size_t)thread_count * sizeof(*args));
    arrivals = (atomic_int *)malloc((size_t)rounds * sizeof(*arrivals));
    if (threads == NULL || args == NULL || arrivals == NULL) {
        fprintf(stderr, "No se pudo reservar memoria para probar la barrera.\n");
        goto cleanup_gate;
    }
    for (int i = 0; i < rounds; i++) {
        atomic_init(&arrivals[i], 0);
    }
    for (int i = 0; i < thread_count; i++) {
        args[i].rank = i;
        args[i].rounds = rounds;
        args[i].failed = 0;
        args[i].failure_round = -1;
        args[i].observed_arrivals = 0;
        args[i].arrivals = arrivals;
        if (pthread_create(&threads[i], NULL, barrier_worker, &args[i]) != 0) {
            fprintf(stderr, "No se pudo crear el thread %d.\n", i);
            start_gate_cancel(&start_gate);
            goto join_threads;
        }
        created++;
    }

    start = lab_now_seconds();
    if (start < 0.0 || start_gate_release(&start_gate) != 0) {
        fprintf(stderr, "No se pudo liberar la compuerta de inicio.\n");
        start_gate_cancel(&start_gate);
        goto join_threads;
    }
    started = 1;

join_threads:
    for (int i = 0; i < created; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "Error al esperar el thread %d.\n", i);
            created = -1;
        }
    }
    elapsed = started ? lab_now_seconds() - start : 0.0;
    if (created != thread_count || created < 0 || !started) {
        fprintf(stderr, "La prueba de barrera %s fallo.\n", name);
        goto cleanup_allocations;
    }
    for (int i = 0; i < thread_count; i++) {
        if (args[i].failed) {
            fprintf(stderr,
                    "La prueba de barrera %s fallo en thread %d, ronda %d "
                    "(llegadas observadas: %d).\n",
                    name, i, args[i].failure_round, args[i].observed_arrivals);
            goto cleanup_allocations;
        }
    }

    printf("%-24s | threads=%d | rondas=%d | %.6f s | %.3f us/barrera | "
           "correcta\n",
           name, thread_count, rounds, elapsed,
           elapsed * 1000000.0 / (double)rounds);
    result = 0;

cleanup_allocations:
    free(arrivals);
    free(args);
    free(threads);
cleanup_gate:
    pthread_cond_destroy(&start_gate.condition);
    pthread_mutex_destroy(&start_gate.mutex);
cleanup_barrier:
    if (kind == BARRIER_BUSY) {
        pthread_mutex_destroy(&busy_barrier.mutex);
    } else if (kind == BARRIER_SEMAPHORE) {
        for (int i = 0; i < semaphore_barrier.rounds; i++) {
            sem_destroy(&semaphore_barrier.round_semaphores[i]);
        }
        sem_destroy(&semaphore_barrier.count_sem);
        free(semaphore_barrier.round_semaphores);
    } else {
        pthread_cond_destroy(&condition_barrier.condition);
        pthread_mutex_destroy(&condition_barrier.mutex);
    }
    return result;
}

int main(int argc, char **argv) {
    int thread_count;
    int rounds = 10000;

    if ((argc != 2 && argc != 3) ||
        lab_parse_positive_int(argv[1], &thread_count) != 0 ||
        (argc == 3 && lab_parse_positive_int(argv[2], &rounds) != 0)) {
        fprintf(stderr, "Uso: %s <threads> [rondas]\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("Prueba de barreras reutilizables; cada thread registra su llegada "
           "en cada ronda.\n");
    if (run_barrier(BARRIER_BUSY, "Espera activa + mutex", thread_count,
                    rounds) != 0 ||
        run_barrier(BARRIER_SEMAPHORE, "Semaforos (una barrera por ronda)",
                    thread_count, rounds) != 0 ||
        run_barrier(BARRIER_CONDITION, "Mutex + variable condicional",
                    thread_count, rounds) != 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
