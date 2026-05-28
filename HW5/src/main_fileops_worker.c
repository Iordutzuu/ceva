#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

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
    int32_t shutdown_requested;

    uint32_t result_count;

    T4Job jobs[T4_MAX_JOBS];
    T4FileRecord results[T4_MAX_RESULTS];
    T4WorkerStats stats[T4_MAX_WORKERS];
} T4Shared;

typedef struct {
    int worker_id;
    int control_fd;
    char ipc_path[T4_MAX_PATH];
} WorkerConfig;

static volatile sig_atomic_t g_worker_stop = 0;

static void on_worker_sigterm(int sig) {
    (void)sig;
    g_worker_stop = 1;
}

static void install_worker_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_worker_sigterm;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
}

static void send_t5msg(int fd, const char *msg) {
    size_t len;
    if (fd < 0 || msg == NULL) {
        return;
    }
    len = strlen(msg);
    if (len > 0 && len < 512) {
        (void)write(fd, msg, len);
    }
}

static void send_worker_exiting(int fd, int worker_id, const char *reason) {
    char line[256];
    snprintf(line, sizeof(line), "T5MSG type=WORKER_EXITING worker_id=%d reason=%s\n",
             worker_id, reason);
    send_t5msg(fd, line);
}

static void send_job_done(int fd, int worker_id, unsigned long long jobs, unsigned long long files, unsigned long long bytes) {
    char line[256];
    snprintf(line, sizeof(line),
             "T5MSG type=JOB_DONE worker_id=%d jobs=%llu files=%llu bytes=%llu\n",
             worker_id, jobs, files, bytes);
    send_t5msg(fd, line);
}

static void send_error_msg(int fd, int worker_id, int err_no, const char *where) {
    char line[256];
    snprintf(line, sizeof(line), "T5MSG type=ERROR worker_id=%d errno=%d where=%s\n",
             worker_id, err_no, where);
    send_t5msg(fd, line);
}

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

static uint64_t now_ms(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
}

static void sleep_ms(int ms) {
    struct timespec ts;

    if (ms <= 0) {
        return;
    }

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;

    nanosleep(&ts, NULL);
}

static int parse_args(int argc, char *argv[], WorkerConfig *cfg) {
    int i;

    cfg->worker_id = -1;
    cfg->control_fd = -1;
    cfg->ipc_path[0] = '\0';

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--worker-id") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --worker-id\n");
                return 0;
            }
            cfg->worker_id = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--ipc") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --ipc\n");
                return 0;
            }
            snprintf(cfg->ipc_path, sizeof(cfg->ipc_path), "%s", argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--control-fd") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --control-fd\n");
                return 0;
            }
            cfg->control_fd = atoi(argv[i + 1]);
            i++;
        } else {
            fprintf(stderr, "Unknown worker argument: %s\n", argv[i]);
            return 0;
        }
    }

    if (cfg->worker_id < 0 || cfg->worker_id >= T4_MAX_WORKERS) {
        fprintf(stderr, "Invalid worker id\n");
        return 0;
    }

    if (cfg->ipc_path[0] == '\0') {
        fprintf(stderr, "Missing --ipc\n");
        return 0;
    }

    if (cfg->control_fd < 0) {
        fprintf(stderr, "Missing --control-fd\n");
        return 0;
    }

    return 1;
}

