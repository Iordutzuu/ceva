#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_COMM 64
#define MAX_CMDLINE 256
#define MAX_PATH_LEN 1024
#define RSS_CHANGE_LIMIT 1024

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

typedef struct {
    ProcRecord *records;
    uint32_t count;
} ProcDb;

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

typedef struct {
    FileRecord *records;
    uint32_t count;
} FileDb;

int parse_args(int argc, char *argv[], char *old_path, size_t old_size,
               char *new_path, size_t new_size,
               char *out_path, size_t out_size) {
    int i;

    old_path[0] = '\0';
    new_path[0] = '\0';
    out_path[0] = '\0';

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--old") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --old\n");
                return 0;
            }
            snprintf(old_path, old_size, "%s", argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--new") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --new\n");
                return 0;
            }
            snprintf(new_path, new_size, "%s", argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "--out") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for --out\n");
                return 0;
            }
            snprintf(out_path, out_size, "%s", argv[i + 1]);
            i++;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 0;
        }
    }

    if (old_path[0] == '\0' || new_path[0] == '\0' || out_path[0] == '\0') {
        fprintf(stderr, "Usage: db_diff --old OLD_DB --new NEW_DB --out OUT_FILE\n");
        return 0;
    }

    return 1;
}

int peek_header(const char *path, char magic_out[5], uint32_t *version_out) {
    FILE *file;
    struct {
        char m[4];
        uint32_t v;
    } hdr;

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    if (fread(&hdr, sizeof(hdr), 1, file) != 1) {
        fclose(file);
        return 0;
    }

    memcpy(magic_out, hdr.m, 4);
    magic_out[4] = '\0';
    *version_out = hdr.v;

    fclose(file);
    return 1;
}

int read_proc_db(const char *path, ProcDb *db) {
    FILE *file;
    ProcDbHeader header;
    size_t read_count;

    db->records = NULL;
    db->count = 0;

    file = fopen(path, "rb");
    if (file == NULL) {
        perror("fopen db");
        return 0;
    }

    if (fread(&header, sizeof(header), 1, file) != 1) {
        fprintf(stderr, "Could not read header from %s\n", path);
        fclose(file);
        return 0;
    }

    db->count = header.record_count;

    if (db->count == 0) {
        fclose(file);
        return 1;
    }

    db->records = malloc((size_t)db->count * sizeof(ProcRecord));
    if (db->records == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(file);
        return 0;
    }

    read_count = fread(db->records, sizeof(ProcRecord), db->count, file);
    if (read_count < db->count) {
        db->count = read_count;
    }

    fclose(file);
    return 1;
}

ProcRecord *find_by_pid(ProcDb *db, int32_t pid) {
    uint32_t i;

    for (i = 0; i < db->count; i++) {
        if (db->records[i].pid == pid) {
            return &db->records[i];
        }
    }

    return NULL;
}

long absolute_long(long value) {
    if (value < 0) {
        return -value;
    }
    return value;
}

void write_proc_diff(FILE *out, ProcDb *old_db, ProcDb *new_db) {
    uint32_t i;
    ProcRecord *match;
    long rss_diff;

    fprintf(out, "=== PROC DIFF ===\n\n");

    fprintf(out, "[NEW PROCESSES]\n");
    for (i = 0; i < new_db->count; i++) {
        match = find_by_pid(old_db, new_db->records[i].pid);
        if (match == NULL) {
            fprintf(out, "PID %d COMM %s RSS %ld KB\n",
                    new_db->records[i].pid,
                    new_db->records[i].comm,
                    new_db->records[i].rss_kb);
        }
    }

    fprintf(out, "\n[TERMINATED PROCESSES]\n");
    for (i = 0; i < old_db->count; i++) {
        match = find_by_pid(new_db, old_db->records[i].pid);
        if (match == NULL) {
            fprintf(out, "PID %d COMM %s RSS %ld KB\n",
                    old_db->records[i].pid,
                    old_db->records[i].comm,
                    old_db->records[i].rss_kb);
        }
    }

    fprintf(out, "\n[MODIFIED PROCESSES]\n");
    for (i = 0; i < new_db->count; i++) {
        match = find_by_pid(old_db, new_db->records[i].pid);
        if (match != NULL) {
            rss_diff = absolute_long(new_db->records[i].rss_kb - match->rss_kb);

            if (rss_diff > RSS_CHANGE_LIMIT ||
                new_db->records[i].state != match->state ||
                new_db->records[i].ppid != match->ppid) {
                fprintf(out,
                        "PID %d COMM %s: PPID %d -> %d, STATE %c -> %c, RSS %ld KB -> %ld KB\n",
                        new_db->records[i].pid,
                        new_db->records[i].comm,
                        match->ppid,
                        new_db->records[i].ppid,
                        match->state,
                        new_db->records[i].state,
                        match->rss_kb,
                        new_db->records[i].rss_kb);
            }
        }
    }
}

