
#!/bin/bash

num="$1"

INET_NTOA() {
    local IFS=. num quad ip e
    num=$1
    for e in 3 2 1
    do
        (( quad = 256 ** e))
        (( ip[3-e] = num / quad ))
        (( num = num % quad ))
    done
    ip[3]=$num
    address="${ip[*]}"
}

INET_ATON ()
{
    local IFS=. ip num e
    ip=($1)
    for e in 3 2 1
    do
        (( num += ip[3-e] * 256 ** e ))
    done
    (( num += ip[3] ))
    subnet=$num
}

if [ "$num" = "" ];
then
        echo You must enter the number of aliases to create.
else
        n=1
        let n_end=$num+1
        while [ $n -lt $n_end ];
        do
		INET_ATON 192.168.1.154
                INET_NTOA $subnet+$n
		ifconfig wlp8s0:$n $address/24 up
                let n+=1
        done
fi