static int open_shared(const char *ipc_path, int *fd_out, T4Shared **shared_out) {
    int fd;
    T4Shared *shared;

    fd = open(ipc_path, O_RDWR);
    if (fd < 0) {
        perror("open ipc");
        return 0;
    }

    shared = mmap(NULL, sizeof(T4Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        perror("mmap ipc");
        close(fd);
        return 0;
    }

    if (memcmp(shared->magic, "IPC4", 4) != 0 || shared->version != 1) {
        fprintf(stderr, "Invalid IPC format\n");
        munmap(shared, sizeof(T4Shared));
        close(fd);
        return 0;
    }

    *fd_out = fd;
    *shared_out = shared;

    return 1;
}

static int pop_job(int fd, T4Shared *shared, T4Job *job) {
    int result = 0;

    if (lock_fd(fd) != 0) {
        return -1;
    }

    if (shared->done || shared->shutdown_requested || g_worker_stop) {
        result = 0;
    } else if (shared->job_count > 0) {
        *job = shared->jobs[shared->job_head];
        shared->job_head = (shared->job_head + 1) % T4_MAX_JOBS;
        shared->job_count--;
        shared->active_jobs++;
        result = 1;
    } else if (shared->active_jobs == 0) {
        shared->done = 1;
        result = 0;
    } else {
        result = 2;
    }

    unlock_fd(fd);

    return result;
}

static int push_job(int fd, T4Shared *shared, const char *path, int depth) {
    int result = 0;

    if (lock_fd(fd) != 0) {
        return 0;
    }

    if (shared->job_count < T4_MAX_JOBS && !shared->done && !shared->shutdown_requested && !g_worker_stop) {
        snprintf(shared->jobs[shared->job_tail].path,
                 sizeof(shared->jobs[shared->job_tail].path),
                 "%s", path);
        shared->jobs[shared->job_tail].depth = depth;
        shared->job_tail = (shared->job_tail + 1) % T4_MAX_JOBS;
        shared->job_count++;
        result = 1;
    }

    unlock_fd(fd);

    return result;
}

static void finish_job(int fd, T4Shared *shared) {
    if (lock_fd(fd) != 0) {
        return;
    }

    if (shared->active_jobs > 0) {
        shared->active_jobs--;
    }

    if (shared->job_count == 0 && shared->active_jobs == 0) {
        shared->done = 1;
    }

    unlock_fd(fd);
}

/* Minimal SHA256 implementation */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    unsigned char data[64];
    uint32_t datalen;
} SHA256_CTX;

static uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

static void sha256_transform(SHA256_CTX *ctx, const unsigned char data[]) {
    static const uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };

    uint32_t a, b, c, d, e, f, g, h;
    uint32_t m[64];
    uint32_t i;

    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i * 4] << 24)
            | ((uint32_t)data[i * 4 + 1] << 16)
            | ((uint32_t)data[i * 4 + 2] << 8)
            | ((uint32_t)data[i * 4 + 3]);
    }

    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[i] + m[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
}

static void sha256_update(SHA256_CTX *ctx, const unsigned char *data, size_t len) {
    size_t i;

    for (i = 0; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;

        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, unsigned char hash[32]) {
    uint32_t i = ctx->datalen;
    uint32_t j;

    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80U;
        while (i < 56) {
            ctx->data[i++] = 0x00U;
        }
    } else {
        ctx->data[i++] = 0x80U;
        while (i < 64) {
            ctx->data[i++] = 0x00U;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += (uint64_t)ctx->datalen * 8ULL;

    ctx->data[63] = (unsigned char)(ctx->bitlen);
    ctx->data[62] = (unsigned char)(ctx->bitlen >> 8);
    ctx->data[61] = (unsigned char)(ctx->bitlen >> 16);
    ctx->data[60] = (unsigned char)(ctx->bitlen >> 24);
    ctx->data[59] = (unsigned char)(ctx->bitlen >> 32);
    ctx->data[58] = (unsigned char)(ctx->bitlen >> 40);
    ctx->data[57] = (unsigned char)(ctx->bitlen >> 48);
    ctx->data[56] = (unsigned char)(ctx->bitlen >> 56);

    sha256_transform(ctx, ctx->data);

    for (j = 0; j < 4; j++) {
        for (i = 0; i < 8; i++) {
            hash[i * 4 + j] = (unsigned char)((ctx->state[i] >> (24 - j * 8)) & 0xffU);
        }
    }
}

static int sha256_file(const char *path, unsigned char hash[32]) {
    FILE *file;
    SHA256_CTX ctx;
    unsigned char buffer[4096];
    size_t n;

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    sha256_init(&ctx);

    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        sha256_update(&ctx, buffer, n);
    }

    if (ferror(file)) {
        fclose(file);
        return 0;
    }

    fclose(file);
    sha256_final(&ctx, hash);

    return 1;
}

static int push_result(int fd, T4Shared *shared, const T4FileRecord *record) {
    int result = 0;

    if (lock_fd(fd) != 0) {
        return 0;
    }

    if (shared->result_count < T4_MAX_RESULTS) {
        shared->results[shared->result_count] = *record;
        shared->result_count++;
        result = 1;
    }

    unlock_fd(fd);

    return result;
}

static int emit_file_record(int fd, T4Shared *shared, int worker_id, const char *path, const struct stat *st) {
    T4FileRecord record;

    memset(&record, 0, sizeof(record));

    if (realpath(path, record.path) == NULL) {
        snprintf(record.path, sizeof(record.path), "%s", path);
    }

    record.size = (uint64_t)st->st_size;
    record.mtime = (int64_t)st->st_mtime;
    record.mode = (uint32_t)st->st_mode;
    record.uid = (uint32_t)st->st_uid;
    record.gid = (uint32_t)st->st_gid;

    if (!sha256_file(path, record.sha256)) {
        return 0;
    }

    if (!push_result(fd, shared, &record)) {
        return 0;
    }

    shared->stats[worker_id].files_emitted++;
    shared->stats[worker_id].bytes_emitted += record.size;

    return 1;
}

static int process_directory(int fd, T4Shared *shared, int worker_id, int control_fd, const T4Job *job) {
    DIR *dir;
    struct dirent *entry;

    dir = opendir(job->path);
    if (dir == NULL) {
        send_error_msg(control_fd, worker_id, errno, "opendir");
        return 0;
    }

    shared->stats[worker_id].jobs_processed++;

    while ((entry = readdir(dir)) != NULL) {
        char child_path[T4_MAX_PATH];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (snprintf(child_path, sizeof(child_path), "%s/%s", job->path, entry->d_name) >= (int)sizeof(child_path)) {
            continue;
        }

        if (lstat(child_path, &st) != 0) {
            continue;
        }

        if (S_ISLNK(st.st_mode)) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (!g_worker_stop && !shared->shutdown_requested &&
                (shared->max_depth < 0 || job->depth + 1 <= shared->max_depth)) {
                push_job(fd, shared, child_path, job->depth + 1);
            }
        } else if (S_ISREG(st.st_mode)) {
            if (!emit_file_record(fd, shared, worker_id, child_path, &st)) {
                send_error_msg(control_fd, worker_id, errno, "emit_file_record");
            }
        }
    }

    closedir(dir);
    return 1;
}

