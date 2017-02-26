#!/bin/bash
clear
cd ..
rm stress_test
svn update
g++ stress_test.cpp -s -g0 -O2 -pthread -orm stress_test -std=c++11
./stress_test
