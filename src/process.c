#include "minios/process.h"

#include "private/process_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static mos_status_t mos_process_map_vm_status(vm_status_t status) {
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

static mos_process_table_t *mos_process_table(mos_kernel_t *kernel) {
    return (mos_process_table_t *)kernel->private_state;
}

static const mos_process_table_t *mos_process_table_const(
    const mos_kernel_t *kernel) {
    return (const mos_process_table_t *)kernel->private_state;
}

static mos_status_t mos_process_require_table(const mos_kernel_t *kernel) {
    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED) {
        return MOS_ERR_STATE;
    }
    if (kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }

    return MOS_OK;
}

static mos_pcb_t *mos_process_find(mos_process_table_t *table, mos_pid_t pid) {
    size_t index;

    for (index = 0U; index < MOS_PROCESS_MAX; index++) {
        if (table->entries[index].state != MOS_PROC_UNUSED &&
            table->entries[index].pid == pid) {
            return &table->entries[index];
        }
    }

    return NULL;
}

static const mos_pcb_t *mos_process_find_const(
    const mos_process_table_t *table, mos_pid_t pid) {
    size_t index;

    for (index = 0U; index < MOS_PROCESS_MAX; index++) {
        if (table->entries[index].state != MOS_PROC_UNUSED &&
            table->entries[index].pid == pid) {
            return &table->entries[index];
        }
    }

    return NULL;
}

/*
 * LAB2 구현 안내
 * - PID 발급, PCB 저장소, 상태 전이를 직접 설계한다. tests는 public API만 호출한다.
 * - 고정 배열을 써도 되고 private 자료구조를 추가해도 된다.
 * - vm_cpu_context_init() 실패를 반드시 처리하고, PID 범위 검사를 빠뜨리지 않는다.
 * - 정답 구조체 필드명을 맞추려 하지 말고 동작 계약을 만족시키는 설계를 우선한다.
 */

mos_status_t mos_process_table_init(mos_kernel_t *kernel) {
    mos_process_table_t *table;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED) {
        return MOS_ERR_STATE;
    }

    if (kernel->private_state == NULL) {
        table = (mos_process_table_t *)calloc(1U, sizeof(*table));
        if (table == NULL) {
            return MOS_ERR_NO_SPACE;
        }
        kernel->private_state = table;
    } else {
        table = mos_process_table(kernel);
        memset(table->entries, 0, sizeof(table->entries));
        memset(&table->scheduler, 0, sizeof(table->scheduler));
    }

    table->next_pid = 1;
    return MOS_OK;
}

mos_status_t mos_process_create(mos_kernel_t *kernel, const vm_program_t *program, mos_pid_t *pid_out) {
    mos_process_table_t *table;
    vm_cpu_context_t context;
    size_t index;
    vm_status_t vm_status;

    if (kernel == NULL || program == NULL || pid_out == NULL) {
        return MOS_ERR_NULL;
    }
    if (mos_process_require_table(kernel) != MOS_OK) {
        return mos_process_require_table(kernel);
    }

    table = mos_process_table(kernel);
    if (table->next_pid <= 0) {
        return MOS_ERR_RANGE;
    }

    for (index = 0U; index < MOS_PROCESS_MAX; index++) {
        if (table->entries[index].state == MOS_PROC_UNUSED) {
            break;
        }
    }
    if (index == MOS_PROCESS_MAX) {
        return MOS_ERR_NO_SPACE;
    }

    vm_status = vm_cpu_context_init(&context, program);
    if (vm_status != VM_OK) {
        return mos_process_map_vm_status(vm_status);
    }

    table->entries[index].pid = table->next_pid;
    table->entries[index].state = MOS_PROC_READY;
    table->entries[index].context = context;
    table->count++;
    *pid_out = table->entries[index].pid;
    if (table->next_pid < INT_MAX) {
        table->next_pid++;
    } else {
        table->next_pid = -1;
    }

    return MOS_OK;
}

mos_status_t mos_process_info(const mos_kernel_t *kernel, mos_pid_t pid, mos_process_info_t *info_out) {
    const mos_process_table_t *table;
    const mos_pcb_t *pcb;

    if (kernel == NULL || info_out == NULL) {
        return MOS_ERR_NULL;
    }
    if (pid <= 0) {
        return MOS_ERR_RANGE;
    }
    if (mos_process_require_table(kernel) != MOS_OK) {
        return mos_process_require_table(kernel);
    }

    table = mos_process_table_const(kernel);
    pcb = mos_process_find_const(table, pid);
    if (pcb == NULL) {
        return MOS_ERR_NOT_FOUND;
    }

    info_out->pid = pcb->pid;
    info_out->state = pcb->state;
    return MOS_OK;
}

mos_status_t mos_process_exit(mos_kernel_t *kernel, mos_pid_t pid) {
    mos_process_table_t *table;
    mos_pcb_t *pcb;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (pid <= 0) {
        return MOS_ERR_RANGE;
    }
    if (mos_process_require_table(kernel) != MOS_OK) {
        return mos_process_require_table(kernel);
    }

    table = mos_process_table(kernel);
    pcb = mos_process_find(table, pid);
    if (pcb == NULL) {
        return MOS_ERR_NOT_FOUND;
    }
    if (pcb->state == MOS_PROC_EXITED) {
        return MOS_ERR_STATE;
    }

    pcb->state = MOS_PROC_EXITED;
    return MOS_OK;
}

mos_status_t mos_process_count(const mos_kernel_t *kernel, size_t *count_out) {
    const mos_process_table_t *table;

    if (kernel == NULL || count_out == NULL) {
        return MOS_ERR_NULL;
    }
    if (mos_process_require_table(kernel) != MOS_OK) {
        return mos_process_require_table(kernel);
    }

    table = mos_process_table_const(kernel);
    *count_out = table->count;
    return MOS_OK;
}
