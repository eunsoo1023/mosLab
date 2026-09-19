#include "minios/system.h"

#include <string.h>

#include "minios/blockdev.h"
#include "minios/fs.h"
#include "minios/memory.h"
#include "minios/scheduler.h"
#include "minios/shell.h"
#include "minios/syscall.h"
#include "minios/vm.h"
#include "private/process_internal.h"

static mos_status_t mos_system_fail(mos_kernel_t *kernel,
                                     mos_system_report_t *report,
                                     mos_status_t status) {
    report->status = status;
    if (kernel != NULL && kernel->state == MOS_KERNEL_BOOTED) {
        (void)mos_kernel_shutdown(kernel);
    }
    return status;
}

/*
 * LAB10 구현 안내
 * - LAB1~LAB9 초기화 순서를 통합하고 실패 시 어디까지 성공했는지 report에 기록한다.
 * - integration demo는 public API만 조합해야 하며 private 내부 상태를 직접 만지지 않는다.
 */

mos_status_t mos_system_boot(mos_kernel_t *kernel, mos_system_report_t *report_out) {
    mos_scheduler_config_t scheduler_config = {2U};
    mos_process_table_t *table;
    mos_status_t status;

    if (report_out == NULL || kernel == NULL) {
        return MOS_ERR_NULL;
    }
    memset(report_out, 0, sizeof(*report_out));

    status = mos_kernel_boot(kernel);
    if (status != MOS_OK) {
        return mos_system_fail(kernel, report_out, status);
    }
    report_out->labs_ready = 1U;

    status = mos_process_table_init(kernel);
    if (status != MOS_OK) {
        return mos_system_fail(kernel, report_out, status);
    }
    report_out->labs_ready = 2U;

    status = mos_scheduler_init(kernel, &scheduler_config);
    if (status != MOS_OK) {
        return mos_system_fail(kernel, report_out, status);
    }
    report_out->labs_ready = 3U;

    report_out->labs_ready = 4U;

    status = mos_memory_init(kernel);
    if (status != MOS_OK) {
        return mos_system_fail(kernel, report_out, status);
    }
    report_out->labs_ready = 5U;

    status = mos_vm_init(kernel);
    if (status != MOS_OK) {
        return mos_system_fail(kernel, report_out, status);
    }
    report_out->labs_ready = 6U;

    table = (mos_process_table_t *)kernel->private_state;
    status = mos_blockdev_init(&table->blockdev, &kernel->machine);
    if (status != MOS_OK) {
        return mos_system_fail(kernel, report_out, status);
    }
    status = mos_fs_init(kernel, &table->blockdev);
    if (status != MOS_OK) {
        return mos_system_fail(kernel, report_out, status);
    }
    report_out->labs_ready = 7U;

    status = mos_syscall_init(kernel);
    if (status != MOS_OK) {
        return mos_system_fail(kernel, report_out, status);
    }
    report_out->labs_ready = 8U;

    status = mos_shell_init(kernel);
    if (status != MOS_OK) {
        return mos_system_fail(kernel, report_out, status);
    }
    report_out->labs_ready = 9U;
    report_out->labs_ready = 10U;
    report_out->status = MOS_OK;
    return MOS_OK;
}

mos_status_t mos_system_run_demo(mos_kernel_t *kernel, mos_system_report_t *report_out) {
    mos_inode_t inode;
    mos_fd_t fd;
    size_t written;
    char output[128];
    mos_status_t status;

    if (kernel == NULL || report_out == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED) {
        report_out->status = MOS_ERR_STATE;
        return MOS_ERR_STATE;
    }

    status = mos_fs_create(kernel, "/integration", &inode);
    if (status != MOS_OK) {
        report_out->status = status;
        return status;
    }
    status = mos_sys_open(kernel, "/integration", &fd);
    if (status != MOS_OK) {
        report_out->status = status;
        return status;
    }
    status = mos_sys_write(kernel, fd, "ok", 2U, &written);
    if (status == MOS_OK && written != 2U) {
        status = MOS_ERR_STATE;
    }
    if (mos_sys_close(kernel, fd) != MOS_OK && status == MOS_OK) {
        status = MOS_ERR_STATE;
    }
    if (status != MOS_OK) {
        report_out->status = status;
        return status;
    }

    status = mos_shell_run_script(kernel, "help\nexit\n", output,
                                  sizeof(output));
    report_out->status = status;
    if (status != MOS_OK) {
        return status;
    }

    report_out->labs_ready = 10U;
    return MOS_OK;
}
