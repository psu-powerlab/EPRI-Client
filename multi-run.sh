#!/bin/bash

num="$1"

if [ "$num" = "" ];
then
	echo You must enter the number of clients
else
	let t=$1*2
	./resource_log.sh  $t &
	n=1
   	let n_end=$num+1
	while [ $n -lt $n_end ];
	do
		echo wlp8s0:$n
		tmux new-window -d ./build/client_test wlp8s0:$n http://192.168.1.6/dcap
		sleep 1
		let n+=1
	done
fi
