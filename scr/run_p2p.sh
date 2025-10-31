#!/bin/bash

# Default traffic type is uniform if not provided
TRAFFIC=${1:-uniform}

echo "Running p2p topology with traffic: $TRAFFIC"

xmake bs2 -A red -t p2p -T $TRAFFIC -I 0.001 -S 0.001
xmake bs2 -A vda -t p2p -T $TRAFFIC -I 0.001 -S 0.001
xmake bs2 -A rc -t p2p -T $TRAFFIC -I 0.001 -S 0.001
xmake bs2 -A mvn -t p2p -T $TRAFFIC -I 0.001 -S 0.001