int read_file_db(const char *path, FileDb *db) {
    FILE *file;
    FileDbHeader header;
    size_t read_count;

    db->records = NULL;
    db->count = 0;

    file = fopen(path, "rb");
    if (file == NULL) {
        perror("fopen file db");
        return 0;
    }

    if (fread(&header, sizeof(header), 1, file) != 1) {
        fprintf(stderr, "Could not read header from %s\n", path);
        fclose(file);
        return 0;
    }

    db->count = header.record_count;

    if (db->count == 0) {
        fclose(file);
        return 1;
    }

    db->records = malloc((size_t)db->count * sizeof(FileRecord));
    if (db->records == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        fclose(file);
        return 0;
    }

    read_count = fread(db->records, sizeof(FileRecord), db->count, file);
    if (read_count < db->count) {
        db->count = read_count;
    }

    fclose(file);
    return 1;
}

FileRecord *find_file_by_path(FileDb *db, const char *path) {
    uint32_t i;

    for (i = 0; i < db->count; i++) {
        if (strcmp(db->records[i].absolute_path, path) == 0) {
            return &db->records[i];
        }
    }

    return NULL;
}

void write_file_diff(FILE *out, FileDb *old_db, FileDb *new_db) {
    uint32_t i;
    FileRecord *match;

    fprintf(out, "=== FILE DIFF ===\n\n");

    fprintf(out, "[ADDED]\n");
    for (i = 0; i < new_db->count; i++) {
        match = find_file_by_path(old_db, new_db->records[i].absolute_path);
        if (match == NULL) {
            fprintf(out, "%s\n", new_db->records[i].absolute_path);
        }
    }

    fprintf(out, "\n[DELETED]\n");
    for (i = 0; i < old_db->count; i++) {
        match = find_file_by_path(new_db, old_db->records[i].absolute_path);
        if (match == NULL) {
            fprintf(out, "%s\n", old_db->records[i].absolute_path);
        }
    }

    fprintf(out, "\n[MODIFIED]\n");
    for (i = 0; i < new_db->count; i++) {
        match = find_file_by_path(old_db, new_db->records[i].absolute_path);
        if (match != NULL) {
            if (new_db->records[i].size != match->size ||
                new_db->records[i].mtime != match->mtime ||
                new_db->records[i].hash != match->hash ||
                strcmp(new_db->records[i].type, match->type) != 0 ||
                strcmp(new_db->records[i].symlink_target, match->symlink_target) != 0) {
                
                fprintf(out, "%s\n", new_db->records[i].absolute_path);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    char old_path[256];
    char new_path[256];
    char out_path[256];
    
    char old_magic[5];
    char new_magic[5];
    uint32_t old_version;
    uint32_t new_version;
    
    FILE *out;

    if (!parse_args(argc, argv, old_path, sizeof(old_path),
                    new_path, sizeof(new_path),
                    out_path, sizeof(out_path))) {
        return 1;
    }

    if (!peek_header(old_path, old_magic, &old_version)) {
        fprintf(stderr, "Could not read magic from old db\n");
        return 1;
    }

    if (!peek_header(new_path, new_magic, &new_version)) {
        fprintf(stderr, "Could not read magic from new db\n");
        return 1;
    }

    if (strcmp(old_magic, new_magic) != 0) {
        fprintf(stderr, "Error: Mismatched DB types (%s vs %s)\n", old_magic, new_magic);
        return 1;
    }

    if (old_version != new_version) {
        fprintf(stderr, "Error: Mismatched DB versions\n");
        return 1;
    }

    out = fopen(out_path, "w");
    if (out == NULL) {
        perror("fopen output");
        return 1;
    }

    if (strcmp(old_magic, "PRC1") == 0) {
        ProcDb old_db;
        ProcDb new_db;

        if (!read_proc_db(old_path, &old_db) || !read_proc_db(new_path, &new_db)) {
            fclose(out);
            return 1;
        }

        write_proc_diff(out, &old_db, &new_db);

        free(old_db.records);
        free(new_db.records);

    } else if (strcmp(old_magic, "IDX1") == 0) {
        FileDb old_db;
        FileDb new_db;

        if (!read_file_db(old_path, &old_db) || !read_file_db(new_path, &new_db)) {
            fclose(out);
            return 1;
        }

        write_file_diff(out, &old_db, &new_db);

        free(old_db.records);
        free(new_db.records);

    } else {
        fprintf(stderr, "Unknown DB format\n");
        fclose(out);
        return 1;
    }

    fclose(out);

    printf("db_diff: report written to %s\n", out_path);

    return 0;
}