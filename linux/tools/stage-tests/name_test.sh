#!/bin/bash
cd ~/cg-e2e
G=$(cat stage/groupid.txt)
DB=stage/rt/carrierService1/chatrobot.db
q() { python3 -c "import sqlite3,sys;c=sqlite3.connect('$DB');print(sys.argv[1], c.execute(sys.argv[2]).fetchall())" "$1" "$2"; }
stop_stage() { P=$(ps -eo pid,args | grep -E "port 5099|3399 /home/beagle/cg-e2e" | grep -v grep | awk '{print $1}'); [ -n "$P" ] && kill $P; sleep 3; }
start_stage() { (setsid nohup stage/start.sh >> stage/server-name.log 2>&1 < /dev/null &); sleep 25; }
cp ~/devs/agentnet-v2-group/build/service-e8/carrierService stage/appserver/carrierService
: > stage/server-name.log
stop_stage; start_stage
q start "select id,NickName from member_table where id=2"
echo "== e2e-C renames itself"
timeout 80 ./cg_e2e -d idC -n "e2e-C's box" -g $G -t 70 -o /tmp/nt_out > /dev/null 2>&1
q after_rename "select id,NickName from member_table where id=2"
echo "== restart (SDK cache from carrier.data)"
stop_stage; start_stage
q after_restart "select id,NickName from member_table where id=2"
grep -E "member name" stage/server-name.log | sed 's/.*CarrierRobot: //'
echo "== e2e-C renames back"
timeout 80 ./cg_e2e -d idC -n "e2e-C" -g $G -t 70 -o /tmp/nt_out > /dev/null 2>&1
q final "select id,NickName from member_table where id=2"
grep -E "member name|updateMemberInfo error" stage/server-name.log | sed 's/.*CarrierRobot: //' | tail -6
stop_stage; rm -rf /tmp/nt_out
echo NAME_DONE
