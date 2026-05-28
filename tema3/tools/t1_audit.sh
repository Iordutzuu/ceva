#!/bin/bash

start=$(date +%s)

mkdir -p reports/fs reports/process reports/proc reports/pipeline

ls -la > reports/fs/A1_ls_long.txt
find . -name "*.sh" > reports/fs/A2_find_sh.txt
du -h --max-depth=1 > reports/fs/A3_du_level1.txt

ps aux --sort=-%mem | head -10 > reports/process/B1_top_mem.txt
pstree -p > reports/process/B2_pstree.txt

sleep 60 &
pgrep sleep > reports/process/B3_pgrep_sleep.txt

grep "model name" /proc/cpuinfo > reports/proc/C1_cpu_model.txt
grep -E "MemTotal|MemAvailable" /proc/meminfo > reports/proc/C2_mem_total_avail.txt
cat /proc/uptime > reports/proc/C3_uptime.txt

find . -type f -exec du -h {} + | sort -rh | head -5 > reports/pipeline/D1_top5_large_files.txt
ps aux | sort -nrk4 | head -5 > reports/pipeline/D2_top5_proc_mem.txt
grep -v "^#" doc/T1_comenzi.md | grep -v "^$" | sort | wc -l > reports/pipeline/D3_count_commands.txt

end=$(date +%s)
duration=$((end - start))

{
    echo "Start: $start"
    echo "End: $end"
    echo "Duration: $duration sec"
    echo "Created paths:"
    echo "reports/fs"
    echo "reports/process"
    echo "reports/proc"
    echo "reports/pipeline"
} > reports/T1_summary.txt
