#!/bin/bash

# Vars
let score=0
initial_directory=$PWD
homework_clone_dir="<unset>"

function init_eval {

    echo "[ INFO ] Initializez directorul temporar ..."

    homework_clone_dir=$(mktemp -p /dev/shm -d)
    rm -rf $homework_clone_dir
    mkdir -p $homework_clone_dir

    cp -r * $homework_clone_dir/

    cd $homework_clone_dir

    rm ./data/*
    rm ./bin/*
    rm -r ./tmp/*

    ./tools/fileops.sh init
    ./tools/fileops.sh clean
    ./tools/fileops.sh build

    if [ ! -f ./bin/fileops_manager ]; then
        echo "[ ERROR ] Nu exista ./bin/fileops_manager dupa ./tools/fileops.sh build"
        echo "          Continutul directorului ./bin/ este:"
        ls -lA ./bin/
        exit 4
    fi

    if [ ! -f ./bin/fileops_worker ]; then
        echo "[ ERROR ] Nu exista ./bin/fileops_worker dupa ./tools/fileops.sh build"
        echo "          Continutul directorului ./bin/ este:"
        ls -lA ./bin/
        exit 5
    fi

    echo "[ INFO ] Initializare completa!"
}

function git_statistics {
    
    echo "[ INFO ] Statistici git:"
    echo "            Ultimul commit: " $(git log -1 --format=%cd)
    echo "            Numarul de commit-uri (fara merges): " $(git rev-list --count --first-parent HEAD)
    echo "            Numarul de commit-uri (cu merges): " $(git rev-list --count HEAD)

}

function verificare_test_student {

    if [ ! -f ./tests/t4_inventory.sh ]; then
        echo "[ EROARE ] Nu exista fisierul ./tests/t4_inventory.sh"
        echo "           Directorul curent este $PWD"
        exit 3
    fi

    echo "[ INFO ] Verific testul scris de student - am inceput la" $(date +"%H:%M:%S")

    bash ./tests/t4_inventory.sh

    ls -lA ./reports/ 
    echo "[ INFO ] Am terminat rularea testului scris de student la" $(date +"%H:%M:%S")
    read -p "         Ce raport sa deschid? ./reports/" report_name
    cat ./reports/$report_name 

    read -p "Este totul ok? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi

}

function verificare_cli_1 {

    echo "[ INFO ] Verificare CLI 1 - 100 de foldere cu 10 fisiere fiecare"

    local temp_dir_path=$(mktemp -p /dev/shm -d)

    for i in {0..100}; do
        if [ $i -eq 0 ]; then
            for j in {1..10}; do
                file_path=$temp_dir_path/file$j.txt
                echo "Verificare CLI 1 - nivel $i" > $file_path
            done
        else
            subdir_path=$temp_dir_path/dir$i
            mkdir $subdir_path
            for j in {1..10}; do
                file_path=$subdir_path/file$j.txt
                echo "Verificare CLI 1 - nivel $i" > $file_path
            done
        fi
    done

    file_and_folder_count=$(find $temp_dir_path -mindepth 1 | wc -l)
    echo "[ INFO ] Am creat $file_and_folder_count foldere si fisiere in $temp_dir_path"

    rm data/ipc.mmap
    rm data/inventory.db

    echo "[ INFO ] Rulez ./tools/fileops.sh run -- fileops_manager --root $temp_dir_path --workers 4"
    echo "[ INFO ] Am inceput la" $(date +"%H:%M:%S")
    ./tools/fileops.sh run -- fileops_manager --root $temp_dir_path --workers 4
    echo "[ INFO ] Am terminat de rulat comanda la" $(date +"%H:%M:%S")

    read -p "Pot sterge $temp_dir_path ? (y/n)" answer
    if [ $answer = "y" ]; then
        rm -rf $temp_dir_path
        echo "Director sters"
    else
        echo "Nu am sters directorul"
    fi

    read -p "[ 1 PUNCT ] Punctaj acordat? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi
}

function verificare_cli_2 {

    echo "[ INFO ] Verificare CLI 2 - Multe foldere, fisiere si symlink-uri"

    local temp_dir_path=$(mktemp -p /dev/shm -d)

    for i in {0..1000}; do
        if [ $i -eq 0 ]; then
            for j in {1..100}; do
                file_path=$temp_dir_path/file$j.txt
                echo "Verificare CLI 2 - nivel $i" > $file_path
            done
        else
            subdir_path=$temp_dir_path/dir$i
            mkdir $subdir_path
            for j in {1..100}; do
                file_path=$subdir_path/file$j.txt
                echo "Verificare CLI 2 - nivel $i" > $file_path
            done

            for j in {1..25}; do
                subdir_2_path=$temp_dir_path/dir$i/subdir$j
                mkdir $subdir_2_path
                echo "Verificare CLI 2 - nivel $i cu subdir $j" > $subdir_2_path/single_file_$j.txt
            done 
        fi
    done

    for i in {1..50}; do
        ln -s /bin/bash $temp_dir_path/bash_symlink_$i
    done

    for i in {51..100}; do
        ln -s /bin/bash $temp_dir_path/dir$i/bash_symlink_$i
    done

    file_and_folder_count=$(find $temp_dir_path -mindepth 1 | wc -l)
    echo "[ INFO ] Am creat $file_and_folder_count foldere si fisiere in $temp_dir_path"

    rm data/my_ipc.mmap
    rm data/my_inventory.db

    echo "[ INFO ] Rulez ./tools/fileops.sh run -- fileops_manager --root $temp_dir_path --workers 8 --ipc data/my_ipc.mmap --db data/my_inventory.db"
    echo "[ INFO ] Am inceput la" $(date +"%H:%M:%S")
    ./tools/fileops.sh run -- fileops_manager --root $temp_dir_path --workers 8 --ipc data/my_ipc.mmap --db data/my_inventory.db
    echo "[ INFO ] Am terminat de rulat comanda la" $(date +"%H:%M:%S")

    read -p "Pot sterge $temp_dir_path ? (y/n)" answer
    if [ $answer = "y" ]; then
        rm -rf $temp_dir_path
        echo "Director sters"
    else
        echo "Nu am sters directorul"
    fi

    read -p "[ 1 PUNCT ] Punctaj acordat? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi
}

function verificare_cli_3 {

    echo "[ INFO ] Verificare CLI 3 - 100 de foldere cu 10 fisiere fiecare + VERIFY DATABASE + DUMP DATABASE"

    local temp_dir_path=$(mktemp -p /dev/shm -d)

    for i in {0..100}; do
        if [ $i -eq 0 ]; then
            for j in {1..10}; do
                file_path=$temp_dir_path/file$j.txt
                echo "Verificare CLI 3 - nivel $i" > $file_path
            done
        else
            subdir_path=$temp_dir_path/dir$i
            mkdir $subdir_path
            for j in {1..10}; do
                file_path=$subdir_path/file$j.txt
                echo "Verificare CLI 3 - nivel $i" > $file_path
            done
        fi
    done

    file_count=$(find $temp_dir_path -mindepth 1 -type f | wc -l)
    echo "[ INFO ] Am creat $file_count fisiere in $temp_dir_path"
    echo "         Folderele si symlink-urile nu sunt numarate"

    rm data/ipc.mmap
    rm data/inventory.db

    echo "[ INFO ] Rulez ./tools/fileops.sh run -- fileops_manager --root $temp_dir_path --workers 4" 
    echo "[ INFO ] Am inceput la" $(date +"%H:%M:%S")
    ./tools/fileops.sh run -- fileops_manager --root $temp_dir_path --workers 4
    echo "[ INFO ] Am terminat de rulat comanda la" $(date +"%H:%M:%S")

    read -p "Pot sterge $temp_dir_path ? (y/n)" answer
    if [ $answer = "y" ]; then
        rm -rf $temp_dir_path
        echo "Director sters"
    else
        echo "Nu am sters directorul"
    fi

    echo "[ INFO ] Acum verificam baza de date. Mai jos gasesti outputul:"
    ./tools/fileops.sh run -- fileops_manager --db data/inventory.db --verify

    echo "Codul de terminare al comenzii verify este $? (ar trebui sa fie 0)"

    read -p "[ 1 PUNCT ] Punctaj acordat? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi



    echo "[ INFO ] Acum dumpam baza de date"
    echo "         Uita-te dupa un numar de $file_count records"
    echo "         De asemenea, ar trebui sa vezi WORKER_COUNT cu valoarea 4"
    ./tools/fileops.sh run -- fileops_manager --db data/inventory.db --dump

    read -p "[ 1 PUNCT ] Punctaj acordat? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi
}

function verificare_cli_4 {

    echo "[ INFO ] Verificare CLI 4 - Multe foldere, fisiere si symlink-uri"

    local temp_dir_path=$(mktemp -p /dev/shm -d)

    for i in {0..1000}; do
        if [ $i -eq 0 ]; then
            for j in {1..100}; do
                file_path=$temp_dir_path/file$j.txt
                echo "Verificare CLI 4 - nivel $i" > $file_path
            done
        else
            subdir_path=$temp_dir_path/dir$i
            mkdir $subdir_path
            for j in {1..100}; do
                file_path=$subdir_path/file$j.txt
                echo "Verificare CLI 4 - nivel $i" > $file_path
            done

            for j in {1..25}; do
                subdir_2_path=$temp_dir_path/dir$i/subdir$j
                mkdir $subdir_2_path
                echo "Verificare CLI 4 - nivel $i cu subdir $j" > $subdir_2_path/single_file_$j.txt
            done 
        fi
    done

    for i in {1..50}; do
        ln -s /bin/bash $temp_dir_path/bash_symlink_$i
    done

    for i in {51..100}; do
        ln -s /bin/bash $temp_dir_path/dir$i/bash_symlink_$i
    done

    file_count=$(find $temp_dir_path -mindepth 1 -type f | wc -l)
    echo "[ INFO ] Am creat $file_count fisiere in $temp_dir_path"
    echo "         Folderele si symlink-urile nu sunt numarate"

    rm data/my_ipc.mmap
    rm data/my_inventory.db

    echo "[ INFO ] Rulez ./tools/fileops.sh run -- fileops_manager --root $temp_dir_path --workers 8 --ipc data/my_ipc.mmap --db data/my_inventory.db"
    echo "[ INFO ] Am inceput la" $(date +"%H:%M:%S")
    ./tools/fileops.sh run -- fileops_manager --root $temp_dir_path --workers 8 --ipc data/my_ipc.mmap --db data/my_inventory.db
    echo "[ INFO ] Am terminat de rulat comanda la" $(date +"%H:%M:%S")

    read -p "Pot sterge $temp_dir_path ? (y/n)" answer
    if [ $answer = "y" ]; then
        rm -rf $temp_dir_path
        echo "Director sters"
    else
        echo "Nu am sters directorul"
    fi

    echo "[ INFO ] Acum verificam baza de date. Mai jos gasesti outputul:"
    ./tools/fileops.sh run -- fileops_manager --db data/my_inventory.db --verify

    echo "Codul de terminare al comenzii verify este $? (ar trebui sa fie 0)"

    read -p "[ 1 PUNCT ] Punctaj acordat? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi



    echo "[ INFO ] Acum dumpam baza de date"
    echo "         Uita-te dupa un numar de $file_count records"
    echo "         De asemenea, ar trebui sa vezi WORKER_COUNT cu valoarea 8"
    ./tools/fileops.sh run -- fileops_manager --db data/my_inventory.db --dump

    read -p "[ 1 PUNCT ] Punctaj acordat? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi
}

function verificare_empty_database {

    # Aici creez un path de directoare in care am doar fisiere symlink si directoare
    # Database-ul ar trebui sa fie minuscul, de dimensiunea unui header, de maxim 4+4+4+4+4 bytes, hai poate 50 de bytes maxim.

    echo "[ INFO ] Verificare Empty Database - size-ul final ar trebui sa fie minuscul, neavand fisiere indexate."

    local temp_dir_path=$(mktemp -p /dev/shm -d)

    for i in {1..10}; do
        symlink_dir=$temp_dir_path/d$i/dA/dB/dC/dD/dE/dF/dG/dH
        mkdir -p $symlink_dir
        ln -s /bin/bash $symlink_dir/bash_symlink_$i
    done

    echo "[ INFO ] Am initializat calea de directoare $temp_dir_path"
    echo "         Exista " $(find $temp_dir_path -type f | wc -l) " fisiere normale"

    rm data/ipc.mmap
    rm data/inventory.db

    echo "[ INFO ] Rulez ./tools/fileops.sh run -- fileops_manager --root $temp_dir_path --workers 2"
    echo "[ INFO ] Am inceput la" $(date +"%H:%M:%S")
    ./tools/fileops.sh run -- fileops_manager --root $temp_dir_path --workers 2
    echo "[ INFO ] Comanda s-a terminat cu codul $?"
    echo "[ INFO ] Am terminat de rulat comanda la" $(date +"%H:%M:%S")

    read -p "Pot sterge $temp_dir_path ? (y/n)" answer
    if [ $answer = "y" ]; then
        rm -rf $temp_dir_path
        echo "Director sters"
    else
        echo "Nu am sters directorul"
    fi

    echo "[ INFO ] Acum dumpam baza de date"
    echo "         Uita-te dupa un numar de 0 records (ai avut doar symlinks si directoare la scanat)"
    echo "         Dimensiunea DB-ului nu ar trebui sa fie mai mare de 50 de bytes."
    echo "         " $(ls -lA data/inventory.db)
    echo "         De asemenea, ar trebui sa vezi WORKER_COUNT cu valoarea 2"
    ./tools/fileops.sh run -- fileops_manager --db data/inventory.db --dump

    read -p "[ 1 PUNCT ] Punctaj acordat? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi
}

function verificare_erori {

    # Aici verific ca se opreste cu un cod de return diferit de 0
    # sau o eroare explicita
    # pentru ca:
    # cazul 1: --root nu este dat ca parametru, dar nici --db
    # cazul 2: --root este dat ca parametru, dar este o cale inexistenta
    # cazul 3: --root este dat ca parametru, exista pe disc, dar este un fisier
    # cazul 4: --root este dat ca parametru, exista pe disc, dar este un folder fara drepturi de executie pentru utilizatorul curent (este /root)

    echo "[ INFO ] Verificare erori - 4 cazuri diferite pentru fileops_manager"

    echo "[ INFO ] Cazul 1 : comanda './tools/fileops.sh run -- fileops_manager --workers 2'"
    ./tools/fileops.sh run -- fileops_manager --workers 2
    echo "Comanda are codul de terminare $? (ar trebui sa fie diferit de 0 SAU sa ai o eroare clara - executia nu poate continua)"
    echo

        
    echo "[ INFO ] Cazul 2 : comanda './tools/fileops.sh run -- fileops_manager --workers 2 --root /abc/cale/care/nu/exista'"
    ./tools/fileops.sh run -- fileops_manager --workers 2 --root /abc/cale/care/nu/exista
    echo "Comanda are codul de terminare $? (ar trebui sa fie diferit de 0 SAU sa ai o eroare clara - executia nu poate continua)"
    echo

    touch /tmp/fisier-simplu
    echo "[ INFO ] Cazul 3 : comanda './tools/fileops.sh run -- fileops_manager --workers 2 --root /tmp/fisier-simplu'"
    ./tools/fileops.sh run -- fileops_manager --workers 2 --root /tmp/fisier-simplu
    echo "Comanda are codul de terminare $? (ar trebui sa fie diferit de 0 SAU sa ai o eroare clara - executia nu poate continua)"
    echo
    rm /tmp/fisier-simplu


    echo "[ INFO ] Cazul 4 : comanda './tools/fileops.sh run -- fileops_manager --workers 2 --root /root/'"
    ./tools/fileops.sh run -- fileops_manager --workers 2 --root /root/
    echo "Comanda are codul de terminare $? (ar trebui sa fie diferit de 0 SAU sa ai o eroare clara - executia nu poate continua)"
    echo

    read -p "[ 1 PUNCT ] Punctaj acordat? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi
}

function verificare_workers {
    
    echo "[ INFO ] Verificare workers folosind ps - este nevoie de argumentul --simulate-work-ms"

    echo "[ INFO ] Rulez comanda './tools/fileops.sh run -- fileops_manager --root $HOME --workers 2 --simulate-work-ms 100000 &' in BACKGROUND"
    echo "[ INFO ] Am inceput rularea la" $(date +"%H:%M:%S")

    ./tools/fileops.sh run -- fileops_manager --root $HOME --workers 2 --simulate-work-ms 100000 &
    fileops_manager_pid=$!

    echo "[ INFO ] Procesul fileops_manager are PID $fileops_manager_pid"
    echo "         Iar acesta este outputul de la ps a | grep fileops_worker"
    sleep 1
    ps a | grep fileops_worker | grep -v grep

    read -p "[ 1 PUNCT ] Punctaj acordat? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi

    echo "[ INFO ] Inchid procesul $fileops_manager_pid"
    kill $fileops_manager_pid

    echo "S-ar putea ca procesele fileops_worker sa mearga in continuare. Inchide-le manual."
}

function verificare_verify {

    echo "[ INFO ] Verificare argument --verify pe o baza de date invalida."
    echo "Rulez './tools/fileops.sh run -- fileops_manager --db /bin/bash --verify' executata la" $(date +"%H:%M:%S")

    ./tools/fileops.sh run -- fileops_manager --db /bin/bash --verify
    
    echo "Codul de terminare al comenzii este $? (ar trebui sa fie diferit de 0)."

    read -p "[ 1 PUNCT ] Punctaj acordat? (y/n)" answer
    if [ $answer = "y" ]; then
        let score+=1
    fi
}

echo "# # # #    Evaluare T4    # # # #"
echo "Asigura-te ca ma rulezi din directorul temei. Momentan, sunt in directorul $PWD"

read -p "Esti in directorul corect? (y/n)" answer
if [ $answer != "y" ]; then
    echo "Muta-te in directorul corect."
    exit 1
fi

init_eval

if [ $# -eq 1 ]; then
    "$1"
else
    verificare_test_student
    verificare_cli_1
    verificare_cli_2
    verificare_cli_3
    verificare_cli_4
    verificare_empty_database
    verificare_erori
    verificare_workers
    verificare_verify
fi

echo "[ INFO ] Scor total: $score / 11"

cd $initial_directory
rm -r $homework_clone_dir
git_statistics