#include "minios/kernel.h"

#include <stdlib.h>
#include <string.h>

#include "private/process_internal.h"

static mos_status_t mos_kernel_map_vm_status(vm_status_t status) {
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

/*
 * LAB1 구현 안내
 * - mos_kernel_boot()에서 VM 생성, 커널 상태 전이, 하위 모듈 초기화 순서를 설계한다.
 * - boot를 두 번 호출하거나 shutdown 이후 다시 호출하는 정책을 명확히 정한다.
 * - 모든 포인터 인자와 VM API 반환값을 검사하고 실패 시 상태 코드를 반환한다.
 * - Copilot에게 "커널 lifecycle 상태 전이표를 기준으로 C99 구현"처럼 요청하면 좋다.
 */

mos_status_t mos_kernel_boot(mos_kernel_t *kernel) {
    vm_status_t vm_status;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }

    memset(kernel, 0, sizeof(*kernel));
    kernel->state = MOS_KERNEL_OFF;

    vm_status = vm_machine_create(&kernel->machine);
    if (vm_status != VM_OK) {
        return mos_kernel_map_vm_status(vm_status);
    }

    kernel->state = MOS_KERNEL_BOOTED;
    return MOS_OK;
}

mos_status_t mos_kernel_shutdown(mos_kernel_t *kernel) {
    mos_process_table_t *table;
    vm_status_t vm_status;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED) {
        return MOS_ERR_STATE;
    }

    vm_status = vm_machine_destroy(&kernel->machine);
    if (vm_status != VM_OK) {
        return mos_kernel_map_vm_status(vm_status);
    }

    table = (mos_process_table_t *)kernel->private_state;
    if (table != NULL) {
        mos_vm_mapping_node_t *mapping = table->vm.head;

        while (mapping != NULL) {
            mos_vm_mapping_node_t *next = mapping->next;

            free(mapping);
            mapping = next;
        }
        free(table->memory.bitmap);
        free(table);
    }
    kernel->private_state = NULL;
    kernel->state = MOS_KERNEL_SHUTDOWN;
    return MOS_OK;
}

mos_status_t mos_kernel_tick(mos_kernel_t *kernel) {
    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED) {
        return MOS_ERR_STATE;
    }

    return mos_kernel_map_vm_status(vm_machine_tick(&kernel->machine));
}

mos_status_t mos_kernel_ticks(const mos_kernel_t *kernel, uint64_t *ticks_out) {
    if (kernel == NULL || ticks_out == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED) {
        return MOS_ERR_STATE;
    }

    return mos_kernel_map_vm_status(
        vm_machine_get_ticks(&kernel->machine, ticks_out));
}
