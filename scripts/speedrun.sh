#!/bin/bash

cmake -S . -B build -DHADES_ENABLE_API=ON

cmake --build build

./build/Hades
