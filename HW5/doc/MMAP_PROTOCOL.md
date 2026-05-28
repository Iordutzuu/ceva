# MMAP_PROTOCOL.md

## 1. Scop

Acest document descrie protocolul IPC folosit in Tema T4.

Comunicarea dintre `fileops_manager` si `fileops_worker` se face printr-un fisier mapat in memorie cu:

mmap(..., MAP_SHARED, ...)

Fisierul implicit este:

data/ipc.mmap

Managerul creeaza fisierul IPC, il dimensioneaza si initializeaza structurile partajate.
Workerii deschid acelasi fisier si il mapeaza in memoria lor.

---

## 2. Executabile implicate

Manager:

bin/fileops_manager

Worker:

bin/fileops_worker

Managerul este rulat de utilizator prin:

./tools/fileops.sh run -- fileops_manager --root <dir> --workers <N>

Workerii sunt porniti doar de manager prin fork() si exec().

Interfata minima a workerului este:

fileops_worker --worker-id <id> --ipc <path>

---

## 3. Layout-ul zonei mmap

Zona partajata contine o structura de tip `T4Shared`.

Aceasta include:

- header IPC
- coada de job-uri
- informatii despre job-urile active
- buffer de rezultate
- statistici per worker

Structura logica este:

[IPC HEADER]
[JOB QUEUE]
[RESULT BUFFER]
[WORKER STATS]

---

## 4. Header IPC

Header-ul contine:

magic = "IPC4"
version = 1
worker_count = numarul de workeri
max_depth = adancimea maxima de scanare
simulate_work_ms = intarziere optionala pentru testare

Campul `magic` este folosit pentru a verifica daca fisierul mmap are formatul corect.

---

## 5. Job queue

Un job reprezinta un director care trebuie scanat.

Structura unui job:

path  = calea directorului
depth = adancimea directorului fata de root

Managerul introduce initial un singur job: directorul root.

Workerii:
1. preiau job-uri din coada
2. scaneaza directoarele
3. adauga subdirectoare noi ca job-uri
4. respecta limita `max_depth`

Coada este implementata ca buffer circular cu:

job_head
job_tail
job_count

Cand se adauga un job, se scrie in pozitia `job_tail`.
Cand se extrage un job, se citeste din pozitia `job_head`.

Indicii se actualizeaza modulo capacitatea cozii.

---

## 6. Terminarea workerilor

Zona partajata contine:

active_jobs
done

`active_jobs` numara cate directoare sunt procesate in acel moment.

Un worker se opreste cand:

job_count == 0
active_jobs == 0

In acel moment se seteaza:

done = 1

Aceasta regula impiedica oprirea prematura atunci cand un worker proceseaza un director si poate descoperi subdirectoare noi.

---

## 7. Result buffer

Workerii nu scriu direct in DB-ul final.

Pentru fiecare fisier regulat gasit, workerul produce un `T4FileRecord` si il pune in bufferul de rezultate din mmap.

Un record contine:

path absolut
size
mtime
mode
uid
gid
sha256

Managerul citeste rezultatele din zona mmap dupa terminarea workerilor si le scrie in DB-ul final.

---

## 8. Backpressure

Bufferul de rezultate are capacitate fixa:

T4_MAX_RESULTS = 8192

Workerii verifica daca exista spatiu in buffer inainte de a adauga un record.

Daca bufferul este plin, recordul nu este suprascris. Aceasta regula previne coruperea sau suprascrierea datelor.

In scenariul de test, numarul de fisiere este mic si nu umple bufferul.

---

## 9. Sincronizare

Pentru sincronizare se foloseste `fcntl` pe fisierul IPC.

Operatiile protejate prin lock sunt:

- preluarea unui job
- introducerea unui job nou
- actualizarea active_jobs si done
- publicarea rezultatelor
- scrierea statisticilor finale

Aceasta solutie este un mecanism POSIX si functioneaza intre procese diferite.

Nu se bazeaza exclusiv pe `sleep()` sau pe presupuneri despre viteza proceselor.

---

## 10. Statistici worker

Fiecare worker scrie in zona IPC:

worker_id
pid
exit_status
jobs_processed
files_emitted
bytes_emitted
wall_time_ms
user_cpu_us
sys_cpu_us

Valorile user_cpu_us si sys_cpu_us sunt obtinute prin getrusage(RUSAGE_SELF).

---

## 11. Responsabilitati

Managerul:
- creeaza fisierul IPC
- initializeaza mmap
- porneste workerii
- asteapta terminarea workerilor
- scrie DB-ul final

Workerul:
- deschide si mapeaza IPC-ul
- preia job-uri
- scaneaza directoare
- publica rezultate
- actualizeaza statistici

Workerii nu scriu DB-ul final.