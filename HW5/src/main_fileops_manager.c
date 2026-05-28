#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define T4_MAX_PATH 512
#define T4_MAX_JOBS 256
#define T4_MAX_RESULTS 8192
#define T4_MAX_WORKERS 16
#define T4_HASH_SIZE 32
#define T5_MSG_MAX 512

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
    int32_t shutdown_requested;
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
    char pid_file[T4_MAX_PATH];
    int workers;
    int max_depth;
    int simulate_work_ms;
    int graceful_timeout;
    int mode_verify;
    int mode_dump;
} ManagerConfig;

static volatile sig_atomic_t g_want_status = 0;
static volatile sig_atomic_t g_want_shutdown = 0;
static volatile sig_atomic_t g_got_sigchld = 0;

static void on_signal(int sig) {
    if (sig == SIGUSR1) {
        g_want_status = 1;
    } else if (sig == SIGINT || sig == SIGTERM) {
        g_want_shutdown = 1;
    } else if (sig == SIGCHLD) {
        g_got_sigchld = 1;
    }
}

static void install_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);
}

static int lock_fd(int fd) {
    struct flock l;
    memset(&l, 0, sizeof(l));
    l.l_type = F_WRLCK;
    l.l_whence = SEEK_SET;
    return fcntl(fd, F_SETLKW, &l);
}

static int unlock_fd(int fd) {
    struct flock l;
    memset(&l, 0, sizeof(l));
    l.l_type = F_UNLCK;
    l.l_whence = SEEK_SET;
    return fcntl(fd, F_SETLK, &l);
}

