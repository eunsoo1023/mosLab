#include "minios/syscall.h"

#include <string.h>

#include "minios/fs.h"
#include "private/process_internal.h"

static mos_process_table_t *mos_syscall_table(mos_kernel_t *kernel) {
    return (mos_process_table_t *)kernel->private_state;
}

static const mos_process_table_t *mos_syscall_table_const(
    const mos_kernel_t *kernel) {
    return (const mos_process_table_t *)kernel->private_state;
}

static mos_status_t mos_syscall_require(const mos_kernel_t *kernel,
                                        const mos_process_table_t **table_out) {
    const mos_process_table_t *table;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }

    table = mos_syscall_table_const(kernel);
    if (!table->fs.initialized || !table->syscall.initialized) {
        return MOS_ERR_STATE;
    }

    *table_out = table;
    return MOS_OK;
}

static mos_status_t mos_syscall_validate_fd(
    const mos_process_table_t *table, mos_fd_t fd,
    const mos_fd_slot_private_t **slot_out) {
    if (fd < 0 || (size_t)fd >= MOS_SYSCALL_MAX_FDS) {
        return MOS_ERR_INVALID;
    }
    if (!table->syscall.slots[fd].used) {
        return MOS_ERR_INVALID;
    }

    *slot_out = &table->syscall.slots[fd];
    return MOS_OK;
}

/*
 * LAB8 구현 안내
 * - fd table을 설계하고 FS/console 같은 대상 객체와 연결한다.
 * - syscall은 policy 계층이므로 VM trap 번호를 어떤 서비스로 해석할지 명확히 한다.
 * - invalid fd, closed fd, buffer 범위 오류를 모두 상태 코드로 표현한다.
 */

mos_status_t mos_syscall_init(mos_kernel_t *kernel) {
    mos_process_table_t *table;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }

    table = mos_syscall_table(kernel);
    if (!table->fs.initialized) {
        return MOS_ERR_STATE;
    }

    memset(&table->syscall, 0, sizeof(table->syscall));
    table->syscall.initialized = 1;
    return MOS_OK;
}

mos_status_t mos_sys_open(mos_kernel_t *kernel, const char *path, mos_fd_t *fd_out) {
    const mos_process_table_t *table_const;
    mos_process_table_t *table;
    mos_file_stat_t stat;
    size_t path_length;
    size_t index;
    mos_status_t status;

    if (kernel == NULL || path == NULL || fd_out == NULL) {
        return MOS_ERR_NULL;
    }
    status = mos_syscall_require(kernel, &table_const);
    if (status != MOS_OK) {
        return status;
    }
    status = mos_fs_stat(kernel, path, &stat);
    if (status != MOS_OK) {
        return status;
    }

    path_length = strlen(path);
    if (path_length >= MOS_FS_MAX_PATH) {
        return MOS_ERR_RANGE;
    }
    for (index = 0U; index < MOS_SYSCALL_MAX_FDS; index++) {
        if (!table_const->syscall.slots[index].used) {
            break;
        }
    }
    if (index == MOS_SYSCALL_MAX_FDS) {
        return MOS_ERR_NO_SPACE;
    }

    table = mos_syscall_table(kernel);
    table->syscall.slots[index].used = 1;
    table->syscall.slots[index].offset = 0U;
    memcpy(table->syscall.slots[index].path, path, path_length + 1U);
    *fd_out = (mos_fd_t)index;
    return MOS_OK;
}

mos_status_t mos_sys_write(mos_kernel_t *kernel, mos_fd_t fd, const void *buffer, size_t size, size_t *written_out) {
    const mos_process_table_t *table;
    const mos_fd_slot_private_t *slot;
    mos_status_t status;

    if (kernel == NULL || written_out == NULL) {
        return MOS_ERR_NULL;
    }
    *written_out = 0U;
    if (size > 0U && buffer == NULL) {
        return MOS_ERR_NULL;
    }
    status = mos_syscall_require(kernel, &table);
    if (status != MOS_OK) {
        return status;
    }
    status = mos_syscall_validate_fd(table, fd, &slot);
    if (status != MOS_OK) {
        return status;
    }

    status = mos_fs_write(kernel, slot->path, buffer, size);
    if (status == MOS_OK) {
        *written_out = size;
    }
    return status;
}

mos_status_t mos_sys_read(mos_kernel_t *kernel, mos_fd_t fd, void *buffer, size_t buffer_size, size_t *read_out) {
    const mos_process_table_t *table;
    const mos_fd_slot_private_t *slot;
    mos_status_t status;

    if (kernel == NULL || read_out == NULL) {
        return MOS_ERR_NULL;
    }
    *read_out = 0U;
    if (buffer_size > 0U && buffer == NULL) {
        return MOS_ERR_NULL;
    }
    status = mos_syscall_require(kernel, &table);
    if (status != MOS_OK) {
        return status;
    }
    status = mos_syscall_validate_fd(table, fd, &slot);
    if (status != MOS_OK) {
        return status;
    }

    return mos_fs_read(kernel, slot->path, buffer, buffer_size, read_out);
}

mos_status_t mos_sys_close(mos_kernel_t *kernel, mos_fd_t fd) {
    const mos_process_table_t *table;
    mos_fd_slot_private_t *slot;
    mos_status_t status;

    status = mos_syscall_require(kernel, &table);
    if (status != MOS_OK) {
        return status;
    }
    if (fd < 0 || (size_t)fd >= MOS_SYSCALL_MAX_FDS ||
        !table->syscall.slots[fd].used) {
        return MOS_ERR_INVALID;
    }

    slot = &mos_syscall_table(kernel)->syscall.slots[fd];
    memset(slot, 0, sizeof(*slot));
    return MOS_OK;
}
