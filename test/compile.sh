#!/bin/bash
clear
rm test
cd ..
svn update
cd test
g++ test.cpp -s -g0 -O2 -pthread -otest -std=c++11
./test