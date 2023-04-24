#!/bin/bash

MAX=1000
tpc () {
    START=$SECONDS
    for ((i=0; i<$MAX; i++)); do
        $1 &> /dev/null
    done
    TOTAL=$((SECONDS - START))
    printf "average in seconds = "
    echo "scale=3 ; $TOTAL / $MAX" | bc
    TPC_TIME=$TOTAL
}

# Get time per connection
printf "Collecting wolfSSH time per connection (password auth) .... :"
CONNECT_COMMAND="./examples/client/client -u jill -P upthehill"
tpc "$CONNECT_COMMAND"

throughput() {
    START=$SECONDS
    for ((i=0; i<$MAX; i++)); do
        $1 &> /dev/null
    done
    TOTAL=$((SECONDS - START))
    TOTAL=$((TOTAL - TPC_TIME))
    SIZE=`wc -c configure.ac | awk '{print $1}'`
    SIZE=$((SIZE * MAX))
    printf "average MB/s = "
    echo "scale=3 ; $SIZE / 1000000 / $TOTAL" | bc
}

printf "Collecting wolfSSH SFTP put throughput .... "
CONNECT_COMMAND="./examples/sftpclient/wolfsftp -g -l configure.ac -r test-configure.ac -u jill -P upthehill"
throughput "$CONNECT_COMMAND"
rm test-configure.ac

printf "Collecting wolfSSH SFTP get throughput .... "
CONNECT_COMMAND="./examples/sftpclient/wolfsftp -G -l test-configure.ac -r configure.ac -u jill -P upthehill"
throughput "$CONNECT_COMMAND"
rm test-configure.ac

#printf "Collecting sshpass + ssh time per connection (password auth) .... :"
#SSH_COMMAND="sshpass -p upthehill -p 22222 jill@127.0.0.1"
#tpc "$SSH_COMMAND"

exit 0