static int write_all(int fd, const void *buffer, size_t size) {
    const char *p = (const char *)buffer;
    size_t total = 0;
    
    while (total < size) {
        ssize_t w = write(fd, p + total, size - total);
        if (w <= 0) {
            return 0;
        }
        total += (size_t)w;
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
    cfg->graceful_timeout = 3;
}

static int parse_args(int argc, char *argv[], ManagerConfig *cfg) {
    int i;
    default_config(cfg);
    
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            snprintf(cfg->root, sizeof(cfg->root), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            cfg->workers = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ipc") == 0 && i + 1 < argc) {
            snprintf(cfg->ipc_path, sizeof(cfg->ipc_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            snprintf(cfg->db_path, sizeof(cfg->db_path), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--max-depth") == 0 && i + 1 < argc) {
            cfg->max_depth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--simulate-work-ms") == 0 && i + 1 < argc) {
            cfg->simulate_work_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--graceful-timeout") == 0 && i + 1 < argc) {
            cfg->graceful_timeout = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
            snprintf(cfg->pid_file, sizeof(cfg->pid_file), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--verify") == 0) {
            cfg->mode_verify = 1;
        } else if (strcmp(argv[i], "--dump") == 0) {
            cfg->mode_dump = 1;
        } else {
            fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
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
    
    if (cfg->graceful_timeout < 1) {
        cfg->graceful_timeout = 1;
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
    shared->job_tail = 1;
    shared->job_count = 1;
    
    *fd_out = fd;
    *shared_out = shared;
    
    return 1;
}

static int write_pid_file(const char *path) {
    FILE *f;
    
    if (path[0] == '\0') {
        return 1;
    }
    
    f = fopen(path, "w");
    if (!f) {
        perror("pid-file");
        return 0;
    }
    
    fprintf(f, "%ld\n", (long)getpid());
    fclose(f);
    
    return 1;
}

static int start_workers(const ManagerConfig *cfg, pid_t *pids, int control_write_fd) {
    int i;
    for (i = 0; i < cfg->workers; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork");
            return 0;
        }
        
        if (pid == 0) {
            char worker_id[32];
            char fd_arg[32];
            
            snprintf(worker_id, sizeof(worker_id), "%d", i);
            snprintf(fd_arg, sizeof(fd_arg), "%d", control_write_fd);
            
            execl("./bin/fileops_worker", "fileops_worker", "--worker-id", worker_id, "--ipc", cfg->ipc_path, "--control-fd", fd_arg, (char *)NULL);
            perror("exec fileops_worker");
            _exit(127);
        }
        
        pids[i] = pid;
    }
    return 1;
}

static void drain_control_pipe(int fd) {
    char buf[1024];
    ssize_t n;
    
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write_all(STDOUT_FILENO, buf, (size_t)n);
    }
}

static void print_status(int ipc_fd, T4Shared *shared, int workers_alive, int complete) {
    int queued = 0;
    int active = 0;
    uint32_t files = 0;
    uint64_t bytes = 0;
    uint32_t i;
    
    if (lock_fd(ipc_fd) == 0) {
        queued = shared->job_count;
        active = shared->active_jobs;
        files = shared->result_count;
        
        for (i = 0; i < shared->worker_count; i++) {
            bytes += shared->stats[i].bytes_emitted;
        }
        unlock_fd(ipc_fd);
    }
    
    printf("STATUS queued_jobs=%d active_jobs=%d files=%u bytes=%llu workers_alive=%d complete=%d\n",
           queued, active, files, (unsigned long long)bytes, workers_alive, complete);
    fflush(stdout);
}

static void request_shutdown(int ipc_fd, T4Shared *shared, pid_t *pids, int *alive, int workers) {
    int i;
    
    if (lock_fd(ipc_fd) == 0) {
        shared->shutdown_requested = 1;
        shared->done = 1;
        unlock_fd(ipc_fd);
    }
    
    for (i = 0; i < workers; i++) {
        if (alive[i]) {
            kill(pids[i], SIGTERM);
        }
    }
}

static void reap_workers(int ipc_fd, T4Shared *shared, pid_t *pids, int *alive, int workers, int *workers_alive) {
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int i;
        int exit_status = 1;
        
        if (WIFEXITED(status)) {
            exit_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exit_status = 128 + WTERMSIG(status);
        }
        
        for (i = 0; i < workers; i++) {
            if (alive[i] && pids[i] == pid) {
                alive[i] = 0;
                (*workers_alive)--;
                
                if (lock_fd(ipc_fd) == 0) {
                    shared->stats[i].exit_status = exit_status;
                    unlock_fd(ipc_fd);
                }
                break;
            }
        }
    }
}

static int write_inventory_db_atomic(const char *db_path, T4Shared *shared, int complete) {
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
    header.complete = complete ? 1U : 0U;
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
    if (!file) {
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
    
    if (header.complete > 1) {
        fprintf(stderr, "Invalid complete flag\n");
        return 1;
    }
    
    expected_size = (off_t)sizeof(T4DbHeader) + 
                    (off_t)header.file_record_count * (off_t)sizeof(T4FileRecord) + 
                    (off_t)header.worker_count * (off_t)sizeof(T4WorkerStats);
                    
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
    
    printf("magic=%.4s\nversion=%u\ncomplete=%u\nfile_record_count=%u\nworker_count=%u\n", 
           header.magic, header.version, header.complete, header.file_record_count, header.worker_count);
           
    return 0;
}

static int inventory_mode(const ManagerConfig *cfg) {
    int ipc_fd;
    int pipefd[2];
    int alive[T4_MAX_WORKERS];
    int workers_alive;
    int shutdown_started = 0;
    int force_killed = 0;
    time_t shutdown_time = 0;
    T4Shared *shared;
    pid_t pids[T4_MAX_WORKERS];
    int complete;
    
    memset(pids, 0, sizeof(pids));
    memset(alive, 0, sizeof(alive));
    
    install_handlers();
    
    if (!create_ipc(cfg, &ipc_fd, &shared)) {
        return 1;
    }
    
    if (!write_pid_file(cfg->pid_file)) {
        munmap(shared, sizeof(T4Shared));
        close(ipc_fd);
        return 1;
    }
    
    if (pipe(pipefd) != 0) {
        perror("pipe");
        munmap(shared, sizeof(T4Shared));
        close(ipc_fd);
        return 1;
    }
    
    fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL, 0) | O_NONBLOCK);
    
    if (!start_workers(cfg, pids, pipefd[1])) {
        close(pipefd[0]);
        close(pipefd[1]);
        munmap(shared, sizeof(T4Shared));
        close(ipc_fd);
        return 1;
    }
    
    close(pipefd[1]);
    
    for (int i = 0; i < cfg->workers; i++) {
        alive[i] = 1;
    }
    
    workers_alive = cfg->workers;

    while (workers_alive > 0) {
        drain_control_pipe(pipefd[0]);
        
        if (g_want_status) {
            g_want_status = 0;
            print_status(ipc_fd, shared, workers_alive, 0);
        }
        
        if (g_want_shutdown && !shutdown_started) {
            shutdown_started = 1;
            shutdown_time = time(NULL);
            request_shutdown(ipc_fd, shared, pids, alive, cfg->workers);
        }
        
        if (g_got_sigchld) {
            g_got_sigchld = 0;
        }
        
        reap_workers(ipc_fd, shared, pids, alive, cfg->workers, &workers_alive);
        
        if (shutdown_started && workers_alive > 0 && time(NULL) - shutdown_time >= cfg->graceful_timeout) {
            for (int i = 0; i < cfg->workers; i++) {
                if (alive[i]) {
                    kill(pids[i], SIGKILL);
                    force_killed = 1;
                }
            }
        }
        usleep(20000);
    }
    
    drain_control_pipe(pipefd[0]);
    close(pipefd[0]);
    
    complete = (!shutdown_started && !g_want_shutdown && !force_killed && shared->done) ? 1 : 0;
    
    if (!write_inventory_db_atomic(cfg->db_path, shared, complete)) {
        munmap(shared, sizeof(T4Shared));
        close(ipc_fd);
        return 1;
    }
    
    printf("fileops_manager: inventory %s\n", complete ? "complete" : "incomplete");
    printf("file_record_count=%u\nworker_count=%u\ncomplete=%d\n", shared->result_count, shared->worker_count, complete);
    
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