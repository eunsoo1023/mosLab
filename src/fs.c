#include "minios/fs.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "private/process_internal.h"

#define MOS_FS_MAGIC 0x4d4f5346U
#define MOS_FS_VERSION 1U
#define MOS_FS_INODE_START 1U
#define MOS_FS_INODE_BLOCKS 4U
#define MOS_FS_DATA_START 5U
#define MOS_FS_DISK_BLOCK_SIZE 128U

typedef struct mos_fs_superblock_disk {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t inode_count;
    uint32_t inode_start;
    uint32_t data_start;
} mos_fs_superblock_disk_t;

typedef struct mos_fs_inode_disk {
    uint32_t used;
    uint32_t inode;
    uint32_t size;
    uint32_t block_count;
    uint32_t blocks[MOS_FS_MAX_BLOCKS_PER_FILE];
    char path[MOS_FS_MAX_PATH];
} mos_fs_inode_disk_t;

static mos_process_table_t *mos_fs_table(mos_kernel_t *kernel) {
    return (mos_process_table_t *)kernel->private_state;
}

static const mos_process_table_t *mos_fs_table_const(
    const mos_kernel_t *kernel) {
    return (const mos_process_table_t *)kernel->private_state;
}

static mos_status_t mos_fs_require(const mos_kernel_t *kernel,
                                   const mos_process_table_t **table_out) {
    const mos_process_table_t *table;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED || kernel->private_state == NULL) {
        return MOS_ERR_STATE;
    }

    table = mos_fs_table_const(kernel);
    if (!table->fs.initialized || table->fs.device == NULL) {
        return MOS_ERR_STATE;
    }

    *table_out = table;
    return MOS_OK;
}

static mos_status_t mos_fs_validate_path(const char *path, size_t *length_out) {
    size_t length;
    size_t index;

    if (path == NULL) {
        return MOS_ERR_NULL;
    }

    length = strlen(path);
    if (length < 2U) {
        return MOS_ERR_INVALID;
    }
    if (length >= MOS_FS_MAX_PATH) {
        return MOS_ERR_RANGE;
    }
    if (path[0] != '/' || path[length - 1U] == '/') {
        return MOS_ERR_INVALID;
    }

    for (index = 1U; index < length; index++) {
        if (path[index] == '/' && path[index - 1U] == '/') {
            return MOS_ERR_INVALID;
        }
        if (path[index] == '.' &&
            (index == 1U || path[index - 1U] == '/') &&
            (index + 1U == length || path[index + 1U] == '/')) {
            return MOS_ERR_INVALID;
        }
    }

    *length_out = length;
    return MOS_OK;
}

static mos_fs_file_private_t *mos_fs_find_file(mos_fs_private_t *fs,
                                                const char *path) {
    size_t index;

    for (index = 0U; index < MOS_FS_MAX_FILES; index++) {
        if (fs->files[index].used && strcmp(fs->files[index].path, path) == 0) {
            return &fs->files[index];
        }
    }

    return NULL;
}

static const mos_fs_file_private_t *mos_fs_find_file_const(
    const mos_fs_private_t *fs, const char *path) {
    size_t index;

    for (index = 0U; index < MOS_FS_MAX_FILES; index++) {
        if (fs->files[index].used && strcmp(fs->files[index].path, path) == 0) {
            return &fs->files[index];
        }
    }

    return NULL;
}

static int mos_fs_block_used(const mos_fs_private_t *fs, size_t block) {
    size_t file_index;
    size_t block_index;

    for (file_index = 0U; file_index < MOS_FS_MAX_FILES; file_index++) {
        if (!fs->files[file_index].used) {
            continue;
        }
        for (block_index = 0U;
             block_index < fs->files[file_index].block_count;
             block_index++) {
            if (fs->files[file_index].blocks[block_index] == block) {
                return 1;
            }
        }
    }

    return 0;
}

