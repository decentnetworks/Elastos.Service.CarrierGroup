#!/bin/bash
cd ~/cg-e2e
G=$(cat stage/groupid.txt)
DB=stage/rt/carrierService1/chatrobot.db
d=floodguard/tgt2; rm -rf $d; mkdir -p $d/inD
q() { python3 -c "import sqlite3,sys;c=sqlite3.connect('$DB');print(sys.argv[1], c.execute(sys.argv[2]).fetchall())" "$1" "$2"; }
stop_stage() { P=$(ps -eo pid,args | grep -E "port 5099|3399 /home/beagle/cg-e2e" | grep -v grep | awk '{print $1}'); [ -n "$P" ] && kill $P; sleep 3; }
: > stage/server-tgt2.log
stop_stage; (setsid nohup stage/start.sh >> stage/server-tgt2.log 2>&1 < /dev/null &); sleep 25
i=1; for c in "/unblock E2E-H" "/del e2e-D" "/del nobody" "/del zz-test-g10.beagles.eth" "/del 5"; do printf '%s' "$c" > $d/inD/$(printf %02d $i).txt; i=$((i+1)); done
timeout 200 floodguard/cg_burst -d idD -g $G -i 6000 -t 190 -o $d/outD $(ls $d/inD/*.txt | sed "s/^/-s /") > $d/D.log 2>&1
q after "select id,substr(Friendid,1,8),NickName from member_table order by id"
q blocked "select * from block_member_table"
grep -E "member |flood unblock|target|OnCarrierFriendMessage from: EHNoc" stage/server-tgt2.log | sed 's/.*: I\///'
grep -c "state=2" $d/D.log
for f in $d/outD/*; do echo "== $f"; cat $f; echo; done
stop_stage
echo TGT2_DONE
