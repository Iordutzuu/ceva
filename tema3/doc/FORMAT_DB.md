# FORMAT_DB.md

## 1. Overview

Acest document descrie formatul binar utilizat pentru stocarea snapshot-urilor de procese în fișierul:

data/proc.db

Formatul este versionat și permite extinderea în viitor.

---

## 2. Structura generală a fișierului

Fișierul este compus din:

[HEADER][RECORD 1][RECORD 2]...[RECORD N]

---

## 3. Header (ProcDbHeader)

Header-ul apare o singură dată la începutul fișierului.

Câmp              Tip            Descriere
----------------------------------------------------------
magic            char[4]        Identificator format: "PRC1"
version          uint32_t       Versiunea formatului (1)
snapshot_id      uint64_t       Timestamp (time(NULL))
snapshot_state   uint32_t       1 = snapshot complet
active_writers   uint32_t       Număr instanțe active (simplificat: 1)
record_count     uint32_t       Număr total procese salvate

---

## 4. Structura unui record (ProcRecord)

Fiecare proces este salvat într-un record fix.

Câmp       Tip                     Descriere
----------------------------------------------------------
pid        int32_t                 ID proces
ppid       int32_t                 ID proces părinte
state      char                    Stare proces (R, S, Z, etc.)
comm       char[64]                Nume proces
cmdline    char[256]               Comandă completă
rss_kb     long                    Memorie RSS (KB)
cpu_time   unsigned long long      Timp CPU (utime + stime)

---

## 5. Dimensiuni fixe

MAX_COMM = 64
MAX_CMDLINE = 256

String-urile sunt:
- terminate cu '\0'
- trunchiate dacă depășesc dimensiunea

---

## 6. Surse de date din /proc

Câmp       Fișier sursă
----------------------------------------------
pid        nume director /proc/<pid>
ppid       /proc/<pid>/status (PPid)
state      /proc/<pid>/status (State)
comm       /proc/<pid>/comm
cmdline    /proc/<pid>/cmdline
rss_kb     /proc/<pid>/status (VmRSS)
cpu_time   /proc/<pid>/stat (utime + stime)

---

## 7. Tratarea erorilor

Procesele pot dispărea în timpul citirii.

Regulă:
- dacă nu se poate citi un câmp → procesul este ignorat
- programul NU se oprește

---

## 8. Concurență

Mai multe instanțe proc_snapshot pot scrie în același fișier.

Mecanism:
- lock exclusiv folosind fcntl
- scriere atomică:
  1. lock
  2. write header + records
  3. unlock

---

## 9. Exemple

După rulare:

./tools/fileops.sh run -- proc_snapshot

rezultă:

data/proc.db

cu:

magic = PRC1
version = 1
record_count ≈ număr procese sistem

---

## 10. Extensibilitate

Formatul poate fi extins prin:
- creșterea version
- adăugarea de câmpuri noi în record

Compatibilitatea se face pe baza câmpului magic și version.

---

## 11. Format pentru FileOps (index.db)

### Header (FileDbHeader)
Câmp              Tip            Descriere
----------------------------------------------------------
magic            char[4]        Identificator format: "IDX1"
version          uint32_t       Versiunea formatului (1)
snapshot_id      uint64_t       Timestamp (time(NULL))
snapshot_state   uint32_t       1 = OPEN, 2 = SEALED
active_writers   uint32_t       Număr instanțe active
record_count     uint32_t       Număr total fișiere salvate

### Structura unui record (FileRecord)
Câmp              Tip                     Descriere
----------------------------------------------------------
absolute_path    char[1024]              Calea absolută a fișierului
type             char[20]                Tipul (Fisier regulat, Director, Symlink, FIFO)
size             long long               Dimensiunea în bytes (0 ptr directoare/linkuri)
mtime            long                    Timpul ultimei modificări
hash             unsigned long           Hash simplu (doar ptr fișiere regulate)
dev              unsigned long           ID device
ino              unsigned long           Inode număr
symlink_target   char[1024]              Destinația dacă este symlink

### Limite alese
- `MAX_PATH_LEN` = 1024
- String-urile sunt terminate cu `\0` și trunchiate dacă depășesc spațiul.

---

## 12. Strategia de actualizare și Condiții de Validitate

O bază de date este considerată validă dacă îndeplinește minim următoarele condiții:
- Are un header complet și corect (câmpul `magic` corespunde tipului de bază de date: `IDX1` sau `PRC1`, iar `version` este `1`).
- Starea (`snapshot_state`) este `1` (OPEN - în curs de completare) sau `2` (SEALED - finalizat, nu mai acceptă contribuții).
- Nu conține înregistrări duplicate pentru aceeași cheie logică (calea absolută pentru fișiere, respectiv PID-ul pentru procese).

Strategia de actualizare (Sincronizare concurentă):
- Se folosesc blocaje exclusive (`fcntl` cu `F_WRLCK`) pe întregul fișier al bazei de date.
- Prima instanță care creează fișierul inițializează header-ul (stare OPEN, active_writers = 1, snapshot_id nou) și își scrie înregistrările.
- Instanțele ulterioare care găsesc baza OPEN reutilizează `snapshot_id`-ul, incrementează `active_writers` sub lock, fac append *doar* la înregistrările noi (verificând să nu existe deja) și actualizează `record_count`.
- Orice instanță care termină își decrementează `active_writers`. Instanța care aduce contorul la `0` marchează starea ca `SEALED`. Dacă o instanță nouă găsește baza `SEALED`, se oprește imediat cu o eroare clară.

---

## 13. Regulile de comparare folosite de db_diff

Utilitarul de diferențiere compară două snapshot-uri distincte (vechi și nou) și raportează diferențele în funcție de tipul bazei de date. Compararea este permisă doar dacă fișierele au același `magic` și `version`.

**Pentru fișiere (IDX1):**
Compararea se face după calea absolută (`absolute_path`).
- **[ADDED]:** Intrări prezente în baza nouă, dar care nu există în cea veche.
- **[DELETED]:** Intrări prezente în baza veche, dar care au dispărut în cea nouă.
- **[MODIFIED]:** Intrări prezente în ambele baze, dar la care diferă cel puțin unul dintre următoarele câmpuri:
  - tipul (`type`)
  - dimensiunea (`size`)
  - timpul modificării (`mtime`)
  - hash-ul/checksum-ul (`hash`)
  - ținta symlink-ului (`symlink_target`)

**Pentru procese (PRC1):**
Compararea se face după PID-ul procesului.
- **[NEW PROCESSES]:** Procese prezente în baza nouă, dar nu și în cea veche.
- **[TERMINATED PROCESSES]:** Procese prezente în baza veche, dar care au fost închise.
- **[MODIFIED PROCESSES]:** Procese prezente în ambele snapshot-uri, unde au fost detectate modificări semnificative:
  - Diferența de memorie RAM consumată (`rss_kb`) este mai mare decât pragul fix stabilit la 1024 KB.
  - S-a schimbat starea procesului (`state`).
  - S-a schimbat PID-ul procesului părinte (`ppid`).