static mos_status_t mos_fs_sync_metadata(mos_fs_private_t *fs) {
    unsigned char block[MOS_FS_DISK_BLOCK_SIZE];
    mos_fs_superblock_disk_t superblock;
    mos_fs_inode_disk_t inode_disk;
    size_t block_index;
    size_t file_index;

    memset(block, 0, sizeof(block));
    memset(&superblock, 0, sizeof(superblock));
    superblock.magic = MOS_FS_MAGIC;
    superblock.version = MOS_FS_VERSION;
    superblock.block_size = (uint32_t)fs->device->block_size;
    superblock.block_count = (uint32_t)fs->device->block_count;
    superblock.inode_count = MOS_FS_MAX_FILES;
    superblock.inode_start = MOS_FS_INODE_START;
    superblock.data_start = MOS_FS_DATA_START;
    memcpy(block, &superblock, sizeof(superblock));
    if (mos_blockdev_write(fs->device, 0U, block, sizeof(block)) != MOS_OK) {
        return MOS_ERR_STATE;
    }

    for (block_index = 0U; block_index < MOS_FS_INODE_BLOCKS; block_index++) {
        memset(block, 0, sizeof(block));
        for (file_index = 0U; file_index < MOS_FS_MAX_FILES; file_index++) {
            size_t inode_block = file_index / 2U;
            size_t inode_slot = file_index % 2U;
            size_t offset = inode_slot * sizeof(inode_disk);
            size_t data_index;

            if (inode_block != block_index || !fs->files[file_index].used) {
                continue;
            }

            memset(&inode_disk, 0, sizeof(inode_disk));
            inode_disk.used = 1U;
            inode_disk.inode = (uint32_t)fs->files[file_index].inode;
            inode_disk.size = (uint32_t)fs->files[file_index].size;
            inode_disk.block_count =
                (uint32_t)fs->files[file_index].block_count;
            for (data_index = 0U;
                 data_index < fs->files[file_index].block_count;
                 data_index++) {
                inode_disk.blocks[data_index] =
                    (uint32_t)fs->files[file_index].blocks[data_index];
            }
            memcpy(inode_disk.path, fs->files[file_index].path,
                   sizeof(inode_disk.path));
            memcpy(block + offset, &inode_disk, sizeof(inode_disk));
        }
        if (mos_blockdev_write(fs->device, MOS_FS_INODE_START + block_index,
                               block, sizeof(block)) != MOS_OK) {
            return MOS_ERR_STATE;
        }
    }

    return MOS_OK;
}

/*
 * LAB7 구현 안내
 * - inode table, directory entry, file data block 배치를 직접 설계한다.
 * - path 길이와 문자 범위를 검사하고, 같은 이름 생성/없는 파일 읽기를 구분한다.
 * - 공개 테스트는 파일 생성/읽기/쓰기/stat 동작만 확인한다.
 */

mos_status_t mos_fs_init(mos_kernel_t *kernel, mos_blockdev_t *device) {
    mos_process_table_t *table;
    unsigned char block[MOS_FS_DISK_BLOCK_SIZE];
    size_t block_index;
    mos_status_t status;

    if (kernel == NULL || device == NULL) {
        return MOS_ERR_NULL;
    }
    if (kernel->state != MOS_KERNEL_BOOTED) {
        return MOS_ERR_STATE;
    }
    if (device->machine == NULL || device->block_size != MOS_FS_DISK_BLOCK_SIZE ||
        device->block_count <= MOS_FS_DATA_START) {
        return MOS_ERR_STATE;
    }

    if (kernel->private_state == NULL) {
        table = (mos_process_table_t *)calloc(1U, sizeof(*table));
        if (table == NULL) {
            return MOS_ERR_NO_SPACE;
        }
        kernel->private_state = table;
    } else {
        table = mos_fs_table(kernel);
    }

    memset(&table->fs, 0, sizeof(table->fs));
    table->fs.device = device;
    for (block_index = 0U; block_index < device->block_count; block_index++) {
        memset(block, 0, sizeof(block));
        status = mos_blockdev_write(device, block_index, block, sizeof(block));
        if (status != MOS_OK) {
            return status;
        }
    }
    table->fs.initialized = 1;
    return mos_fs_sync_metadata(&table->fs);
}