static void save_final_stats(int fd, T4Shared *shared, int worker_id, uint64_t start_ms) {
    struct rusage usage;
    uint64_t end_ms = now_ms();

    getrusage(RUSAGE_SELF, &usage);

    if (lock_fd(fd) != 0) {
        return;
    }

    shared->stats[worker_id].worker_id = worker_id;
    shared->stats[worker_id].pid = (int32_t)getpid();
    shared->stats[worker_id].wall_time_ms = end_ms - start_ms;
    shared->stats[worker_id].user_cpu_us =
        ((uint64_t)usage.ru_utime.tv_sec * 1000000ULL) + (uint64_t)usage.ru_utime.tv_usec;
    shared->stats[worker_id].sys_cpu_us =
        ((uint64_t)usage.ru_stime.tv_sec * 1000000ULL) + (uint64_t)usage.ru_stime.tv_usec;

    unlock_fd(fd);
}

int main(int argc, char *argv[]) {
    WorkerConfig cfg;
    int fd;
    T4Shared *shared;
    uint64_t start_ms;

    if (!parse_args(argc, argv, &cfg)) {
        return 1;
    }

    install_worker_handler();

    if (!open_shared(cfg.ipc_path, &fd, &shared)) {
        return 1;
    }

    start_ms = now_ms();

    if (lock_fd(fd) == 0) {
        shared->stats[cfg.worker_id].worker_id = cfg.worker_id;
        shared->stats[cfg.worker_id].pid = (int32_t)getpid();
        unlock_fd(fd);
    }

    while (1) {
        T4Job job;
        int pop_result = pop_job(fd, shared, &job);

        if (pop_result == 1) {
            sleep_ms(shared->simulate_work_ms);
            {
                uint64_t before_jobs = shared->stats[cfg.worker_id].jobs_processed;
                uint64_t before_files = shared->stats[cfg.worker_id].files_emitted;
                uint64_t before_bytes = shared->stats[cfg.worker_id].bytes_emitted;
                process_directory(fd, shared, cfg.worker_id, cfg.control_fd, &job);
                finish_job(fd, shared);
                send_job_done(cfg.control_fd, cfg.worker_id,
                              (unsigned long long)(shared->stats[cfg.worker_id].jobs_processed - before_jobs),
                              (unsigned long long)(shared->stats[cfg.worker_id].files_emitted - before_files),
                              (unsigned long long)(shared->stats[cfg.worker_id].bytes_emitted - before_bytes));
            }
        } else if (pop_result == 0) {
            break;
        } else {
            sleep_ms(10);
        }
    }

    save_final_stats(fd, shared, cfg.worker_id, start_ms);
    send_worker_exiting(cfg.control_fd, cfg.worker_id, g_worker_stop ? "shutdown" : "normal");

    munmap(shared, sizeof(T4Shared));
    close(fd);

    return 0;
}