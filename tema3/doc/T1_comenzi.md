# T1 - Comenzi Linux
# Bordianu Stefan

## A) Filesystem

### A1. Listeaza continutul directorului curent in format long si include fisierele ascunse
1. Descriere:
Afiseaza toate fisierele si directoarele din directorul curent, inclusiv cele ascunse, in format detaliat.

2. Comanda:
```bash
ls -la > reports/fs/A1_ls_long.txt
```

3. Fisier output:
`reports/fs/A1_ls_long.txt`

4. Explicatie:
Optiunea `-l` afiseaza informatii detaliate, iar `-a` include fisierele ascunse.

---

### A2. Gaseste in proiect toate fisierele cu extensia .sh si afiseaza calea lor relativa
1. Descriere:
Cauta toate fisierele shell script din proiect.

2. Comanda:
```bash
find . -name "*.sh" > reports/fs/A2_find_sh.txt
```

3. Fisier output:
`reports/fs/A2_find_sh.txt`

4. Explicatie:
Comanda cauta recursiv incepand din directorul curent toate fisierele care se termina in `.sh`.

---

### A3. Afiseaza dimensiunea in mod human-readable pentru directoarele de nivel 1 din proiect
1. Descriere:
Afiseaza dimensiunea directoarelor principale din proiect intr-un format usor de citit.

2. Comanda:
```bash
du -h --max-depth=1 > reports/fs/A3_du_level1.txt
```

3. Fisier output:
`reports/fs/A3_du_level1.txt`

4. Explicatie:
Optiunea `-h` afiseaza dimensiunile in KB, MB etc., iar `--max-depth=1` limiteaza afisarea la directoarele de nivel 1.

---

## B) Procese

### B1. Afiseaza primele 10 procese ordonate descrescator dupa consumul de memorie
1. Descriere:
Afiseaza procesele care folosesc cea mai multa memorie RAM.

2. Comanda:
```bash
ps aux --sort=-%mem | head -10 > reports/process/B1_top_mem.txt
```

3. Fisier output:
`reports/process/B1_top_mem.txt`

4. Explicatie:
`ps aux` listeaza procesele, `--sort=-%mem` le sorteaza descrescator dupa memorie, iar `head -10` pastreaza primele 10 linii.

---

### B2. Afiseaza arborele de procese pentru sistem, cu PID-urile vizibile
1. Descriere:
Afiseaza structura proceselor sub forma de arbore.

2. Comanda:
```bash
pstree -p > reports/process/B2_pstree.txt
```

3. Fisier output:
`reports/process/B2_pstree.txt`

4. Explicatie:
Comanda `pstree` afiseaza relatia parinte-copil dintre procese, iar `-p` afiseaza PID-urile.

---

### B3. Porneste un proces de test (`sleep 60` in background) si demonstreaza ca il poti identifica dupa nume si PID
1. Descriere:
Porneste un proces simplu in background si apoi il identifica dupa nume.

2. Comenzi:
```bash
sleep 60 &
pgrep sleep > reports/process/B3_pgrep_sleep.txt
```

3. Fisier output:
`reports/process/B3_pgrep_sleep.txt`

4. Explicatie:
`sleep 60 &` porneste procesul in fundal pentru 60 de secunde. `pgrep sleep` afiseaza PID-ul procesului cu numele `sleep`.

Observatie:
Procesul poate fi oprit ulterior cu:
```bash
kill PID
```

---

## C) /proc

### C1. Extrage modelul CPU din /proc/cpuinfo
1. Descriere:
Extrage informatia despre modelul procesorului.

2. Comanda:
```bash
grep "model name" /proc/cpuinfo > reports/proc/C1_cpu_model.txt
```

3. Fisier output:
`reports/proc/C1_cpu_model.txt`

4. Explicatie:
`grep` cauta liniile care contin textul `model name` in fisierul `/proc/cpuinfo`.

---

### C2. Extrage `MemTotal` si `MemAvailable` din /proc/meminfo
1. Descriere:
Extrage memoria totala si memoria disponibila din sistem.

2. Comanda:
```bash
grep -E "MemTotal|MemAvailable" /proc/meminfo > reports/proc/C2_mem_total_avail.txt
```

3. Fisier output:
`reports/proc/C2_mem_total_avail.txt`

4. Explicatie:
`grep -E` permite cautarea mai multor modele. Aici sunt cautate liniile `MemTotal` si `MemAvailable`.

---

### C3. Afiseaza uptime din /proc/uptime
1. Descriere:
Afiseaza timpul de functionare al sistemului.

2. Comanda:
```bash
cat /proc/uptime > reports/proc/C3_uptime.txt
```

3. Fisier output:
`reports/proc/C3_uptime.txt`

4. Explicatie:
Fisierul `/proc/uptime` contine timpul de functionare al sistemului in secunde.

---

## D) Pipeline

### D1. Construieste un top 5 al celor mai mari fisiere din proiect, afisand doar dimensiunea si calea
1. Descriere:
Gaseste fisierele din proiect, le sorteaza dupa dimensiune si afiseaza primele 5.

2. Comanda:
```bash
find . -type f -exec du -h {} + | sort -rh | head -5 > reports/pipeline/D1_top5_large_files.txt
```

3. Fisier output:
`reports/pipeline/D1_top5_large_files.txt`

4. Explicatie:
`find` gaseste fisierele, `du -h` afiseaza dimensiunea, `sort -rh` sorteaza descrescator, iar `head -5` pastreaza primele 5 rezultate.

---

### D2. Construieste un top 5 procese dupa memorie
1. Descriere:
Sorteaza procesele dupa memorie si pastreaza primele 5 linii.

2. Comanda:
```bash
ps aux | sort -nrk4 | head -5 > reports/pipeline/D2_top5_proc_mem.txt
```

3. Fisier output:
`reports/pipeline/D2_top5_proc_mem.txt`

4. Explicatie:
`ps aux` listeaza procesele, `sort -nrk4` sorteaza numeric descrescator dupa coloana de memorie, iar `head -5` pastreaza primele 5 procese.

---

### D3. Din fisierul doc/T1_comenzi.md, extrage liniile care contin comenzi, sorteaza-le si numara cate sunt
1. Descriere:
Elimina liniile de titlu si liniile goale din documentatie, apoi sorteaza si numara liniile ramase.

2. Comanda:
```bash
grep -v "^#" doc/T1_comenzi.md | grep -v "^$" | sort | wc -l > reports/pipeline/D3_count_commands.txt
```

3. Fisier output:
`reports/pipeline/D3_count_commands.txt`

4. Explicatie:
Primul `grep -v` elimina titlurile, al doilea elimina liniile goale, `sort` sorteaza rezultatele, iar `wc -l` numara liniile.
