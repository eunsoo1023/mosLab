#include "minios/shell.h"

#include <ctype.h>
#include <string.h>

#include "mosvm/vm_console.h"
#include "private/process_internal.h"

#define MOS_SHELL_MAX_LINE 128U

static mos_status_t mos_shell_map_vm_status(vm_status_t status) {
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

static mos_process_table_t *mos_shell_table(mos_kernel_t *kernel) {
    return (mos_process_table_t *)kernel->private_state;
}

static const mos_process_table_t *mos_shell_table_const(
    const mos_kernel_t *kernel) {
    return (const mos_process_table_t *)kernel->private_state;
}

static mos_status_t mos_shell_require(const mos_kernel_t *kernel,
                                      const mos_process_table_t **table_out) {
    const mos_process_table_t *table;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }

    table = mos_shell_table_const(kernel);
    if (!table->syscall.initialized || !table->shell.initialized) {
        return MOS_ERR_STATE;
    }
    if (table->shell.exited) {
        return MOS_ERR_STATE;
    }

    *table_out = table;
    return MOS_OK;
}

static mos_status_t mos_shell_write(mos_kernel_t *kernel, const char *text) {
    return mos_shell_map_vm_status(vm_console_write(&kernel->machine, text));
}

static mos_status_t mos_shell_parse(char *line, char **command,
                                    size_t *argument_count) {
    char *cursor;
    size_t length;
    size_t index;

    length = strlen(line);
    while (length > 0U && isspace((unsigned char)line[length - 1U])) {
        line[--length] = '\0';
    }
    cursor = line;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '\0') {
        *command = cursor;
        *argument_count = 0U;
        return MOS_OK;
    }

    *command = cursor;
    while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor == '\0') {
        *argument_count = 0U;
        return MOS_OK;
    }

    *cursor++ = '\0';
    *argument_count = 0U;
    while (*cursor != '\0') {
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        (*argument_count)++;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
    }

    for (index = 0U; index < *argument_count; index++) {
        (void)index;
    }
    return MOS_OK;
}

static mos_status_t mos_shell_execute_buffer(mos_kernel_t *kernel,
                                              char *line,
                                              mos_shell_result_t *result_out) {
    char *command;
    size_t argument_count;
    mos_process_table_t *table = mos_shell_table(kernel);
    mos_status_t status;

    status = mos_shell_parse(line, &command, &argument_count);
    if (status != MOS_OK) {
        return status;
    }
    if (*command == '\0') {
        return MOS_OK;
    }
    if (strcmp(command, "help") == 0) {
        if (argument_count != 0U) {
            return MOS_ERR_INVALID;
        }
        return mos_shell_write(kernel, "help: help exit\n");
    }
    if (strcmp(command, "exit") == 0) {
        if (argument_count != 0U) {
            return MOS_ERR_INVALID;
        }
        status = mos_shell_write(kernel, "exit\n");
        if (status == MOS_OK) {
            table->shell.exited = 1;
            result_out->should_exit = 1;
        }
        return status;
    }

    return MOS_ERR_NOT_FOUND;
}

/*
 * LAB9 구현 안내
 * - tokenizer/parser/command dispatch를 private helper로 분리하면 Copilot이 이해하기 쉽다.
 * - 빈 줄, 너무 긴 줄, 알 수 없는 명령, exit 명령을 deterministic하게 처리한다.
 * - tests는 콘솔 문자열만 확인하므로 private parser 이름에 의존하지 않는다.
 */

mos_status_t mos_shell_init(mos_kernel_t *kernel) {
    mos_process_table_t *table;
    mos_status_t status;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }

    table = mos_shell_table(kernel);
    if (!table->syscall.initialized) {
        return MOS_ERR_STATE;
    }
    status = mos_shell_map_vm_status(vm_console_clear(&kernel->machine));
    if (status != MOS_OK) {
        return status;
    }
    table->shell.initialized = 1;
    table->shell.exited = 0;
    return MOS_OK;
}

mos_status_t mos_shell_execute_line(mos_kernel_t *kernel, const char *line, mos_shell_result_t *result_out) {
    const mos_process_table_t *table;
    char line_buffer[MOS_SHELL_MAX_LINE];
    size_t length;
    mos_status_t status;

    if (kernel == NULL || line == NULL || result_out == NULL) {
        return MOS_ERR_NULL;
    }
    result_out->status = MOS_OK;
    result_out->should_exit = 0;
    status = mos_shell_require(kernel, &table);
    if (status != MOS_OK) {
        result_out->status = status;
        return status;
    }

    length = strlen(line);
    if (length >= sizeof(line_buffer)) {
        result_out->status = MOS_ERR_RANGE;
        return MOS_ERR_RANGE;
    }
    memcpy(line_buffer, line, length + 1U);
    status = mos_shell_execute_buffer(kernel, line_buffer, result_out);
    result_out->status = status;
    return status;
}

mos_status_t mos_shell_run_script(mos_kernel_t *kernel, const char *script, char *output, size_t output_size) {
    const mos_process_table_t *table;
    const char *line_start;
    const char *line_end;
    char line_buffer[MOS_SHELL_MAX_LINE];
    mos_shell_result_t result;
    size_t line_length;
    mos_status_t status;

    if (kernel == NULL || script == NULL || output == NULL) {
        return MOS_ERR_NULL;
    }
    if (output_size == 0U) {
        return MOS_ERR_INVALID;
    }
    output[0] = '\0';
    status = mos_shell_require(kernel, &table);
    if (status != MOS_OK) {
        return status;
    }
    status = mos_shell_map_vm_status(vm_console_clear(&kernel->machine));
    if (status != MOS_OK) {
        return status;
    }

    line_start = script;
    while (*line_start != '\0') {
        line_end = strchr(line_start, '\n');
        line_length = line_end == NULL
                          ? strlen(line_start)
                          : (size_t)(line_end - line_start);
        if (line_length >= sizeof(line_buffer)) {
            return MOS_ERR_RANGE;
        }
        memcpy(line_buffer, line_start, line_length);
        line_buffer[line_length] = '\0';
        status = mos_shell_execute_buffer(kernel, line_buffer, &result);
        if (status != MOS_OK) {
            return status;
        }
        if (result.should_exit) {
            break;
        }
        if (line_end == NULL) {
            break;
        }
        line_start = line_end + 1U;
    }

    return mos_shell_map_vm_status(vm_console_snapshot(
        &kernel->machine, output, output_size));
}
