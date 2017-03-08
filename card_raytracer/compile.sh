#!/bin/bash
clear
cd ..
svn update
cd card_raytracer
rm card_raytracer
g++ card_raytracer.cpp -s -g0 -O2 -pthread -ocard_raytracer -std=c++11
./card_raytracer
