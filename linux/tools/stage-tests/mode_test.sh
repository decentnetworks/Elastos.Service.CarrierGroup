#!/bin/bash
cd ~/cg-e2e
G=$(cat stage/groupid.txt); ADDR=4ATaQv5X1JMzzRPb1tmweRfNAPBC9pK5Z6agmSSnRbMoWYAZPfRx
DB=stage/rt/carrierService1/chatrobot.db
d=floodguard/mode; rm -rf $d; mkdir -p $d/in1 $d/in2
q() { python3 -c "import sqlite3,sys;c=sqlite3.connect('$DB');print(sys.argv[1], c.execute(sys.argv[2]).fetchall())" "$1" "$2"; }
stop_stage() { P=$(ps -eo pid,args | grep -E "port 5099|3399 /home/beagle/cg-e2e" | grep -v grep | awk '{print $1}'); [ -n "$P" ] && kill $P; sleep 3; }
: > stage/server-mode.log
stop_stage; (setsid nohup stage/start.sh >> stage/server-mode.log 2>&1 < /dev/null &); sleep 25
timeout 20 ./cg_e2e -d idG -a > /dev/null 2>&1; timeout 20 ./cg_e2e -d idH -a > /dev/null 2>&1
echo "== default (open): e2e-G asks"
q mode "select Value from service_text where Key='join.mode'"
timeout 80 ./cg_e2e -d idG -n e2e-G -j $ADDR -t 70 > $d/G.log 2>&1
echo "== creator: /join, /join approve, /join"
printf '/join' > $d/in1/1.txt; printf '/join approve' > $d/in1/2.txt; printf '/join' > $d/in1/3.txt; printf '/help' > $d/in1/4.txt
timeout 120 floodguard/cg_burst -d idD -g $G -i 3000 -t 80 -o $d/out1 $(ls $d/in1/*.txt | sed "s/^/-s /") > $d/D1.log 2>&1
echo "== approve mode: e2e-H asks"
timeout 80 ./cg_e2e -d idH -n e2e-H -j $ADDR -t 70 > $d/H.log 2>&1
q joins "select id,Name from join_request_table where RejectedAt=0"
echo "== non-creator A: /join open; creator: /join open; e2e-H waits"
timeout 100 floodguard/cg_burst -d idA -g $G -i 1000 -t 70 -o $d/outA -s <(printf '/join open') > $d/A.log 2>&1
printf '/join open' > $d/in2/1.txt
timeout 200 ./cg_e2e -d idH -g $G -t 150 -o $d/outH > $d/H2.log 2>&1 &
timeout 120 floodguard/cg_burst -d idD -g $G -i 3000 -t 80 -o $d/out2 -s $d/in2/1.txt > $d/D2.log 2>&1
wait
q members "select id,NickName from member_table order by id"
q joins "select id,Name from join_request_table where RejectedAt=0"
q mode "select Value from service_text where Key='join.mode'"
grep -E "join |command refused" stage/server-mode.log | sed 's/.*CarrierRobot: //'
for f in $d/out1/* $d/out2/* $d/outA/*; do grep -qE "join|Join|creator|Commands" $f && { echo "== $f"; cat $f; echo; }; done
stop_stage
echo MODE_DONE
