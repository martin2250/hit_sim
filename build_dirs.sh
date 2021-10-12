#!/bin/bash

mkdir release
cmake -B release -S .

mkdir debug
cmake -DCMAKE_BUILD_TYPE=Debug -B debug -S .