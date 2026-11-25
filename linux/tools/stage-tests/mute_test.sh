#!/bin/bash
# Three short floods from e2e-C, 11 min apart, into the STAGE group only -> expect a mute on the third.
cd ~/cg-e2e
G=$(cat stage/groupid.txt)
W=(alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima mike november oscar)
for round in 1 2 3; do
  d=floodguard/mute$round; rm -rf $d; mkdir -p $d/in $d/out
  for i in 0 1 2 3 4; do printf "flood round $round %s %s distinct words here" ${W[$((round*5+i-5))]} ${W[$i]} > $d/in/$i.txt; done
  timeout 90 floodguard/cg_burst -d idC -g $G -i 200 -t 90 -o $d/out $(ls $d/in/*.txt | sed "s/^/-s /") > $d/send.log 2>&1
  echo "round $round done $(date +%T)"
  [ $round -lt 3 ] && sleep 660
done
grep "flood mute" stage/server11.log
python3 -c "
import sqlite3; c=sqlite3.connect('stage/rt/carrierService1/chatrobot.db')
print('mute rows', c.execute('select Friendid,MutedAt,MutedUntil from flood_mute_table').fetchall())"
for f in floodguard/mute3/out/*; do echo "== $f"; cat "$f"; echo; done
echo MUTE_TEST_DONE
