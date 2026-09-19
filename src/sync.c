#include "minios/sync.h"

#include <stdlib.h>

#include "private/process_internal.h"

typedef struct mos_sync_wait_queue {
    mos_pid_t pids[MOS_PROCESS_MAX];
    size_t head;
    size_t size;
} mos_sync_wait_queue_t;

typedef struct mos_mutex_private {
    mos_pid_t owner_pid;
    mos_sync_wait_queue_t waiters;
} mos_mutex_private_t;

static mos_status_t mos_sync_require_kernel(const mos_kernel_t *kernel) {
    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }

    return MOS_OK;
}

static mos_pcb_t *mos_sync_find_process(mos_kernel_t *kernel, mos_pid_t pid) {
    mos_process_table_t *table =
        (mos_process_table_t *)kernel->private_state;
    size_t index;

    for (index = 0U; index < MOS_PROCESS_MAX; index++) {
        if (table->entries[index].state != MOS_PROC_UNUSED &&
            table->entries[index].pid == pid) {
            return &table->entries[index];
        }
    }

    return NULL;
}

static mos_status_t mos_sync_validate_process(mos_kernel_t *kernel,
                                               mos_pid_t pid,
                                               mos_pcb_t **pcb_out) {
    mos_pcb_t *pcb;

    if (pid <= 0) {
        return MOS_ERR_RANGE;
    }

    pcb = mos_sync_find_process(kernel, pid);
    if (pcb == NULL) {
        return MOS_ERR_NOT_FOUND;
    }
    if (pcb->state == MOS_PROC_BLOCKED ||
        pcb->state == MOS_PROC_EXITED) {
        return MOS_ERR_STATE;
    }

    *pcb_out = pcb;
    return MOS_OK;
}

static void mos_sync_queue_push(mos_sync_wait_queue_t *queue, mos_pid_t pid) {
    size_t index = (queue->head + queue->size) % MOS_PROCESS_MAX;

    queue->pids[index] = pid;
    queue->size++;
}

static mos_pid_t mos_sync_queue_pop(mos_sync_wait_queue_t *queue) {
    mos_pid_t pid = queue->pids[queue->head];

    queue->head = (queue->head + 1U) % MOS_PROCESS_MAX;
    queue->size--;
    return pid;
}

/*
 * LAB4 구현 안내
 * - semaphore 카운터와 대기 큐를 어떻게 결합할지 설계한다.
 * - mutex는 소유자 추적, 중복 lock, unlock 오류 처리를 분명히 해야 한다.
 * - 단일 스레드 시뮬레이션이어도 race-free 추상 동작을 문서화하며 구현한다.
 */

mos_status_t mos_sem_init(mos_semaphore_t *sem, int initial_value) {
    mos_sync_wait_queue_t *queue;

    if (sem == NULL) {
        return MOS_ERR_NULL;
    }
    if (initial_value < 0) {
        return MOS_ERR_INVALID;
    }

    queue = (mos_sync_wait_queue_t *)calloc(1U, sizeof(*queue));
    if (queue == NULL) {
        return MOS_ERR_NO_SPACE;
    }

    sem->value = initial_value;
    sem->waiters = queue;
    return MOS_OK;
}

mos_status_t mos_sem_wait(mos_kernel_t *kernel, mos_semaphore_t *sem, mos_pid_t pid) {
    mos_sync_wait_queue_t *queue;
    mos_pcb_t *pcb;

    if (kernel == NULL || sem == NULL) {
        return MOS_ERR_NULL;
    }
    if (mos_sync_require_kernel(kernel) != MOS_OK) {
        return mos_sync_require_kernel(kernel);
    }
    if (sem->waiters == NULL) {
        return MOS_ERR_STATE;
    }
    if (mos_sync_validate_process(kernel, pid, &pcb) != MOS_OK) {
        return mos_sync_validate_process(kernel, pid, &pcb);
    }

    queue = (mos_sync_wait_queue_t *)sem->waiters;
    if (sem->value > 0) {
        sem->value--;
        return MOS_OK;
    }
    if (queue->size == MOS_PROCESS_MAX) {
        return MOS_ERR_NO_SPACE;
    }

    mos_sync_queue_push(queue, pid);
    pcb->state = MOS_PROC_BLOCKED;
    return MOS_ERR_BLOCKED;
}

