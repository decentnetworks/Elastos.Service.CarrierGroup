#!/bin/bash
cd ~/cg-e2e
G=$(cat stage/groupid.txt); ADDR=4ATaQv5X1JMzzRPb1tmweRfNAPBC9pK5Z6agmSSnRbMoWYAZPfRx
DB=stage/rt/carrierService1/chatrobot.db
d=floodguard/join2; rm -rf $d; mkdir -p $d/outD $d/outE $d/inD
q() { python3 -c "import sqlite3,sys;c=sqlite3.connect('$DB');print(sys.argv[1], c.execute(sys.argv[2]).fetchall())" "$1" "$2"; }
stop_stage() { P=$(ps -eo pid,args | grep -E "port 5099|3399 /home/beagle/cg-e2e" | grep -v grep | awk '{print $1}'); [ -n "$P" ] && kill $P; sleep 3; }
start_stage() { (setsid nohup stage/start.sh >> stage/server-join.log 2>&1 < /dev/null &); sleep 25; }
: > stage/server-join.log
stop_stage; start_stage
echo "== approved e2e-E comes online; stranger e2e-F asks to join"
timeout 100 ./cg_e2e -d idE -g $G -t 90 -o $d/outE > $d/E.log 2>&1 &
sleep 5
timeout 80 ./cg_e2e -d idF -n e2e-F -j $ADDR -t 70 > $d/F1.log 2>&1
wait
q members "select id,NickName from member_table"
q joins "select id,Name,Notified,RejectedAt from join_request_table"
echo "== restart; creator: /pending, /reject 2"
stop_stage; start_stage
q joins_after_restart "select id,Name,RejectedAt from join_request_table"
printf '/pending' > $d/inD/1.txt; printf '/reject 2' > $d/inD/2.txt; printf '/pending' > $d/inD/3.txt
timeout 120 floodguard/cg_burst -d idD -g $G -i 4000 -t 90 -o $d/outD $(ls $d/inD/*.txt | sed "s/^/-s /") > $d/D.log 2>&1
q joins "select id,Name,RejectedAt>0 from join_request_table"
echo "== e2e-F asks again (should be ignored)"
timeout 80 ./cg_e2e -d idF -n e2e-F -j $ADDR -t 70 > $d/F2.log 2>&1
q joins "select id,Name,RejectedAt>0 from join_request_table"
grep -E "join |command refused" stage/server-join.log | sort | uniq -c
for f in $d/outD/*; do echo "== $f"; cat $f; echo; done
stop_stage
echo JOIN_TEST2_DONE
