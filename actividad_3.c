#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lab_common.h"

typedef struct Node {
    long long value;
    struct Node *next;
} Node;

typedef struct {
    int rank;
    int operations;
    int thread_count;
    int failed;
} ThreadArgs;

static Node *head;
static size_t list_size;
static pthread_rwlock_t list_lock;
static int unsynchronized_mode;

static int insert_value(long long value) {
    Node **position;
    Node *node;

    if (pthread_rwlock_wrlock(&list_lock) != 0) {
        return -1;
    }
    position = &head;
    while (*position != NULL && (*position)->value < value) {
        position = &(*position)->next;
    }
    if (*position != NULL && (*position)->value == value) {
        pthread_rwlock_unlock(&list_lock);
        return 0;
    }
    node = (Node *)malloc(sizeof(*node));
    if (node == NULL) {
        pthread_rwlock_unlock(&list_lock);
        return -1;
    }
    node->value = value;
    node->next = *position;
    *position = node;
    list_size++;
    if (pthread_rwlock_unlock(&list_lock) != 0) {
        return -1;
    }
    return 1;
}

static int contains_value(long long value) {
    Node *current;
    int found = 0;

    if (pthread_rwlock_rdlock(&list_lock) != 0) {
        return -1;
    }
    current = head;
    while (current != NULL && current->value < value) {
        current = current->next;
    }
    found = current != NULL && current->value == value;
    if (pthread_rwlock_unlock(&list_lock) != 0) {
        return -1;
    }
    return found;
}

static int delete_value(long long value) {
    Node **position;
    Node *removed;

    if (pthread_rwlock_wrlock(&list_lock) != 0) {
        return -1;
    }
    position = &head;
    while (*position != NULL && (*position)->value < value) {
        position = &(*position)->next;
    }
    if (*position == NULL || (*position)->value != value) {
        pthread_rwlock_unlock(&list_lock);
        return 0;
    }
    removed = *position;
    *position = removed->next;
    free(removed);
    list_size--;
    if (pthread_rwlock_unlock(&list_lock) != 0) {
        return -1;
    }
    return 1;
}

static void *list_worker(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    long long base = (long long)args->rank * args->operations;

    if (unsynchronized_mode) {
        for (int i = 0; i < args->operations; i++) {
            Node *node = (Node *)malloc(sizeof(*node));
            if (node == NULL) {
                args->failed = 1;
                return NULL;
            }
            node->value = base + i;
            node->next = head;
            head = node;
        }
        return NULL;
    }

    for (int i = 0; i < args->operations; i++) {
        long long value = base + i;
        if (insert_value(value) != 1 || contains_value(value) != 1 ||
            (i % 2 == 0 && delete_value(value) != 1)) {
            args->failed = 1;
            break;
        }
    }
    return NULL;
}

static size_t count_nodes(void) {
    size_t count = 0;
    for (Node *current = head; current != NULL; current = current->next) {
        count++;
    }
    return count;
}

static void free_nodes(void) {
    Node *current = head;
    while (current != NULL) {
        Node *next = current->next;
        free(current);
        current = next;
    }
    head = NULL;
}

int main(int argc, char **argv) {
    int thread_count;
    int operations;
    int first_argument = 1;
    pthread_t *threads = NULL;
    ThreadArgs *args = NULL;
    int created = 0;
    int result = EXIT_FAILURE;
    double start;
    double elapsed;
    size_t final_size;
    size_t expected_size;

    if (argc >= 2 && strcmp(argv[1], "--sin-sync") == 0) {
        unsynchronized_mode = 1;
        first_argument++;
    }
    if (argc != first_argument + 2 ||
        lab_parse_positive_int(argv[first_argument], &thread_count) != 0 ||
        lab_parse_positive_int(argv[first_argument + 1], &operations) != 0) {
        fprintf(stderr, "Uso: %s [--sin-sync] <threads> <operaciones-por-thread>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    threads = (pthread_t *)malloc((size_t)thread_count * sizeof(*threads));
    args = (ThreadArgs *)calloc((size_t)thread_count, sizeof(*args));
    if (threads == NULL || args == NULL) {
        fprintf(stderr, "No se pudo reservar memoria para los threads.\n");
        goto cleanup;
    }
    if (pthread_rwlock_init(&list_lock, NULL) != 0) {
        fprintf(stderr, "No se pudo inicializar el read-write lock.\n");
        goto cleanup;
    }

    start = lab_now_seconds();
    if (start < 0.0) {
        pthread_rwlock_destroy(&list_lock);
        goto cleanup;
    }
    for (int i = 0; i < thread_count; i++) {
        args[i].rank = i;
        args[i].operations = operations;
        args[i].thread_count = thread_count;
        if (pthread_create(&threads[i], NULL, list_worker, &args[i]) != 0) {
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
    elapsed = lab_now_seconds() - start;
    if (created < 0 || created != thread_count) {
        pthread_rwlock_destroy(&list_lock);
        goto cleanup;
    }
    for (int i = 0; i < thread_count; i++) {
        if (args[i].failed) {
            fprintf(stderr, "El thread %d no pudo completar sus operaciones.\n",
                    i);
            pthread_rwlock_destroy(&list_lock);
            goto cleanup;
        }
    }

    if (unsynchronized_mode) {
        final_size = count_nodes();
        expected_size = (size_t)thread_count * (size_t)operations;
    } else {
        if (pthread_rwlock_rdlock(&list_lock) != 0) {
            fprintf(stderr, "No se pudo bloquear la lista para validarla.\n");
            pthread_rwlock_destroy(&list_lock);
            goto cleanup;
        }
        final_size = count_nodes();
        expected_size = (size_t)thread_count *
                        ((size_t)operations / 2 + (size_t)operations % 2);
        if (final_size != list_size) {
            fprintf(stderr, "El contador interno de la lista es inconsistente.\n");
            pthread_rwlock_unlock(&list_lock);
            pthread_rwlock_destroy(&list_lock);
            goto cleanup;
        }
        pthread_rwlock_unlock(&list_lock);
    }

    printf("Modo: %s | Threads: %d | Operaciones/thread: %d\n",
           unsynchronized_mode ? "SIN sincronizacion (demostracion de carrera)"
                               : "read-write lock",
           thread_count, operations);
    printf("Nodos finales: %zu | Esperados: %zu | Consistencia: %s\n",
           final_size, expected_size,
           final_size == expected_size ? "correcta" : "FALLO");
    printf("Tiempo concurrente: %.6f s\n", elapsed);
    if (unsynchronized_mode) {
        printf("La ejecucion sin lock tiene comportamiento indefinido en C; "
               "el resultado puede variar.\n");
    }
    result = final_size == expected_size ? EXIT_SUCCESS : EXIT_FAILURE;
    pthread_rwlock_destroy(&list_lock);

cleanup:
    free_nodes();
    free(args);
    free(threads);
    return result;
}
