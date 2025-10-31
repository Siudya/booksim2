#!/bin/bash

# Default traffic type is uniform if not provided
TRAFFIC=${1:-uniform}

echo "Running mesh topology with traffic: $TRAFFIC"

xmake bs2 -A red -t mesh -T $TRAFFIC -I 0.001 -S 0.001
xmake bs2 -A vda -t mesh -T $TRAFFIC -I 0.001 -S 0.001
xmake bs2 -A rc -t mesh -T $TRAFFIC -I 0.001 -S 0.001
xmake bs2 -A mvn -t mesh -T $TRAFFIC -I 0.001 -S 0.001