#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define T4_MAX_PATH 512
#define T4_MAX_JOBS 256
#define T4_MAX_RESULTS 8192
#define T4_MAX_WORKERS 16
#define T4_HASH_SIZE 32

typedef struct {
    char path[T4_MAX_PATH];
    int32_t depth;
} T4Job;

typedef struct {
    char path[T4_MAX_PATH];
    uint64_t size;
    int64_t mtime;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    unsigned char sha256[T4_HASH_SIZE];
} T4FileRecord;

typedef struct {
    int32_t worker_id;
    int32_t pid;
    int32_t exit_status;
    uint64_t jobs_processed;
    uint64_t files_emitted;
    uint64_t bytes_emitted;
    uint64_t wall_time_ms;
    uint64_t user_cpu_us;
    uint64_t sys_cpu_us;
} T4WorkerStats;

typedef struct {
    char magic[4];
    uint32_t version;
    uint32_t worker_count;
    int32_t max_depth;
    int32_t simulate_work_ms;

    int32_t job_head;
    int32_t job_tail;
    int32_t job_count;
    int32_t active_jobs;
    int32_t done;

    uint32_t result_count;

    T4Job jobs[T4_MAX_JOBS];
    T4FileRecord results[T4_MAX_RESULTS];
    T4WorkerStats stats[T4_MAX_WORKERS];
} T4Shared;

typedef struct {
    char magic[4];
    uint32_t version;
    uint32_t complete;
    uint32_t file_record_count;
    uint32_t worker_count;
} T4DbHeader;

typedef struct {
    char root[T4_MAX_PATH];
    char ipc_path[T4_MAX_PATH];
    char db_path[T4_MAX_PATH];
    int workers;
    int max_depth;
    int simulate_work_ms;
    int mode_verify;
    int mode_dump;
} ManagerConfig;

static int lock_fd(int fd) {
    struct flock lock;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;

    return fcntl(fd, F_SETLKW, &lock);
}

static int unlock_fd(int fd) {
    struct flock lock;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;

    return fcntl(fd, F_SETLK, &lock);
}

static int write_all(int fd, const void *buffer, size_t size) {
    const char *ptr = (const char *)buffer;
    size_t total = 0;

    while (total < size) {
        ssize_t written = write(fd, ptr + total, size - total);
        if (written <= 0) {
            return 0;
        }
        total += (size_t)written;
    }

    return 1;
}

static void default_config(ManagerConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    snprintf(cfg->ipc_path, sizeof(cfg->ipc_path), "data/ipc.mmap");
    snprintf(cfg->db_path, sizeof(cfg->db_path), "data/inventory.db");

    cfg->workers = 0;
    cfg->max_depth = -1;
    cfg->simulate_work_ms = 0;
    cfg->mode_verify = 0;
    cfg->mode_dump = 0;
}

static int parse_args(int argc, char *argv[], ManagerConfig *cfg) {
    int i;

    default_config(cfg);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --root\n");
                return 0;
            }
            snprintf(cfg->root, sizeof(cfg->root), "%s", argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--workers") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --workers\n");
                return 0;
            }
            cfg->workers = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--ipc") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --ipc\n");
                return 0;
            }
            snprintf(cfg->ipc_path, sizeof(cfg->ipc_path), "%s", argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--db") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --db\n");
                return 0;
            }
            snprintf(cfg->db_path, sizeof(cfg->db_path), "%s", argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--max-depth") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --max-depth\n");
                return 0;
            }
            cfg->max_depth = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--simulate-work-ms") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --simulate-work-ms\n");
                return 0;
            }
            cfg->simulate_work_ms = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--verify") == 0) {
            cfg->mode_verify = 1;
        } else if (strcmp(argv[i], "--dump") == 0) {
            cfg->mode_dump = 1;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 0;
        }
    }

    if (cfg->mode_verify || cfg->mode_dump) {
        return 1;
    }

    if (cfg->root[0] == '\0') {
        fprintf(stderr, "Inventory mode requires --root <dir>\n");
        return 0;
    }

    if (cfg->workers < 1 || cfg->workers > T4_MAX_WORKERS) {
        fprintf(stderr, "--workers must be between 1 and %d\n", T4_MAX_WORKERS);
        return 0;
    }

    return 1;
}

static int create_ipc(const ManagerConfig *cfg, int *fd_out, T4Shared **shared_out) {
    int fd;
    T4Shared *shared;
    char root_abs[T4_MAX_PATH];

    mkdir("data", 0755);

    fd = open(cfg->ipc_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open ipc");
        return 0;
    }

    if (ftruncate(fd, (off_t)sizeof(T4Shared)) != 0) {
        perror("ftruncate ipc");
        close(fd);
        return 0;
    }

    shared = mmap(NULL, sizeof(T4Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap ipc");
        close(fd);
        return 0;
    }

    memset(shared, 0, sizeof(T4Shared));

    memcpy(shared->magic, "IPC4", 4);
    shared->version = 1;
    shared->worker_count = (uint32_t)cfg->workers;
    shared->max_depth = cfg->max_depth;
    shared->simulate_work_ms = cfg->simulate_work_ms;

    if (realpath(cfg->root, root_abs) == NULL) {
        perror("realpath root");
        munmap(shared, sizeof(T4Shared));
        close(fd);
        return 0;
    }

    snprintf(shared->jobs[0].path, sizeof(shared->jobs[0].path), "%s", root_abs);
    shared->jobs[0].depth = 0;
    shared->job_head = 0;
    shared->job_tail = 1;
    shared->job_count = 1;
    shared->active_jobs = 0;
    shared->done = 0;
    shared->result_count = 0;

    *fd_out = fd;
    *shared_out = shared;

    return 1;
}

static int start_workers(const ManagerConfig *cfg, pid_t *pids) {
    int i;

    for (i = 0; i < cfg->workers; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return 0;
        }

        if (pid == 0) {
            char worker_id[32];

            snprintf(worker_id, sizeof(worker_id), "%d", i);

            execl("./bin/fileops_worker",
                  "fileops_worker",
                  "--worker-id", worker_id,
                  "--ipc", cfg->ipc_path,
                  (char *)NULL);

            perror("exec fileops_worker");
            _exit(127);
        }

        pids[i] = pid;
    }

    return 1;
}

