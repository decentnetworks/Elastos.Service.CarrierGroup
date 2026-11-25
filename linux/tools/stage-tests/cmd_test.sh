#!/bin/bash
# feedback#22 stage check: e2e-A (non-creator) sends commands; e2e-C and e2e-D listen.
cd ~/cg-e2e
G=$(cat stage/groupid.txt)
d=floodguard/cmd22; rm -rf $d; mkdir -p $d/in $d/outA $d/outC $d/outD
printf '/friends list' > $d/in/1.txt
printf '/block 2' > $d/in/2.txt
printf '/help' > $d/in/3.txt
printf '/' > $d/in/4.txt
printf 'control chat from A after the commands, path /usr/bin ok' > $d/in/5.txt
before=$(python3 -c "import sqlite3;print(sqlite3.connect('stage/rt/carrierService1/chatrobot.db').execute('select count(*) from message_table').fetchone()[0])")
timeout 150 ./cg_e2e -d idC -g $G -t 140 -o $d/outC > $d/C.log 2>&1 &
timeout 150 ./cg_e2e -d idD -g $G -t 140 -o $d/outD > $d/D.log 2>&1 &
sleep 40
timeout 110 floodguard/cg_burst -d idA -g $G -i 2000 -t 100 -o $d/outA $(ls $d/in/*.txt | sed "s/^/-s /") > $d/A.log 2>&1
wait
after=$(python3 -c "import sqlite3;print(sqlite3.connect('stage/rt/carrierService1/chatrobot.db').execute('select count(*) from message_table').fetchone()[0])")
echo "message_table before=$before after=$after"
python3 -c "
import sqlite3;c=sqlite3.connect('stage/rt/carrierService1/chatrobot.db')
for r in c.execute('select * from message_table order by rowid desc limit 3'): print('row',r[:4])"
for w in A C D; do echo "######## received by $w"; for f in $d/out$w/*; do [ -f "$f" ] && { echo "== $f"; cat "$f"; echo; }; done; done
grep -E "command refused|flood" stage/server22.log | tail
echo CMD_TEST_DONE
