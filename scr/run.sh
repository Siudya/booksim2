#!/bin/bash
echo "red vda rc mvn vcs" | tr ' ' '\n' | parallel -j 5 ./scr/run_traffic.sh {}
python3 ./scr/log_to_csv.py
python3 ./scr/csv_to_plot.py