#!/bin/bash

mkdir release
cmake -B release -S . -G Ninja

mkdir debug
cmake -DCMAKE_BUILD_TYPE=Debug -B debug -S . -G Ninja