mos_status_t mos_fs_create(mos_kernel_t *kernel, const char *path, mos_inode_t *inode_out) {
    const mos_process_table_t *table_const;
    mos_process_table_t *table;
    mos_fs_file_private_t *file = NULL;
    size_t path_length;
    size_t index;
    mos_status_t status;

    if (kernel == NULL || inode_out == NULL) {
        return MOS_ERR_NULL;
    }
    status = mos_fs_validate_path(path, &path_length);
    if (status != MOS_OK) {
        return status;
    }
    status = mos_fs_require(kernel, &table_const);
    if (status != MOS_OK) {
        return status;
    }
    if (mos_fs_find_file_const(&table_const->fs, path) != NULL) {
        return MOS_ERR_STATE;
    }

    table = mos_fs_table(kernel);
    for (index = 0U; index < MOS_FS_MAX_FILES; index++) {
        if (!table->fs.files[index].used) {
            file = &table->fs.files[index];
            break;
        }
    }
    if (file == NULL) {
        return MOS_ERR_NO_SPACE;
    }

    memset(file, 0, sizeof(*file));
    file->used = 1;
    file->inode = (mos_inode_t)index;
    memcpy(file->path, path, path_length + 1U);
    status = mos_fs_sync_metadata(&table->fs);
    if (status != MOS_OK) {
        memset(file, 0, sizeof(*file));
        return status;
    }
    *inode_out = file->inode;
    return MOS_OK;
}

mos_status_t mos_fs_write(mos_kernel_t *kernel, const char *path, const void *buffer, size_t size) {
    const mos_process_table_t *table_const;
    mos_process_table_t *table;
    mos_fs_file_private_t *file;
    size_t path_length;
    size_t required_blocks;
    size_t new_blocks[MOS_FS_MAX_BLOCKS_PER_FILE];
    size_t new_block_count;
    size_t block_index;
    unsigned char block[MOS_FS_DISK_BLOCK_SIZE];
    mos_status_t status;

    if (kernel == NULL) {
        return MOS_ERR_NULL;
    }
    status = mos_fs_validate_path(path, &path_length);
    if (status != MOS_OK) {
        return status;
    }
    if (size > 0U && buffer == NULL) {
        return MOS_ERR_NULL;
    }
    status = mos_fs_require(kernel, &table_const);
    if (status != MOS_OK) {
        return status;
    }
    file = mos_fs_find_file((mos_fs_private_t *)&table_const->fs, path);
    if (file == NULL) {
        return MOS_ERR_NOT_FOUND;
    }

    required_blocks = size == 0U ? 0U :
        (size + MOS_FS_DISK_BLOCK_SIZE - 1U) / MOS_FS_DISK_BLOCK_SIZE;
    if (required_blocks > MOS_FS_MAX_BLOCKS_PER_FILE || size > UINT32_MAX) {
        return MOS_ERR_NO_SPACE;
    }

    table = mos_fs_table(kernel);
    for (block_index = 0U; block_index < file->block_count; block_index++) {
        new_blocks[block_index] = file->blocks[block_index];
    }
    new_block_count = file->block_count;
    for (block_index = file->block_count;
         block_index < required_blocks; block_index++) {
        size_t candidate;

        for (candidate = MOS_FS_DATA_START;
             candidate < table->fs.device->block_count; candidate++) {
            if (!mos_fs_block_used(&table->fs, candidate)) {
                new_blocks[new_block_count++] = candidate;
                break;
            }
        }
        if (new_block_count == block_index) {
            return MOS_ERR_NO_SPACE;
        }
    }

    for (block_index = 0U; block_index < required_blocks; block_index++) {
        size_t offset = block_index * MOS_FS_DISK_BLOCK_SIZE;
        size_t remaining = size > offset ? size - offset : 0U;
        size_t copy_size = remaining > MOS_FS_DISK_BLOCK_SIZE
                               ? MOS_FS_DISK_BLOCK_SIZE : remaining;

        memset(block, 0, sizeof(block));
        if (copy_size > 0U) {
            memcpy(block, (const unsigned char *)buffer + offset, copy_size);
        }
        status = mos_blockdev_write(
            table->fs.device,
            block_index < file->block_count
                ? file->blocks[block_index]
                : new_blocks[block_index],
            block, sizeof(block));
        if (status != MOS_OK) {
            return status;
        }
    }

    file->size = size;
    file->block_count = required_blocks;
    for (block_index = 0U; block_index < required_blocks; block_index++) {
        file->blocks[block_index] = new_blocks[block_index];
    }
    for (; block_index < MOS_FS_MAX_BLOCKS_PER_FILE; block_index++) {
        file->blocks[block_index] = 0U;
    }
    return mos_fs_sync_metadata(&table->fs);
}

