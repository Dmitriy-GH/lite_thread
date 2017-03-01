#!/bin/bash
clear
cd ..
svn update
cd stress_test
rm stress_test
g++ stress_test.cpp -s -g0 -O2 -pthread -ostress_test -std=c++14
./stress_test
