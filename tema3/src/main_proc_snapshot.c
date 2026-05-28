#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAX_COMM 64
#define MAX_CMDLINE 256

typedef struct {
    char magic[4];
    uint32_t version;
    uint64_t snapshot_id;
    uint32_t snapshot_state;
    uint32_t active_writers;
    uint32_t record_count;
} ProcDbHeader;

typedef struct {
    int32_t pid;
    int32_t ppid;
    char state;
    char comm[MAX_COMM];
    char cmdline[MAX_CMDLINE];
    long rss_kb;
    unsigned long long cpu_time;
} ProcRecord;

int is_number(const char *str) {
    int i;
    if (str == NULL || str[0] == '\0') {
        return 0;
    }
    for (i = 0; str[i] != '\0'; i++) {
        if (!isdigit((unsigned char)str[i])) {
            return 0;
        }
    }
    return 1;
}

int read_comm(const char *pid, char *comm, size_t size) {
    char path[256];
    FILE *file;
    snprintf(path, sizeof(path), "/proc/%s/comm", pid);
    file = fopen(path, "r");
    if (file == NULL) return 0;
    if (fgets(comm, size, file) == NULL) {
        fclose(file);
        return 0;
    }
    fclose(file);
    comm[strcspn(comm, "\n")] = '\0';
    return 1;
}

int read_status(const char *pid, int *ppid, char *state, long *rss_kb) {
    char path[256];
    char line[512];
    FILE *file;
    snprintf(path, sizeof(path), "/proc/%s/status", pid);
    file = fopen(path, "r");
    if (file == NULL) return 0;
    *ppid = 0;
    *state = '?';
    *rss_kb = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "State:", 6) == 0) {
            sscanf(line, "State:\t%c", state);
        } else if (strncmp(line, "PPid:", 5) == 0) {
            sscanf(line, "PPid:\t%d", ppid);
        } else if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS:\t%ld", rss_kb);
        }
    }
    fclose(file);
    return 1;
}

int read_cmdline(const char *pid, char *cmdline, size_t size, const char *comm) {
    char path[256];
    FILE *file;
    size_t n, i;
    snprintf(path, sizeof(path), "/proc/%s/cmdline", pid);
    file = fopen(path, "r");
    if (file == NULL) return 0;
    n = fread(cmdline, 1, size - 1, file);
    fclose(file);
    if (n == 0) {
        snprintf(cmdline, size, "%s", comm);
        return 1;
    }
    cmdline[n] = '\0';
    for (i = 0; i < n; i++) {
        if (cmdline[i] == '\0') {
            cmdline[i] = ' ';
        }
    }
    return 1;
}

int read_cpu_time(const char *pid, unsigned long long *cpu_time) {
    char path[256];
    char line[2048];
    FILE *file;
    char *end_paren, *rest;
    char state;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned int flags;
    unsigned long minflt, cminflt, majflt, cmajflt;
    unsigned long long utime, stime;
    snprintf(path, sizeof(path), "/proc/%s/stat", pid);
    file = fopen(path, "r");
    if (file == NULL) return 0;
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return 0;
    }
    fclose(file);
    end_paren = strrchr(line, ')');
    if (end_paren == NULL) return 0;
    rest = end_paren + 2;
    if (sscanf(rest, "%c %d %d %d %d %d %u %lu %lu %lu %lu %llu %llu",
               &state, &ppid, &pgrp, &session, &tty_nr, &tpgid,
               &flags, &minflt, &cminflt, &majflt, &cmajflt,
               &utime, &stime) != 13) {
        return 0;
    }
    *cpu_time = utime + stime;
    return 1;
}

int parse_db_argument(int argc, char *argv[], char *db_path, size_t size) {
    int i;
    snprintf(db_path, size, "data/proc.db");
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--db") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing path after --db\n");
                return 0;
            }
            snprintf(db_path, size, "%s", argv[i + 1]);
            return 1;
        }
    }
    return 1;
}

