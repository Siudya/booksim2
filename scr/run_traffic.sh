#!/bin/bash
RF=${1:-red}

xmake bs2 -A $RF -I 0.005 -S 0.001 -T uniform -t mesh
xmake bs2 -A $RF -I 0.017 -S 0.001 -T uniform -t twin
xmake bs2 -A $RF -I 0.017 -S 0.001 -T uniform -t p2p 

xmake bs2 -A $RF -I 0.005 -S 0.0005 -T bitcomp -t mesh
xmake bs2 -A $RF -I 0.011 -S 0.0005 -T bitcomp -t twin
xmake bs2 -A $RF -I 0.005 -S 0.0005 -T bitcomp -t p2p 

xmake bs2 -A $RF -I 0.010 -S 0.0005 -T shuffle -t mesh
xmake bs2 -A $RF -I 0.015 -S 0.0005 -T shuffle -t twin
xmake bs2 -A $RF -I 0.012 -S 0.0005 -T shuffle -t p2p 