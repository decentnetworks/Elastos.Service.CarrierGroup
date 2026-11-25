#!/bin/bash
cd ~/cg-e2e
G=$(cat stage/groupid.txt)
DB=stage/rt/carrierService1/chatrobot.db
d=floodguard/tgt; rm -rf $d; mkdir -p $d/inD $d/inA
q() { python3 -c "import sqlite3,sys;c=sqlite3.connect('$DB');print(sys.argv[1], c.execute(sys.argv[2]).fetchall())" "$1" "$2"; }
stop_stage() { P=$(ps -eo pid,args | grep -E "port 5099|3399 /home/beagle/cg-e2e" | grep -v grep | awk '{print $1}'); [ -n "$P" ] && kill $P; sleep 3; }
cp ~/devs/agentnet-v2-group/build/service-e8/carrierService stage/appserver/carrierService
: > stage/server-tgt.log
stop_stage; (setsid nohup stage/start.sh >> stage/server-tgt.log 2>&1 < /dev/null &); sleep 25
q before "select id,substr(Friendid,1,8),NickName from member_table order by id"
printf '/del 1' > $d/inA/1.txt
timeout 120 floodguard/cg_burst -d idA -g $G -i 1000 -t 110 -o $d/outA -s $d/inA/1.txt > $d/A.log 2>&1
i=1; for c in "/del e2e-G" "/block 5VhKgvau" "/unblock E2E-H" "/del e2e-D" "/del nobody" "/del zz-test-g10.beagles.eth" "/del 5" "/help"; do printf '%s' "$c" > $d/inD/$(printf %02d $i).txt; i=$((i+1)); done
timeout 230 floodguard/cg_burst -d idD -g $G -i 6000 -t 220 -o $d/outD $(ls $d/inD/*.txt | sed "s/^/-s /") > $d/D.log 2>&1
q after "select id,substr(Friendid,1,8),NickName from member_table order by id"
q blocked "select * from block_member_table"
grep -E "member |flood unblock|command refused|target" stage/server-tgt.log | sed 's/.*CarrierRobot: //'
for f in $d/outA/* $d/outD/*; do echo "== $f"; cat $f; echo; done
stop_stage
echo TGT_DONE
