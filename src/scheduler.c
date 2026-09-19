#include "minios/scheduler.h"

#include <stddef.h>

#include "private/process_internal.h"

static mos_status_t mos_scheduler_map_vm_status(vm_status_t status) {
    switch (status) {
        case VM_OK:
            return MOS_OK;
        case VM_ERR_NULL:
            return MOS_ERR_NULL;
        case VM_ERR_RANGE:
            return MOS_ERR_RANGE;
        case VM_ERR_STATE:
            return MOS_ERR_STATE;
        case VM_ERR_FULL:
            return MOS_ERR_NO_SPACE;
        case VM_ERR_BAD_ARG:
            return MOS_ERR_INVALID;
    }

    return MOS_ERR_INVALID;
}

static mos_process_table_t *mos_scheduler_table(mos_kernel_t *kernel) {
    return (mos_process_table_t *)kernel->private_state;
}

static const mos_process_table_t *mos_scheduler_table_const(
    const mos_kernel_t *kernel) {
    return (const mos_process_table_t *)kernel->private_state;
}

static mos_status_t mos_scheduler_require(const mos_kernel_t *kernel) {
    const mos_process_table_t *table;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }

    table = mos_scheduler_table_const(kernel);
    if (!table->scheduler.initialized) {
        return MOS_ERR_STATE;
    }

    return MOS_OK;
}

static mos_pcb_t *mos_scheduler_find(mos_process_table_t *table, mos_pid_t pid) {
    size_t index;

    for (index = 0U; index < MOS_PROCESS_MAX; index++) {
        if (table->entries[index].state != MOS_PROC_UNUSED &&
            table->entries[index].pid == pid) {
            return &table->entries[index];
        }
    }

    return NULL;
}

static int mos_scheduler_queue_contains(
    const mos_scheduler_private_t *scheduler, mos_pid_t pid) {
    size_t offset;

    for (offset = 0U; offset < scheduler->size; offset++) {
        size_t index = (scheduler->head + offset) % MOS_PROCESS_MAX;

        if (scheduler->queue[index] == pid) {
            return 1;
        }
    }

    return 0;
}

static void mos_scheduler_queue_push(mos_scheduler_private_t *scheduler,
                                     mos_pid_t pid) {
    size_t index = (scheduler->head + scheduler->size) % MOS_PROCESS_MAX;

    scheduler->queue[index] = pid;
    scheduler->size++;
}

static mos_pid_t mos_scheduler_queue_pop(mos_scheduler_private_t *scheduler) {
    mos_pid_t pid = scheduler->queue[scheduler->head];

    scheduler->head = (scheduler->head + 1U) % MOS_PROCESS_MAX;
    scheduler->size--;
    return pid;
}

/*
 * LAB3 구현 안내
 * - ready queue와 round-robin 순환 규칙을 직접 구현한다.
 * - scheduler는 VM CPU step만 사용하고 PCB 저장 정책을 대신 제공하지 않는다.
 * - 빈 queue, 중복 enqueue, 종료된 PID 처리 정책을 상태 코드로 표현한다.
 */

mos_status_t mos_scheduler_init(mos_kernel_t *kernel, const mos_scheduler_config_t *config) {
    mos_process_table_t *table;

    if (kernel == NULL || config == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }
    if (config->time_slice_ticks == 0U) {
        return MOS_ERR_INVALID;
    }

    table = mos_scheduler_table(kernel);
    table->scheduler.time_slice_ticks = config->time_slice_ticks;
    table->scheduler.head = 0U;
    table->scheduler.size = 0U;
    table->scheduler.current_pid = -1;
    table->scheduler.dispatch_count = 0U;
    table->scheduler.initialized = 1;
    return MOS_OK;
}

mos_status_t mos_scheduler_enqueue(mos_kernel_t *kernel, mos_pid_t pid) {
    mos_process_table_t *table;
    mos_pcb_t *pcb;

    if (pid <= 0) {
        return MOS_ERR_RANGE;
    }
    if (mos_scheduler_require(kernel) != MOS_OK) {
        return mos_scheduler_require(kernel);
    }

    table = mos_scheduler_table(kernel);
    pcb = mos_scheduler_find(table, pid);
    if (pcb == NULL) {
        return MOS_ERR_NOT_FOUND;
    }
    if (pcb->state != MOS_PROC_READY) {
        return MOS_ERR_STATE;
    }
    if (mos_scheduler_queue_contains(&table->scheduler, pid)) {
        return MOS_ERR_STATE;
    }
    if (table->scheduler.size == MOS_PROCESS_MAX) {
        return MOS_ERR_NO_SPACE;
    }

    mos_scheduler_queue_push(&table->scheduler, pid);
    return MOS_OK;
}

