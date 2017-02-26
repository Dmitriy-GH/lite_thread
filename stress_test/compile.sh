#!/bin/bash
clear
cd ..
svn update
cd stress_test
g++ stress_test.cpp -s -g0 -O2 -pthread -o[Dstress_test -std=c++11
./stress_test
