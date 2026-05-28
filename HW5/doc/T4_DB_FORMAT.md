# T4_DB_FORMAT.md

## 1. Scop

Acest document descrie formatul bazei de date binare finale generate de Tema T4.

Fisierul implicit este:

data/inventory.db

Baza de date este scrisa doar de `fileops_manager`.

Workerii nu scriu direct in DB-ul final.

---

## 2. Structura generala

Fisierul DB are urmatoarea structura:

[HEADER][FILE RECORDS][WORKER STATS]

Header-ul apare o singura data la inceput.
Dupa header urmeaza toate file records.
La final urmeaza statisticile workerilor.

---

## 3. Header DB

Header-ul este reprezentat de structura `T4DbHeader`.

Campuri:

magic
version
complete
file_record_count
worker_count

---

## 4. Campurile header-ului

magic:

Valoare: "INV4"

Identifica fisierul ca fiind o baza de date de inventariere pentru Tema T4.

version:

Valoare: 1

Indica versiunea formatului binar.

complete:

Valoare: 1 daca inventarierea s-a terminat normal.

file_record_count:

Numarul de fisiere regulate salvate in DB.

worker_count:

Numarul de workeri folositi la inventariere.

---

## 5. File record

Fiecare fisier regulat este salvat intr-un record de tip `T4FileRecord`.

Campuri:

path
size
mtime
mode
uid
gid
sha256

---

## 6. Campurile file record

path:

Calea absoluta a fisierului.

size:

Dimensiunea fisierului in bytes.

mtime:

Timpul ultimei modificari.

mode:

Modul fisierului, obtinut prin lstat/stat.

uid:

ID-ul utilizatorului proprietar.

gid:

ID-ul grupului proprietar.

sha256:

Hash SHA256 calculat pe continutul fisierului.

---

## 7. Worker stats

Dupa file records, DB-ul contine statistici pentru fiecare worker.

Structura folosita este `T4WorkerStats`.

Campuri:

worker_id
pid
exit_status
jobs_processed
files_emitted
bytes_emitted
wall_time_ms
user_cpu_us
sys_cpu_us

---

## 8. Campurile worker stats

worker_id:

ID-ul logic al workerului.

pid:

PID-ul procesului worker.

exit_status:

Codul de iesire colectat de manager prin waitpid.

jobs_processed:

Numarul de directoare procesate de worker.

files_emitted:

Numarul de file records produse de worker.

bytes_emitted:

Suma dimensiunilor fisierelor emise de worker.

wall_time_ms:

Timpul total de executie al workerului, in milisecunde.

user_cpu_us:

Timp CPU in user mode, obtinut cu getrusage.

sys_cpu_us:

Timp CPU in kernel mode, obtinut cu getrusage.

---

## 9. Scriere atomica

DB-ul final este scris atomic de manager.

Pasii sunt:

1. managerul scrie intr-un fisier temporar
2. managerul scrie header + file records + worker stats
3. managerul inchide fisierul temporar
4. managerul publica DB-ul final prin rename()

Astfel se evita aparitia unui DB partial in cazul unei erori.

---

## 10. Verify

Modul:

./tools/fileops.sh run -- fileops_manager --db data/inventory.db --verify

Verifica:

- magic == INV4
- version == 1
- dimensiunea fisierului este consistenta
- file_record_count este compatibil cu dimensiunea DB
- worker_count este compatibil cu dimensiunea DB

Daca DB-ul este valid, programul se termina cu exit code 0 si afiseaza:

verify=OK

---

## 11. Dump

Modul:

./tools/fileops.sh run -- fileops_manager --db data/inventory.db --dump

Afiseaza un sumar stabil:

magic=INV4
version=1
complete=1
file_record_count=<numar>
worker_count=<numar>

Acest output poate fi verificat automat in test.

---

## 12. Limite alese

T4_MAX_PATH = 512
T4_MAX_JOBS = 256
T4_MAX_RESULTS = 8192
T4_MAX_WORKERS = 16
T4_HASH_SIZE = 32

Aceste limite sunt fixe pentru a simplifica formatul binar si memoria partajata.