mos_status_t mos_scheduler_next(mos_kernel_t *kernel, mos_pid_t *pid_out) {
    mos_process_table_t *table;
    mos_scheduler_private_t *scheduler;
    mos_pid_t pid;

    if (kernel == NULL || pid_out == NULL) {
        return MOS_ERR_NULL;
    }
    if (mos_scheduler_require(kernel) != MOS_OK) {
        return mos_scheduler_require(kernel);
    }

    table = mos_scheduler_table(kernel);
    scheduler = &table->scheduler;
    if (scheduler->size == 0U) {
        return MOS_ERR_NOT_FOUND;
    }

    pid = mos_scheduler_queue_pop(scheduler);
    mos_scheduler_queue_push(scheduler, pid);
    *pid_out = pid;
    return MOS_OK;
}

mos_status_t mos_scheduler_run_next(mos_kernel_t *kernel, mos_scheduler_step_result_t *result_out) {
    mos_process_table_t *table;
    mos_scheduler_private_t *scheduler;
    mos_pcb_t *pcb;
    mos_pid_t pid;
    vm_cpu_event_t cpu_event;
    mos_scheduler_event_t scheduler_event;
    mos_process_state_t new_state;
    unsigned int step;
    vm_status_t vm_status;

    if (kernel == NULL || result_out == NULL) {
        return MOS_ERR_NULL;
    }
    if (mos_scheduler_require(kernel) != MOS_OK) {
        return mos_scheduler_require(kernel);
    }

    table = mos_scheduler_table(kernel);
    scheduler = &table->scheduler;
    if (scheduler->size == 0U) {
        return MOS_ERR_NOT_FOUND;
    }

    pid = mos_scheduler_queue_pop(scheduler);
    pcb = mos_scheduler_find(table, pid);
    if (pcb == NULL || pcb->state != MOS_PROC_READY) {
        return MOS_ERR_STATE;
    }

    pcb->state = MOS_PROC_RUNNING;
    scheduler->current_pid = pid;
    scheduler->dispatch_count++;
    scheduler_event = MOS_SCHED_EVENT_TIME_SLICE;
    new_state = MOS_PROC_READY;

    for (step = 0U; step < scheduler->time_slice_ticks; step++) {
        vm_status = vm_cpu_step(&pcb->context, &cpu_event);
        if (vm_status != VM_OK) {
            pcb->state = MOS_PROC_READY;
            mos_scheduler_queue_push(scheduler, pid);
            scheduler->current_pid = -1;
            return mos_scheduler_map_vm_status(vm_status);
        }

        if (cpu_event.type == VM_CPU_EVENT_NONE) {
            continue;
        }
        if (cpu_event.type == VM_CPU_EVENT_YIELD) {
            scheduler_event = MOS_SCHED_EVENT_YIELD;
        } else if (cpu_event.type == VM_CPU_EVENT_HALT) {
            scheduler_event = MOS_SCHED_EVENT_HALTED;
            new_state = MOS_PROC_EXITED;
        } else if (cpu_event.type == VM_CPU_EVENT_TRAP) {
            scheduler_event = MOS_SCHED_EVENT_TRAP;
        } else {
            pcb->state = MOS_PROC_READY;
            mos_scheduler_queue_push(scheduler, pid);
            scheduler->current_pid = -1;
            return MOS_ERR_INVALID;
        }
        break;
    }

    pcb->state = new_state;
    if (new_state == MOS_PROC_READY) {
        mos_scheduler_queue_push(scheduler, pid);
    }
    scheduler->current_pid = -1;
    result_out->pid = pid;
    result_out->event = scheduler_event;
    result_out->new_state = new_state;
    return MOS_OK;
}

mos_status_t mos_scheduler_get_stats(const mos_kernel_t *kernel, mos_scheduler_stats_t *stats_out) {
    const mos_process_table_t *table;

    if (kernel == NULL || stats_out == NULL) {
        return MOS_ERR_NULL;
    }
    if (mos_scheduler_require(kernel) != MOS_OK) {
        return mos_scheduler_require(kernel);
    }

    table = mos_scheduler_table_const(kernel);
    stats_out->dispatch_count = table->scheduler.dispatch_count;
    stats_out->current_pid = table->scheduler.current_pid;
    return MOS_OK;
}
