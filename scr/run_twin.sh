#!/bin/bash

# Default traffic type is uniform if not provided
TRAFFIC=${1:-uniform}

echo "Running twin topology with traffic: $TRAFFIC"

xmake bs2 -A red -t twin -T $TRAFFIC -I 0.001 -S 0.001
xmake bs2 -A vda -t twin -T $TRAFFIC -I 0.001 -S 0.001
xmake bs2 -A rc -t twin -T $TRAFFIC -I 0.001 -S 0.001
xmake bs2 -A mvn -t twin -T $TRAFFIC -I 0.001 -S 0.001