#!/bin/bash

let num=$1
echo $num

file="/home/tylor/PClog$(date +%R).txt"

echo "MEM (%), CPU (%)" > $file
n=0
while [ $n -lt $num ]; 
do
	MEM=$(free -m | awk 'NR==2{printf "%.2f, ", $3*100/$2}')
	CPU=$(top -bn1 | grep load | awk '{printf "%.2f", $(NF-2)}')
	echo "$MEM$CPU" >> $file
	sleep 1
	let n+=1
done
