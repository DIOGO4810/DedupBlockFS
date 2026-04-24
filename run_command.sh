#!/bin/bash
echo "PID = $$"
echo "Press any key to continue"
read -n 1 -s key
cd benchmarks
python3 dedupe_workload.py
cd ..

