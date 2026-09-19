#ifndef MOSLAB_PROCESS_INTERNAL_H
#define MOSLAB_PROCESS_INTERNAL_H

#include <stddef.h>

#include "minios/blockdev.h"
#include "minios/process.h"
#include "mosvm/vm_cpu.h"

#define MOS_PROCESS_MAX 64U

typedef struct mos_pcb {
    mos_pid_t pid;
    mos_process_state_t state;
    vm_cpu_context_t context;
} mos_pcb_t;

typedef struct mos_scheduler_private {
    unsigned int time_slice_ticks;
    mos_pid_t queue[MOS_PROCESS_MAX];
    size_t head;
    size_t size;
    mos_pid_t current_pid;
    unsigned int dispatch_count;
    int initialized;
} mos_scheduler_private_t;

typedef struct mos_memory_private {
    size_t total_frames;
    size_t free_frames;
    size_t bitmap_bytes;
    unsigned char *bitmap;
    int initialized;
} mos_memory_private_t;

typedef struct mos_vm_mapping_node {
    mos_pid_t pid;
    size_t virtual_page;
    size_t frame;
    int writable;
    struct mos_vm_mapping_node *next;
} mos_vm_mapping_node_t;

typedef struct mos_vm_private {
    mos_vm_mapping_node_t *head;
    size_t mapping_count;
    int initialized;
} mos_vm_private_t;

#define MOS_FS_MAX_FILES 8U
#define MOS_FS_MAX_BLOCKS_PER_FILE 4U
#define MOS_FS_MAX_PATH 32U

typedef struct mos_fs_file_private {
    int used;
    int inode;
    size_t size;
    size_t block_count;
    size_t blocks[MOS_FS_MAX_BLOCKS_PER_FILE];
    char path[MOS_FS_MAX_PATH];
} mos_fs_file_private_t;

typedef struct mos_fs_private {
    mos_blockdev_t *device;
    mos_fs_file_private_t files[MOS_FS_MAX_FILES];
    int initialized;
} mos_fs_private_t;

#define MOS_SYSCALL_MAX_FDS 16U

typedef struct mos_fd_slot_private {
    int used;
    char path[MOS_FS_MAX_PATH];
    size_t offset;
} mos_fd_slot_private_t;

typedef struct mos_syscall_private {
    mos_fd_slot_private_t slots[MOS_SYSCALL_MAX_FDS];
    int initialized;
} mos_syscall_private_t;

typedef struct mos_shell_private {
    int initialized;
    int exited;
} mos_shell_private_t;

typedef struct mos_process_table {
    mos_pcb_t entries[MOS_PROCESS_MAX];
    size_t count;
    mos_pid_t next_pid;
    mos_scheduler_private_t scheduler;
    mos_memory_private_t memory;
    mos_vm_private_t vm;
    mos_blockdev_t blockdev;
    mos_fs_private_t fs;
    mos_syscall_private_t syscall;
    mos_shell_private_t shell;
} mos_process_table_t;

#endif