#!/bin/bash
# Join approval on the STAGE group only. e2e-D = creator, e2e-A = member, e2e-B / e2e-E = strangers.
cd ~/cg-e2e
G=$(cat stage/groupid.txt); ADDR=4ATaQv5X1JMzzRPb1tmweRfNAPBC9pK5Z6agmSSnRbMoWYAZPfRx
DB=stage/rt/carrierService1/chatrobot.db
d=floodguard/join; rm -rf $d; mkdir -p $d/outD $d/outA $d/outB $d/inD $d/inA
q() { python3 -c "import sqlite3,sys;c=sqlite3.connect('$DB');print(sys.argv[1], c.execute(sys.argv[2]).fetchall())" "$1" "$2"; }
stop_stage() { P=$(ps -eo pid,args | grep -E "port 5099|3399 /home/beagle/cg-e2e" | grep -v grep | awk '{print $1}'); [ -n "$P" ] && kill $P; sleep 3; }
start_stage() { (setsid nohup stage/start.sh >> stage/server-join.log 2>&1 < /dev/null &); sleep 25; }
: > stage/server-join.log
stop_stage; start_stage
echo "== phase 1: e2e-B asks to join (creator e2e-D listening)"
timeout 130 ./cg_e2e -d idD -g $G -t 120 -o $d/outD > $d/D1.log 2>&1 &
sleep 30
timeout 80 ./cg_e2e -d idB -n e2e-B -j $ADDR -t 70 > $d/B1.log 2>&1
wait
q members "select id,NickName from member_table"
q joins "select id,Friendid,Name,Notified from join_request_table"
grep -E "join (held|accepted|refused)" stage/server-join.log | sort | uniq -c
echo "== phase 2: restart, e2e-E asks to join"
stop_stage; start_stage
q joins_after_restart "select id,Name from join_request_table"
timeout 80 ./cg_e2e -d idE -n e2e-E -j $ADDR -t 70 > $d/E1.log 2>&1
q joins "select id,Name from join_request_table"
echo "== phase 3: non-creator A tries /approve 1; creator D: /pending, /approve 1, /reject 2, /pending; B waits to come in"
printf '/approve 1' > $d/inA/1.txt
printf '/pending' > $d/inD/1.txt; printf '/approve 1' > $d/inD/2.txt; printf '/reject 2' > $d/inD/3.txt; printf '/pending' > $d/inD/4.txt; printf '/help' > $d/inD/5.txt
timeout 200 ./cg_e2e -d idB -g $G -t 190 -o $d/outB > $d/B2.log 2>&1 &
timeout 100 floodguard/cg_burst -d idA -g $G -i 1000 -t 80 -o $d/outA -s $d/inA/1.txt > $d/A.log 2>&1
timeout 150 floodguard/cg_burst -d idD -g $G -i 4000 -t 120 -o $d/outD2 $(ls $d/inD/*.txt | sed "s/^/-s /") > $d/D2.log 2>&1
wait
q members "select id,NickName from member_table"
q joins "select id,Name from join_request_table"
grep -E "join |command refused" stage/server-join.log | grep -v "OnCarrier" | tail -20
for w in outD outA outD2; do echo "######## $w"; for f in $(grep -lE "Join|join|approve|creator|Approved|Rejected|Commands" $d/$w/* 2>/dev/null); do echo "== $f"; cat $f; echo; done; done
echo "######## B connected?"; grep -ciE "connected|online" $d/B2.log; tail -3 $d/B2.log
stop_stage
echo JOIN_TEST_DONE