int add_record(ProcRecord **records, size_t *count, size_t *capacity, ProcRecord *record) {
    ProcRecord *new_records;
    if (*count >= *capacity) {
        size_t new_capacity = (*capacity == 0) ? 128 : (*capacity * 2);
        new_records = realloc(*records, new_capacity * sizeof(ProcRecord));
        if (new_records == NULL) return 0;
        *records = new_records;
        *capacity = new_capacity;
    }
    (*records)[*count] = *record;
    (*count)++;
    return 1;
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
    ssize_t written;
    size_t total = 0;
    while (total < size) {
        written = write(fd, ptr + total, size - total);
        if (written <= 0) return 0;
        total += (size_t)written;
    }
    return 1;
}

int write_database(const char *db_path, ProcRecord *records, size_t count) {
    int fd;
    ProcDbHeader header;

    mkdir("data", 0755);

    fd = open(db_path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        perror("open db");
        return 0;
    }

    if (lock_file(fd) < 0) {
        perror("fcntl lock");
        close(fd);
        return 0;
    }

    ssize_t bytes_read = read(fd, &header, sizeof(ProcDbHeader));
    if (bytes_read == 0) {
        memcpy(header.magic, "PRC1", 4);
        header.version = 1;
        header.snapshot_id = (uint64_t)time(NULL);
        header.snapshot_state = 1;
        header.active_writers = 1;
        header.record_count = 0;

        write_all(fd, &header, sizeof(ProcDbHeader));
    } else {
        if (header.snapshot_state == 2) {
            fprintf(stderr, "Error: Snapshot is SEALED. Cannot append records.\n");
            unlock_file(fd);
            close(fd);
            return 0;
        }
        header.active_writers++;
    }

    size_t existing_count = header.record_count;
    ProcRecord *existing_records = NULL;
    if (existing_count > 0) {
        existing_records = malloc(existing_count * sizeof(ProcRecord));
        lseek(fd, sizeof(ProcDbHeader), SEEK_SET);
        read(fd, existing_records, existing_count * sizeof(ProcRecord));
    }

    lseek(fd, 0, SEEK_END);
    size_t added_count = 0;

    for (size_t i = 0; i < count; i++) {
        int is_dup = 0;
        for (size_t j = 0; j < existing_count; j++) {
            if (records[i].pid == existing_records[j].pid) {
                is_dup = 1;
                break;
            }
        }
        if (!is_dup) {
            write_all(fd, &records[i], sizeof(ProcRecord));
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
    write_all(fd, &header, sizeof(ProcDbHeader));

    unlock_file(fd);
    close(fd);

    printf("proc_snapshot: added %zu new processes (total DB: %u)\n", added_count, header.record_count);
    return 1;
}

int main(int argc, char *argv[]) {
    char db_path[256];
    DIR *dir;
    struct dirent *entry;
    ProcRecord *records;
    size_t count;
    size_t capacity;

    if (!parse_db_argument(argc, argv, db_path, sizeof(db_path))) {
        return 1;
    }

    dir = opendir("/proc");
    if (dir == NULL) {
        perror("opendir /proc");
        return 1;
    }

    records = NULL;
    count = 0;
    capacity = 0;

    while ((entry = readdir(dir)) != NULL) {
        ProcRecord record;

        if (!is_number(entry->d_name)) continue;

        memset(&record, 0, sizeof(record));
        record.pid = atoi(entry->d_name);

        if (!read_comm(entry->d_name, record.comm, sizeof(record.comm))) continue;
        if (!read_status(entry->d_name, &record.ppid, &record.state, &record.rss_kb)) continue;
        if (!read_cmdline(entry->d_name, record.cmdline, sizeof(record.cmdline), record.comm)) continue;
        if (!read_cpu_time(entry->d_name, &record.cpu_time)) continue;

        if (!add_record(&records, &count, &capacity, &record)) {
            fprintf(stderr, "Error: memory allocation failed\n");
            free(records);
            closedir(dir);
            return 1;
        }
    }

    closedir(dir);

    if (!write_database(db_path, records, count)) {
        free(records);
        return 1;
    }

    free(records);
    return 0;
}