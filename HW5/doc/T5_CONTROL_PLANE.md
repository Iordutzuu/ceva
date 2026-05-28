# T5_CONTROL_PLANE

## 1. Ideea generală

Tema T5 adaugă un canal separat de control peste tema T4.

- **Data plane**: rămâne în `mmap`, ca în T4. Joburile, rezultatele și statisticile se află în structura partajată.
- **Control plane**: este un `pipe` anonim prin care workerii trimit mesaje scurte către manager.
- **Signal plane**: managerul reacționează la `SIGUSR1`, `SIGINT`, `SIGTERM`, `SIGCHLD`; workerul reacționează la `SIGTERM`.

## 2. Pipe worker -> manager

Managerul creează un pipe anonim înainte să pornească workerii:

```c
pipe(pipefd);
```

- `pipefd[0]` este capătul de citire, păstrat de manager.
- `pipefd[1]` este capătul de scriere, moștenit de workeri prin `fork()` + `exec()`.

Managerul pornește workerii cu argumentul:

```text
--control-fd <fd>
```

Exemplu:

```text
fileops_worker --worker-id 0 --ipc data/ipc.mmap --control-fd 4
```

## 3. Formatul T5MSG

Fiecare mesaj:

- începe cu `T5MSG`;
- se termină cu newline;
- este trimis cu un singur `write()`;
- are dimensiune mică, sub `PIPE_BUF`.

Format folosit:

```text
T5MSG type=<TYPE> worker_id=<id> key=value ...
```

Mesaje implementate:

```text
T5MSG type=JOB_DONE worker_id=0 jobs=1 files=3 bytes=120
T5MSG type=WORKER_EXITING worker_id=0 reason=normal
T5MSG type=WORKER_EXITING worker_id=0 reason=shutdown
T5MSG type=ERROR worker_id=0 errno=13 where=opendir
```

## 4. SIGUSR1 pentru status

Handlerul pentru `SIGUSR1` nu face `printf()` direct. El setează doar un flag de tip `volatile sig_atomic_t`.

Bucla principală din manager verifică flagul și afișează o linie stabilă:

```text
STATUS queued_jobs=<n> active_jobs=<n> files=<n> bytes=<n> workers_alive=<n> complete=<0|1>
```

## 5. Shutdown grațios

La `SIGINT` sau `SIGTERM`, managerul:

1. setează `shutdown_requested=1` în `mmap`;
2. setează `done=1`, ca workerii să nu mai ia joburi noi;
3. trimite `SIGTERM` către toți workerii vii;
4. așteaptă maximum `--graceful-timeout <sec>`;
5. dacă mai există workeri vii după timeout, trimite `SIGKILL`;
6. face `waitpid()` până nu rămân zombie;
7. scrie DB-ul final cu `complete=0`.

Workerul are handler pentru `SIGTERM`. Când primește semnalul:

- nu mai ia joburi noi;
- nu mai adaugă joburi noi în coadă;
- termină jobul curent cât poate de curat;
- trimite `WORKER_EXITING`;
- iese determinist.

## 6. DB complet sau incomplet

DB-ul este scris atomic de manager, prin fișier temporar urmat de `rename()`.

Valori pentru `complete`:

- `complete=1`: inventarierea s-a terminat normal;
- `complete=0`: inventarierea a fost întreruptă prin `SIGINT`/`SIGTERM` sau prin timeout.

Important: `complete=0` nu înseamnă DB corupt. Înseamnă DB valid structural, dar inventariere incompletă.

`--verify` acceptă și DB-uri cu `complete=0`, dacă structura este validă.

`--dump` afișează mereu cheia:

```text
complete=<0|1>
```

## 7. Argumente T5 adăugate

Manager:

```text
--graceful-timeout <sec>
--simulate-work-ms <ms>
--pid-file <path>
```

Worker:

```text
--control-fd <fd>
```

## 8. Test T5

Testul principal este:

```text
tests/t5_signals_pipes.sh
```

El verifică:

1. pornirea managerului cu `--pid-file`;
2. trimiterea lui `SIGUSR1`;
3. apariția unei linii `STATUS`;
4. trimiterea lui `SIGTERM`;
5. existența mesajelor `T5MSG`;
6. existența DB-ului;
7. `--verify` cu succes;
8. `--dump` cu `complete=0`.
