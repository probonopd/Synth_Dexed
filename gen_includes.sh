#!/bin/bash

echo "#include <Arduino.h>" > synth_dexed.h
echo "#include \"config.h\"" >> synth_dexed.h
for i in `cat includes.txt`
do
	cat $i >> synth_dexed.h
done

echo "#include \"synth_dexed.h\"" > synth_dexed.cpp
for i in `ls orig_code/*.cpp`
do
	cat $i >> synth_dexed.cpp
done
