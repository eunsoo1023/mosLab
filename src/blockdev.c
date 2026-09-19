#include "minios/blockdev.h"

#include "mosvm/vm_blockdev.h"

static mos_status_t mos_blockdev_map_vm_status(vm_status_t status) {
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

static mos_status_t mos_blockdev_validate(mos_blockdev_t *device,
                                           size_t block_index,
                                           const void *buffer,
                                           size_t buffer_size) {
    if (device == NULL || buffer == NULL) {
        return MOS_ERR_NULL;
    }
    if (device->machine == NULL || device->block_size == 0U ||
        device->block_count == 0U) {
        return MOS_ERR_STATE;
    }
    if (block_index >= device->block_count) {
        return MOS_ERR_RANGE;
    }
    if (buffer_size < device->block_size) {
        return MOS_ERR_INVALID;
    }

    return MOS_OK;
}

/*
 * LAB7 준비 구현 안내
 * - VM raw block API를 감싼 뒤 miniOS 오류 코드로 변환한다.
 * - buffer_size, block_index, 초기화 여부를 먼저 검사한다.
 * - 상위 파일 시스템이 raw VM 세부사항에 의존하지 않도록 경계를 만든다.
 */

mos_status_t mos_blockdev_init(mos_blockdev_t *device, vm_machine_t *machine) {
    vm_machine_spec_t spec;
    vm_status_t vm_status;

    if (device == NULL || machine == NULL) {
        return MOS_ERR_NULL;
    }

    vm_status = vm_machine_get_spec(machine, &spec);
    if (vm_status != VM_OK) {
        return mos_blockdev_map_vm_status(vm_status);
    }
    if (spec.block_size == 0U || spec.block_count == 0U) {
        return MOS_ERR_INVALID;
    }

    device->machine = machine;
    device->block_size = spec.block_size;
    device->block_count = spec.block_count;
    return MOS_OK;
}

mos_status_t mos_blockdev_read(mos_blockdev_t *device, size_t block_index, void *buffer, size_t buffer_size) {
    mos_status_t status;

    status = mos_blockdev_validate(device, block_index, buffer, buffer_size);
    if (status != MOS_OK) {
        return status;
    }

    return mos_blockdev_map_vm_status(vm_block_read(
        device->machine, block_index, buffer, buffer_size));
}

mos_status_t mos_blockdev_write(mos_blockdev_t *device, size_t block_index, const void *buffer, size_t buffer_size) {
    mos_status_t status;

    status = mos_blockdev_validate(device, block_index, buffer, buffer_size);
    if (status != MOS_OK) {
        return status;
    }

    return mos_blockdev_map_vm_status(vm_block_write(
        device->machine, block_index, buffer, buffer_size));
}
