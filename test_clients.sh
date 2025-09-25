#!/bin/bash
# save as test_clients.sh

for i in {1..50}; do
    echo "Starting client $i"
    timeout 30 ./client 127.0.0.1 &
    sleep 0.1  # Small delay between connections
done

wait
echo "All clients finished"
