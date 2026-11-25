#!/bin/bash
cd ~/cg-e2e
G=$(cat stage/groupid.txt)
DB=stage/rt/carrierService1/chatrobot.db
d=floodguard/mode2; rm -rf $d; mkdir -p $d/inA $d/inD
q() { python3 -c "import sqlite3,sys;c=sqlite3.connect('$DB');print(sys.argv[1], c.execute(sys.argv[2]).fetchall())" "$1" "$2"; }
stop_stage() { P=$(ps -eo pid,args | grep -E "port 5099|3399 /home/beagle/cg-e2e" | grep -v grep | awk '{print $1}'); [ -n "$P" ] && kill $P; sleep 3; }
: > stage/server-mode2.log
stop_stage; (setsid nohup stage/start.sh >> stage/server-mode2.log 2>&1 < /dev/null &); sleep 25
q before "select Value from service_text where Key='join.mode'"
printf '/join open' > $d/inA/1.txt
timeout 150 floodguard/cg_burst -d idA -g $G -i 1000 -t 140 -o $d/outA -s $d/inA/1.txt > $d/A.log 2>&1
printf '/join' > $d/inD/1.txt; printf '/help' > $d/inD/2.txt; printf '/join open' > $d/inD/3.txt
timeout 260 ./cg_e2e -d idH -g $G -t 250 -o $d/outH > $d/H.log 2>&1 &
sleep 10
timeout 200 floodguard/cg_burst -d idD -g $G -i 5000 -t 190 -o $d/outD $(ls $d/inD/*.txt | sed "s/^/-s /") > $d/D.log 2>&1
wait
q members "select id,NickName from member_table order by id"
q joins "select id,Name from join_request_table where RejectedAt=0"
q mode "select Value from service_text where Key='join.mode'"
grep -E "join |command refused" stage/server-mode2.log | sed 's/.*CarrierRobot: //'
for f in $d/outD/* $d/outA/*; do grep -qE "join|Join|creator|Commands" $f && { echo "== $f"; cat $f; echo; }; done
stop_stage
echo MODE2_DONE
