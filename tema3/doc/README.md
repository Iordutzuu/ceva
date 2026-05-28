# FileOps & ProcOps Tools - Tema T1

## Structura proiectului

Acest proiect este organizat in urmatoarele directoare:

- bin/ - executabile
- src/ - fisiere sursa C
- include/ - fisiere header
- data/ - fisiere persistente
- logs/ - loguri
- reports/ - rapoarte generate de comenzile rulate
- tmp/ - fisiere temporare
- tests/ - scenarii de test
- doc/ - documentatie
- tools/ - scripturi

## Rularea auditului

Prima oara acord permisiuni de executare:

chmod +x tools/t1_audit.sh

Auditul se ruleaza cu comanda:

bash tools/t1_audit.sh

## Unde sunt salvate rapoartele

Rezultatele comenzilor sunt salvate in folderul `reports/`, in subfolderele:

- reports/fs/
- reports/process/
- reports/proc/
- reports/pipeline/

Fisierul "summary"" este:

reports/T1_summary.txt
