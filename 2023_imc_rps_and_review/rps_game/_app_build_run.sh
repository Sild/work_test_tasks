#!/bin/bash
clang++ -std=c++17 main.cpp game.cpp -o rps_game
./rps_game
# use ./rps_game 3 to specify rounds count