mos_status_t mos_sem_signal(mos_kernel_t *kernel, mos_semaphore_t *sem) {
    mos_sync_wait_queue_t *queue;
    mos_pid_t pid;
    mos_pcb_t *pcb;

    if (kernel == NULL || sem == NULL) {
        return MOS_ERR_NULL;
    }
    if (mos_sync_require_kernel(kernel) != MOS_OK) {
        return mos_sync_require_kernel(kernel);
    }
    if (sem->waiters == NULL) {
        return MOS_ERR_STATE;
    }

    queue = (mos_sync_wait_queue_t *)sem->waiters;
    if (queue->size == 0U) {
        sem->value++;
        return MOS_OK;
    }

    pid = mos_sync_queue_pop(queue);
    pcb = mos_sync_find_process(kernel, pid);
    if (pcb == NULL || pcb->state != MOS_PROC_BLOCKED) {
        return MOS_ERR_STATE;
    }
    pcb->state = MOS_PROC_READY;
    return MOS_OK;
}

mos_status_t mos_mutex_init(mos_mutex_t *mutex) {
    mos_mutex_private_t *private_state;

    if (mutex == NULL) {
        return MOS_ERR_NULL;
    }

    private_state = (mos_mutex_private_t *)calloc(1U, sizeof(*private_state));
    if (private_state == NULL) {
        return MOS_ERR_NO_SPACE;
    }

    private_state->owner_pid = -1;
    mutex->locked = 0;
    mutex->waiters = private_state;
    return MOS_OK;
}

mos_status_t mos_mutex_lock(mos_kernel_t *kernel, mos_mutex_t *mutex, mos_pid_t pid) {
    mos_mutex_private_t *private_state;
    mos_pcb_t *pcb;

    if (kernel == NULL || mutex == NULL) {
        return MOS_ERR_NULL;
    }
    if (mos_sync_require_kernel(kernel) != MOS_OK) {
        return mos_sync_require_kernel(kernel);
    }
    if (mutex->waiters == NULL) {
        return MOS_ERR_STATE;
    }
    if (mos_sync_validate_process(kernel, pid, &pcb) != MOS_OK) {
        return mos_sync_validate_process(kernel, pid, &pcb);
    }

    private_state = (mos_mutex_private_t *)mutex->waiters;
    if (!mutex->locked) {
        mutex->locked = 1;
        private_state->owner_pid = pid;
        return MOS_OK;
    }
    if (private_state->owner_pid == pid) {
        return MOS_ERR_STATE;
    }
    if (private_state->waiters.size == MOS_PROCESS_MAX) {
        return MOS_ERR_NO_SPACE;
    }

    mos_sync_queue_push(&private_state->waiters, pid);
    pcb->state = MOS_PROC_BLOCKED;
    return MOS_ERR_BLOCKED;
}

mos_status_t mos_mutex_unlock(mos_kernel_t *kernel, mos_mutex_t *mutex, mos_pid_t pid) {
    mos_mutex_private_t *private_state;
    mos_pcb_t *pcb;

    if (kernel == NULL || mutex == NULL) {
        return MOS_ERR_NULL;
    }
    if (mos_sync_require_kernel(kernel) != MOS_OK) {
        return mos_sync_require_kernel(kernel);
    }
    if (mutex->waiters == NULL) {
        return MOS_ERR_STATE;
    }
    if (pid <= 0) {
        return MOS_ERR_RANGE;
    }

    private_state = (mos_mutex_private_t *)mutex->waiters;
    if (!mutex->locked || private_state->owner_pid != pid) {
        return MOS_ERR_STATE;
    }
    if (private_state->waiters.size == 0U) {
        mutex->locked = 0;
        private_state->owner_pid = -1;
        return MOS_OK;
    }

    pid = mos_sync_queue_pop(&private_state->waiters);
    pcb = mos_sync_find_process(kernel, pid);
    if (pcb == NULL || pcb->state != MOS_PROC_BLOCKED) {
        return MOS_ERR_STATE;
    }
    pcb->state = MOS_PROC_READY;
    private_state->owner_pid = pid;
    return MOS_OK;
}

mos_status_t mos_mutex_owner(const mos_mutex_t *mutex, mos_pid_t *pid_out) {
    const mos_mutex_private_t *private_state;

    if (mutex == NULL || pid_out == NULL) {
        return MOS_ERR_NULL;
    }
    if (mutex->waiters == NULL) {
        return MOS_ERR_STATE;
    }

    private_state = (const mos_mutex_private_t *)mutex->waiters;
    *pid_out = private_state->owner_pid;
    return MOS_OK;
}
