#include "minios/vm.h"

#include <stdint.h>
#include <stdlib.h>

#include "private/process_internal.h"

static mos_process_table_t *mos_vm_table(mos_kernel_t *kernel) {
    return (mos_process_table_t *)kernel->private_state;
}

static const mos_process_table_t *mos_vm_table_const(
    const mos_kernel_t *kernel) {
    return (const mos_process_table_t *)kernel->private_state;
}

static mos_status_t mos_vm_require(const mos_kernel_t *kernel,
                                   const mos_process_table_t **table_out) {
    const mos_process_table_t *table;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }

    table = mos_vm_table_const(kernel);
    if (!table->memory.initialized || !table->vm.initialized) {
        return MOS_ERR_STATE;
    }

    *table_out = table;
    return MOS_OK;
}

static mos_pcb_t *mos_vm_find_process(mos_kernel_t *kernel, mos_pid_t pid) {
    mos_process_table_t *table = mos_vm_table(kernel);
    size_t index;

    for (index = 0U; index < MOS_PROCESS_MAX; index++) {
        if (table->entries[index].state != MOS_PROC_UNUSED &&
            table->entries[index].pid == pid) {
            return &table->entries[index];
        }
    }

    return NULL;
}

static mos_vm_mapping_node_t *mos_vm_find_mapping(
    mos_vm_mapping_node_t *head, mos_pid_t pid, mos_vaddr_t virtual_page) {
    mos_vm_mapping_node_t *mapping = head;

    while (mapping != NULL) {
        if (mapping->pid == pid && mapping->virtual_page == virtual_page) {
            return mapping;
        }
        mapping = mapping->next;
    }

    return NULL;
}

static void mos_vm_clear_mappings(mos_vm_private_t *vm) {
    mos_vm_mapping_node_t *mapping = vm->head;

    while (mapping != NULL) {
        mos_vm_mapping_node_t *next = mapping->next;

        free(mapping);
        mapping = next;
    }
    vm->head = NULL;
    vm->mapping_count = 0U;
}

static int mos_vm_frame_allocated(const mos_memory_private_t *memory,
                                  mos_frame_t frame) {
    size_t byte = frame / 8U;
    unsigned int bit = (unsigned int)(frame % 8U);

    return (memory->bitmap[byte] & (unsigned char)(1U << bit)) != 0U;
}

/*
 * LAB6 구현 안내
 * - 가상 페이지 번호와 offset을 분리하고 물리 frame 주소로 변환한다.
 * - page table 구조, writable bit, unmapped page 오류 처리는 학생이 설계한다.
 * - VM 라이브러리의 raw memory API와 혼동하지 말고 주소 변환 정책만 구현한다.
 */

mos_status_t mos_vm_init(mos_kernel_t *kernel) {
    mos_process_table_t *table;
    vm_machine_spec_t spec;
    vm_status_t vm_status;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }
    table = mos_vm_table(kernel);
    if (!table->memory.initialized) {
        return MOS_ERR_STATE;
    }

    vm_status = vm_machine_get_spec(&kernel->machine, &spec);
    if (vm_status != VM_OK) {
        return vm_status == VM_ERR_NULL ? MOS_ERR_NULL : MOS_ERR_STATE;
    }
    if (spec.page_size != MOS_VM_PAGE_SIZE) {
        return MOS_ERR_INVALID;
    }

    if (table->vm.initialized) {
        mos_vm_clear_mappings(&table->vm);
    }
    table->vm.initialized = 1;
    return MOS_OK;
}

mos_status_t mos_vm_map(mos_kernel_t *kernel, mos_pid_t pid, mos_vaddr_t virtual_page, mos_frame_t frame, int writable) {
    const mos_process_table_t *table_const;
    mos_process_table_t *table;
    mos_vm_mapping_node_t *mapping;
    mos_status_t status;

    status = mos_vm_require(kernel, &table_const);
    if (status != MOS_OK) {
        return status;
    }
    if (pid <= 0) {
        return MOS_ERR_RANGE;
    }
    if (mos_vm_find_process(kernel, pid) == NULL) {
        return MOS_ERR_NOT_FOUND;
    }
    if (frame >= table_const->memory.total_frames) {
        return MOS_ERR_RANGE;
    }
    if (!mos_vm_frame_allocated(&table_const->memory, frame)) {
        return MOS_ERR_STATE;
    }
    if (writable != 0 && writable != 1) {
        return MOS_ERR_INVALID;
    }

    table = mos_vm_table(kernel);
    if (mos_vm_find_mapping(table->vm.head, pid, virtual_page) != NULL) {
        return MOS_ERR_STATE;
    }

    mapping = (mos_vm_mapping_node_t *)calloc(1U, sizeof(*mapping));
    if (mapping == NULL) {
        return MOS_ERR_NO_SPACE;
    }
    mapping->pid = pid;
    mapping->virtual_page = virtual_page;
    mapping->frame = frame;
    mapping->writable = writable;
    mapping->next = table->vm.head;
    table->vm.head = mapping;
    table->vm.mapping_count++;
    return MOS_OK;
}

mos_status_t mos_vm_unmap(mos_kernel_t *kernel, mos_pid_t pid, mos_vaddr_t virtual_page) {
    mos_process_table_t *table;
    mos_vm_mapping_node_t **link;
    mos_status_t status;

    status = mos_vm_require(kernel, (const mos_process_table_t **)&table);
    if (status != MOS_OK) {
        return status;
    }
    if (pid <= 0) {
        return MOS_ERR_RANGE;
    }
    if (mos_vm_find_process(kernel, pid) == NULL) {
        return MOS_ERR_NOT_FOUND;
    }

    link = &table->vm.head;
    while (*link != NULL) {
        if ((*link)->pid == pid && (*link)->virtual_page == virtual_page) {
            mos_vm_mapping_node_t *mapping = *link;

            *link = mapping->next;
            free(mapping);
            table->vm.mapping_count--;
            return MOS_OK;
        }
        link = &(*link)->next;
    }

    return MOS_ERR_NOT_FOUND;
}

mos_status_t mos_vm_translate(const mos_kernel_t *kernel, mos_pid_t pid, mos_vaddr_t virtual_address, mos_paddr_t *physical_out) {
    const mos_process_table_t *table;
    const mos_vm_mapping_node_t *mapping;
    mos_vaddr_t virtual_page;
    mos_vaddr_t offset;
    mos_paddr_t physical;
    mos_status_t status;

    if (kernel == NULL || physical_out == NULL) {
        return MOS_ERR_NULL;
    }
    status = mos_vm_require(kernel, &table);
    if (status != MOS_OK) {
        return status;
    }
    if (pid <= 0) {
        return MOS_ERR_RANGE;
    }
    if (mos_vm_find_process((mos_kernel_t *)kernel, pid) == NULL) {
        return MOS_ERR_NOT_FOUND;
    }

    virtual_page = virtual_address / MOS_VM_PAGE_SIZE;
    offset = virtual_address % MOS_VM_PAGE_SIZE;
    mapping = mos_vm_find_mapping(table->vm.head, pid, virtual_page);
    if (mapping == NULL) {
        return MOS_ERR_NOT_FOUND;
    }
    if (mapping->frame > (SIZE_MAX - offset) / MOS_VM_PAGE_SIZE) {
        return MOS_ERR_RANGE;
    }

    physical = mapping->frame * MOS_VM_PAGE_SIZE + offset;
    *physical_out = physical;
    return MOS_OK;
}
