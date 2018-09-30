#!/usr/bin/env bash
rm -rf curlpp
git clone https://github.com/jpbarrette/curlpp
mkdir curlpp/build && cd curlpp/build || exit
cmake .. -DCMAKE_BUILD_TYPE=Release
make curlpp
