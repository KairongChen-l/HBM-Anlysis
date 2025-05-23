#!/bin/bash
while true; do
  clear
  echo "===== NUMA Node 0 Memory Usage ====="
  grep -E "MemTotal|MemFree|MemUsed" /sys/devices/system/node/node0/meminfo
  echo "===== NUMA Node 1 Memory Usage ====="
  grep -E "MemTotal|MemFree|MemUsed" /sys/devices/system/node/node1/meminfo
  sleep 1
done
