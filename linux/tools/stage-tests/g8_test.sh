#!/bin/bash
cd ~/cg-e2e
ADDR=dxXdbQXJnB3phXEsVWKFGeQ3uBbKN3K1mXFEniPSL5nX3dhw3Mfk
G=$(python3 - <<PY
A='123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
def dec(s):
    n=0
    for ch in s: n=n*58+A.index(ch)
    b=n.to_bytes((n.bit_length()+7)//8,'big'); return b
def enc(b):
    n=int.from_bytes(b,'big'); s=''
    while n: n,r=divmod(n,58); s=A[r]+s
    return s
print(enc(dec("$ADDR")[:32]))
PY
)
echo group8 userid $G
DB=~/devs/Elastos.Service.CarrierGroup/linux/ui/runtime_data/carrierService8/chatrobot.db
q() { python3 -c "import sqlite3,sys;c=sqlite3.connect('file:$DB?mode=ro',uri=True);print(sys.argv[1], c.execute(sys.argv[2]).fetchall())" "$1" "$2"; }
d=floodguard/g8; rm -rf $d; mkdir -p $d/outB $d/inB $d/outF
T=$(date "+%F %T")
timeout 130 ./cg_e2e -d idB -g $G -t 120 -o $d/outB > $d/B1.log 2>&1 &
sleep 45
timeout 80 ./cg_e2e -d idF -n e2e-F -j $ADDR -t 70 > $d/F1.log 2>&1
wait
q joins "select id,substr(Friendid,1,8),Name,Notified from join_request_table"
N=$(python3 -c "import sqlite3;c=sqlite3.connect('file:$DB?mode=ro',uri=True);r=c.execute(\"select id from join_request_table where Friendid='Cx2K7vHXwPTZnSshJLg8fev5Jxa5G9ovcpNPuWAxicbU' and RejectedAt=0\").fetchone();print(r[0] if r else 0)")
echo request number $N
printf "/approve $N" > $d/inB/1.txt; printf '/pending' > $d/inB/2.txt
timeout 200 ./cg_e2e -d idF -g $G -t 190 -o $d/outF > $d/F2.log 2>&1 &
timeout 120 floodguard/cg_burst -d idB -g $G -i 4000 -t 90 -o $d/outB2 $(ls $d/inB/*.txt | sed "s/^/-s /") > $d/B2.log 2>&1
wait
q members "select id,substr(Friendid,1,8),NickName from member_table order by id"
q joins "select id,Name,RejectedAt from join_request_table"
journalctl --user -u carriergroup.service --since "$T" --no-pager | grep -E "join |command refused" | sed 's/.*\]: //'
for f in $(grep -lE "Join|join|Approved" $d/outB/* $d/outB2/* 2>/dev/null); do echo "== $f"; cat $f; echo; done
echo G8_DONE
