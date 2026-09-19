#include "minios/memory.h"

#include <limits.h>
#include <stdlib.h>

#include "private/process_internal.h"

static mos_status_t mos_memory_map_vm_status(vm_status_t status) {
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

static mos_process_table_t *mos_memory_table(mos_kernel_t *kernel) {
    return (mos_process_table_t *)kernel->private_state;
}

static const mos_process_table_t *mos_memory_table_const(
    const mos_kernel_t *kernel) {
    return (const mos_process_table_t *)kernel->private_state;
}

static mos_status_t mos_memory_require(const mos_kernel_t *kernel,
                                       const mos_process_table_t **table_out) {
    const mos_process_table_t *table;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }

    table = mos_memory_table_const(kernel);
    if (!table->memory.initialized) {
        return MOS_ERR_STATE;
    }

    *table_out = table;
    return MOS_OK;
}

static int mos_memory_bit_is_set(const mos_memory_private_t *memory,
                                 mos_frame_t frame) {
    size_t byte = frame / CHAR_BIT;
    unsigned int bit = (unsigned int)(frame % CHAR_BIT);

    return (memory->bitmap[byte] & (unsigned char)(1U << bit)) != 0U;
}

static void mos_memory_set_bit(mos_memory_private_t *memory,
                               mos_frame_t frame) {
    size_t byte = frame / CHAR_BIT;
    unsigned int bit = (unsigned int)(frame % CHAR_BIT);

    memory->bitmap[byte] |= (unsigned char)(1U << bit);
}

static void mos_memory_clear_bit(mos_memory_private_t *memory,
                                 mos_frame_t frame) {
    size_t byte = frame / CHAR_BIT;
    unsigned int bit = (unsigned int)(frame % CHAR_BIT);

    memory->bitmap[byte] &= (unsigned char)~(1U << bit);
}

/*
 * LAB5 구현 안내
 * - VM spec의 frame_count/page_size를 읽고 free frame 집합을 구성한다.
 * - 중복 free, 범위 밖 frame, allocator 미초기화 상태를 구분해 반환한다.
 * - bitmap, stack, free list 중 하나를 선택하되 public API 밖으로 노출하지 않는다.
 */

mos_status_t mos_memory_init(mos_kernel_t *kernel) {
    vm_machine_spec_t spec;
    mos_process_table_t *table;
    unsigned char *bitmap;
    size_t bitmap_bytes;
    vm_status_t vm_status;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED) {
        return MOS_ERR_STATE;
    }

    vm_status = vm_machine_get_spec(&kernel->machine, &spec);
    if (vm_status != VM_OK) {
        return mos_memory_map_vm_status(vm_status);
    }
    if (spec.frame_count == 0U) {
        return MOS_ERR_INVALID;
    }

    bitmap_bytes = (spec.frame_count + CHAR_BIT - 1U) / CHAR_BIT;
    bitmap = (unsigned char *)calloc(bitmap_bytes, sizeof(*bitmap));
    if (bitmap == NULL) {
        return MOS_ERR_NO_SPACE;
    }

    if (kernel->private_state == NULL) {
        table = (mos_process_table_t *)calloc(1U, sizeof(*table));
        if (table == NULL) {
            free(bitmap);
            return MOS_ERR_NO_SPACE;
        }
        kernel->private_state = table;
    } else {
        table = mos_memory_table(kernel);
    }

    free(table->memory.bitmap);
    table->memory.total_frames = spec.frame_count;
    table->memory.free_frames = spec.frame_count;
    table->memory.bitmap_bytes = bitmap_bytes;
    table->memory.bitmap = bitmap;
    table->memory.initialized = 1;
    return MOS_OK;
}

mos_status_t mos_frame_alloc(mos_kernel_t *kernel, mos_frame_t *frame_out) {
    const mos_process_table_t *table_const;
    mos_process_table_t *table;
    mos_memory_private_t *memory;
    mos_frame_t frame;
    mos_status_t status;

    if (kernel == NULL || frame_out == NULL) {
        return MOS_ERR_NULL;
    }

    status = mos_memory_require(kernel, &table_const);
    if (status != MOS_OK) {
        return status;
    }
    if (table_const->memory.free_frames == 0U) {
        return MOS_ERR_NO_SPACE;
    }

    table = mos_memory_table(kernel);
    memory = &table->memory;
    for (frame = 0U; frame < memory->total_frames; frame++) {
        if (!mos_memory_bit_is_set(memory, frame)) {
            mos_memory_set_bit(memory, frame);
            memory->free_frames--;
            *frame_out = frame;
            return MOS_OK;
        }
    }

    return MOS_ERR_STATE;
}

mos_status_t mos_frame_free(mos_kernel_t *kernel, mos_frame_t frame) {
    const mos_process_table_t *table_const;
    mos_process_table_t *table;
    mos_memory_private_t *memory;
    mos_status_t status;

    status = mos_memory_require(kernel, &table_const);
    if (status != MOS_OK) {
        return status;
    }
    if (frame >= table_const->memory.total_frames) {
        return MOS_ERR_RANGE;
    }
    if (!mos_memory_bit_is_set(&table_const->memory, frame)) {
        return MOS_ERR_STATE;
    }

    table = mos_memory_table(kernel);
    memory = &table->memory;
    mos_memory_clear_bit(memory, frame);
    memory->free_frames++;
    return MOS_OK;
}

mos_status_t mos_memory_get_stats(const mos_kernel_t *kernel, mos_memory_stats_t *stats_out) {
    const mos_process_table_t *table;
    mos_status_t status;

    if (kernel == NULL || stats_out == NULL) {
        return MOS_ERR_NULL;
    }

    status = mos_memory_require(kernel, &table);
    if (status != MOS_OK) {
        return status;
    }

    stats_out->total_frames = table->memory.total_frames;
    stats_out->free_frames = table->memory.free_frames;
    return MOS_OK;
}