static void wait_workers(int ipc_fd, T4Shared *shared, pid_t *pids, int workers) {
    int i;

    for (i = 0; i < workers; i++) {
        int status;
        int exit_status = 1;

        if (waitpid(pids[i], &status, 0) < 0) {
            perror("waitpid");
            exit_status = 1;
        } else if (WIFEXITED(status)) {
            exit_status = WEXITSTATUS(status);
        } else {
            exit_status = 1;
        }

        if (lock_fd(ipc_fd) == 0) {
            shared->stats[i].exit_status = exit_status;
            unlock_fd(ipc_fd);
        }
    }
}

static int write_inventory_db_atomic(const char *db_path, T4Shared *shared) {
    int fd;
    char tmp_path[T4_MAX_PATH];
    T4DbHeader header;
    size_t records_size;
    size_t stats_size;

    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", db_path, (long)getpid());

    fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open temp db");
        return 0;
    }

    memcpy(header.magic, "INV4", 4);
    header.version = 1;
    header.complete = 1;
    header.file_record_count = shared->result_count;
    header.worker_count = shared->worker_count;

    if (!write_all(fd, &header, sizeof(header))) {
        perror("write db header");
        close(fd);
        return 0;
    }

    records_size = (size_t)shared->result_count * sizeof(T4FileRecord);
    if (records_size > 0 && !write_all(fd, shared->results, records_size)) {
        perror("write db records");
        close(fd);
        return 0;
    }

    stats_size = (size_t)shared->worker_count * sizeof(T4WorkerStats);
    if (stats_size > 0 && !write_all(fd, shared->stats, stats_size)) {
        perror("write db stats");
        close(fd);
        return 0;
    }

    fsync(fd);
    close(fd);

    if (rename(tmp_path, db_path) != 0) {
        perror("rename db");
        return 0;
    }

    return 1;
}

static int read_db_header(const char *path, T4DbHeader *header, struct stat *st) {
    FILE *file;

    if (stat(path, st) != 0) {
        perror("stat db");
        return 0;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        perror("fopen db");
        return 0;
    }

    if (fread(header, sizeof(*header), 1, file) != 1) {
        fprintf(stderr, "Could not read DB header\n");
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

static int verify_db(const char *path) {
    T4DbHeader header;
    struct stat st;
    off_t expected_size;

    if (!read_db_header(path, &header, &st)) {
        return 1;
    }

    if (memcmp(header.magic, "INV4", 4) != 0) {
        fprintf(stderr, "Invalid magic\n");
        return 1;
    }

    if (header.version != 1) {
        fprintf(stderr, "Invalid version\n");
        return 1;
    }

    expected_size = (off_t)sizeof(T4DbHeader)
        + (off_t)header.file_record_count * (off_t)sizeof(T4FileRecord)
        + (off_t)header.worker_count * (off_t)sizeof(T4WorkerStats);

    if (st.st_size != expected_size) {
        fprintf(stderr, "Invalid DB size\n");
        return 1;
    }

    printf("verify=OK\n");
    return 0;
}

static int dump_db(const char *path) {
    T4DbHeader header;
    struct stat st;

    if (!read_db_header(path, &header, &st)) {
        return 1;
    }

    printf("magic=%.4s\n", header.magic);
    printf("version=%u\n", header.version);
    printf("complete=%u\n", header.complete);
    printf("file_record_count=%u\n", header.file_record_count);
    printf("worker_count=%u\n", header.worker_count);

    return 0;
}

static int inventory_mode(const ManagerConfig *cfg) {
    int ipc_fd;
    T4Shared *shared;
    pid_t pids[T4_MAX_WORKERS];

    if (!create_ipc(cfg, &ipc_fd, &shared)) {
        return 1;
    }

    if (!start_workers(cfg, pids)) {
        munmap(shared, sizeof(T4Shared));
        close(ipc_fd);
        return 1;
    }

    wait_workers(ipc_fd, shared, pids, cfg->workers);

    if (!write_inventory_db_atomic(cfg->db_path, shared)) {
        munmap(shared, sizeof(T4Shared));
        close(ipc_fd);
        return 1;
    }

    printf("fileops_manager: inventory complete\n");
    printf("file_record_count=%u\n", shared->result_count);
    printf("worker_count=%u\n", shared->worker_count);

    munmap(shared, sizeof(T4Shared));
    close(ipc_fd);

    return 0;
}

int main(int argc, char *argv[]) {
    ManagerConfig cfg;

    if (!parse_args(argc, argv, &cfg)) {
        return 1;
    }

    if (cfg.mode_verify) {
        return verify_db(cfg.db_path);
    }

    if (cfg.mode_dump) {
        return dump_db(cfg.db_path);
    }

    return inventory_mode(&cfg);
}