mos_status_t mos_fs_read(const mos_kernel_t *kernel, const char *path, void *buffer, size_t buffer_size, size_t *read_out) {
    const mos_process_table_t *table;
    const mos_fs_file_private_t *file;
    size_t path_length;
    size_t bytes_to_read;
    size_t block_index;
    unsigned char block[MOS_FS_DISK_BLOCK_SIZE];
    mos_status_t status;

    if (kernel == NULL || read_out == NULL) {
        return MOS_ERR_NULL;
    }
    status = mos_fs_validate_path(path, &path_length);
    if (status != MOS_OK) {
        return status;
    }
    if (buffer_size > 0U && buffer == NULL) {
        return MOS_ERR_NULL;
    }
    status = mos_fs_require(kernel, &table);
    if (status != MOS_OK) {
        return status;
    }
    file = mos_fs_find_file_const(&table->fs, path);
    if (file == NULL) {
        return MOS_ERR_NOT_FOUND;
    }

    bytes_to_read = file->size < buffer_size ? file->size : buffer_size;
    for (block_index = 0U;
         block_index * MOS_FS_DISK_BLOCK_SIZE < bytes_to_read;
         block_index++) {
        size_t offset = block_index * MOS_FS_DISK_BLOCK_SIZE;
        size_t copy_size = bytes_to_read - offset;

        if (copy_size > MOS_FS_DISK_BLOCK_SIZE) {
            copy_size = MOS_FS_DISK_BLOCK_SIZE;
        }
        status = mos_blockdev_read(table->fs.device, file->blocks[block_index],
                                   block, sizeof(block));
        if (status != MOS_OK) {
            return status;
        }
        memcpy((unsigned char *)buffer + offset, block, copy_size);
    }
    *read_out = bytes_to_read;
    return MOS_OK;
}

mos_status_t mos_fs_stat(const mos_kernel_t *kernel, const char *path, mos_file_stat_t *stat_out) {
    const mos_process_table_t *table;
    const mos_fs_file_private_t *file;
    size_t path_length;
    mos_status_t status;

    if (kernel == NULL || stat_out == NULL) {
        return MOS_ERR_NULL;
    }
    status = mos_fs_validate_path(path, &path_length);
    if (status != MOS_OK) {
        return status;
    }
    status = mos_fs_require(kernel, &table);
    if (status != MOS_OK) {
        return status;
    }
    file = mos_fs_find_file_const(&table->fs, path);
    if (file == NULL) {
        return MOS_ERR_NOT_FOUND;
    }

    stat_out->inode = file->inode;
    stat_out->size = file->size;
    return MOS_OK;
}
