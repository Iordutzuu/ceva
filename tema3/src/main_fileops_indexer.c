#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <fcntl.h>
#include <time.h>

#define MAX_PATH_LEN 1024

const char* DEFAULT_DB_PATH = "data/index.db";

typedef struct {
    char magic[4];
    uint32_t version;
    uint64_t snapshot_id;
    uint32_t snapshot_state;
    uint32_t active_writers;
    uint32_t record_count;
} FileDbHeader;

typedef struct {
    char absolute_path[MAX_PATH_LEN];
    char type[20];
    long long size;
    long mtime;
    unsigned long hash;
    unsigned long dev;
    unsigned long ino;
    char symlink_target[MAX_PATH_LEN];
} FileRecord;

unsigned long simple_hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

int lock_file(int fd) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    return fcntl(fd, F_SETLKW, &lock);
}

int unlock_file(int fd) {
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    return fcntl(fd, F_SETLK, &lock);
}

int write_all(int fd, const void *buffer, size_t size) {
    const char *ptr = (const char *)buffer;
    size_t total = 0;
    while (total < size) {
        ssize_t written = write(fd, ptr + total, size - total);
        if (written <= 0) return 0;
        total += written;
    }
    return 1;
}

void add_record(FileRecord **records, size_t *count, size_t *capacity, FileRecord *record) {
    if (*count >= *capacity) {
        *capacity = (*capacity == 0) ? 128 : (*capacity * 2);
        *records = realloc(*records, *capacity * sizeof(FileRecord));
    }
    (*records)[*count] = *record;
    (*count)++;
}

void process_directory(const char *current_path, FileRecord **records, size_t *count, size_t *capacity) {
    DIR *dir = opendir(current_path);
    if (dir == NULL) {
        perror("Eroare deschidere director");
        return;
    }

    struct dirent *entry;
    struct stat file_stat;
    char path[MAX_PATH_LEN];
    char absolute_path[MAX_PATH_LEN];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", current_path, entry->d_name);

        if (realpath(path, absolute_path) == NULL) {
            strncpy(absolute_path, path, sizeof(absolute_path));
        }

        if (lstat(path, &file_stat) == -1) {
            continue;
        }

        FileRecord record;
        memset(&record, 0, sizeof(FileRecord));
        strncpy(record.absolute_path, absolute_path, MAX_PATH_LEN - 1);
        record.dev = file_stat.st_dev;
        record.ino = file_stat.st_ino;
        record.mtime = file_stat.st_mtime;

        if (S_ISREG(file_stat.st_mode)) {
            strcpy(record.type, "Fisier regulat");
            record.size = file_stat.st_size;
            record.hash = simple_hash(absolute_path);
        } else if (S_ISDIR(file_stat.st_mode)) {
            strcpy(record.type, "Director");
        } else if (S_ISLNK(file_stat.st_mode)) {
            strcpy(record.type, "Symlink");
            ssize_t len = readlink(path, record.symlink_target, sizeof(record.symlink_target) - 1);
            if (len != -1) record.symlink_target[len] = '\0';
        } else if (S_ISFIFO(file_stat.st_mode)) {
            strcpy(record.type, "FIFO");
        } else {
            strcpy(record.type, "Necunoscut");
        }

        add_record(records, count, capacity, &record);

        if (S_ISDIR(file_stat.st_mode)) {
            process_directory(path, records, count, capacity);
        }
    }
    closedir(dir);
}

int write_database(const char *db_path, FileRecord *records, size_t count) {
    int fd;
    FileDbHeader header;

    mkdir("data", 0755);

    fd = open(db_path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        perror("Eroare open DB");
        return 0;
    }

    if (lock_file(fd) < 0) {
        perror("Eroare fcntl lock");
        close(fd);
        return 0;
    }

    ssize_t bytes_read = read(fd, &header, sizeof(FileDbHeader));
    if (bytes_read == 0) {
        memcpy(header.magic, "IDX1", 4);
        header.version = 1;
        header.snapshot_id = (uint64_t)time(NULL);
        header.snapshot_state = 1;
        header.active_writers = 1;
        header.record_count = 0;

        write_all(fd, &header, sizeof(FileDbHeader));
    } else {
        if (header.snapshot_state == 2) {
            fprintf(stderr, "Eroare: Snapshot-ul este SEALED. Nu se mai pot adauga inregistrari!\n");
            unlock_file(fd);
            close(fd);
            return 0;
        }
        header.active_writers++;
    }

    size_t existing_count = header.record_count;
    FileRecord *existing_records = NULL;
    if (existing_count > 0) {
        existing_records = malloc(existing_count * sizeof(FileRecord));
        lseek(fd, sizeof(FileDbHeader), SEEK_SET);
        read(fd, existing_records, existing_count * sizeof(FileRecord));
    }

    lseek(fd, 0, SEEK_END);
    size_t added_count = 0;

    for (size_t i = 0; i < count; i++) {
        int is_dup = 0;
        for (size_t j = 0; j < existing_count; j++) {
            if (strcmp(records[i].absolute_path, existing_records[j].absolute_path) == 0) {
                is_dup = 1;
                break;
            }
        }
        if (!is_dup) {
            write_all(fd, &records[i], sizeof(FileRecord));
            added_count++;
        }
    }

    if (existing_records) free(existing_records);

    header.record_count += added_count;
    header.active_writers--;
    
    if (header.active_writers == 0) {
        header.snapshot_state = 2;
    }

    lseek(fd, 0, SEEK_SET);
    write_all(fd, &header, sizeof(FileDbHeader));

    unlock_file(fd);
    close(fd);

    printf("fileops_indexer: a adaugat %zu inregistrari noi (total DB: %u)\n", added_count, header.record_count);
    return 1;
}

int main(int argc, char *argv[]) {
    char *root_dir = NULL;
    char *db_path = (char*)DEFAULT_DB_PATH;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            root_dir = argv[++i];
        } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        }
    }

    if (root_dir == NULL) {
        fprintf(stderr, "Eroare: Argumentul --root este obligatoriu!\n");
        return 1;
    }

    FileRecord *records = NULL;
    size_t count = 0;
    size_t capacity = 0;

    process_directory(root_dir, &records, &count, &capacity);

    write_database(db_path, records, count);

    free(records);
    return 0;
}