#!/bin/bash
cd ~/cg-e2e
G=$(cat stage/groupid.txt)
DB=stage/rt/carrierService1/chatrobot.db
d=floodguard/tgt3; rm -rf $d; mkdir -p $d/in1 $d/in2
q() { python3 -c "import sqlite3,sys;c=sqlite3.connect('$DB');print(sys.argv[1], c.execute(sys.argv[2]).fetchall())" "$1" "$2"; }
stop_stage() { P=$(ps -eo pid,args | grep -E "port 5099|3399 /home/beagle/cg-e2e" | grep -v grep | awk '{print $1}'); [ -n "$P" ] && kill $P; sleep 3; }
start_stage() { (setsid nohup stage/start.sh >> stage/server-tgt3.log 2>&1 < /dev/null &); sleep 25; }
cp ~/devs/agentnet-v2-group/build/service-e8/carrierService stage/appserver/carrierService
: > stage/server-tgt3.log
stop_stage; start_stage
printf "/help" > $d/in1/1.txt
timeout 120 floodguard/cg_burst -d idD -g $G -i 4000 -t 100 -o $d/out1 -s $d/in1/1.txt > $d/D1.log 2>&1
q blocked "select * from block_member_table"; q names "select * from block_name_table"
stop_stage; start_stage
printf '/unblock e2e-b' > $d/in2/1.txt; printf '/unblock 5VhKgvau' > $d/in2/2.txt; printf '/unblock e2e-b' > $d/in2/3.txt
timeout 150 floodguard/cg_burst -d idD -g $G -i 6000 -t 130 -o $d/out2 $(ls $d/in2/*.txt | sed "s/^/-s /") > $d/D2.log 2>&1
q blocked "select * from block_member_table"
q members "select id,NickName from member_table order by id"
grep -c "state=2" $d/D1.log $d/D2.log
grep -E "member |flood unblock" stage/server-tgt3.log | sed 's/.*CarrierRobot: //'
for f in $d/out1/* $d/out2/*; do echo "== $f"; cat $f; echo; done
stop_stage
echo TGT3_DONE
