#!/bin/bash
clear
cd test
./compile.sh
cd ../card_raytracer
./compile.sh
cd ../stress_test
./compile.sh
cd ..