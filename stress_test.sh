#!/bin/bash
# save as stress_test.sh

START_TIME=$(date +%s)
CRASH_COUNT=0
TEST_DURATION=3600  # 1 hour test

while [ $(($(date +%s) - START_TIME)) -lt $TEST_DURATION ]; do
    # Try to connect every 5 seconds
    timeout 5 ./client 127.0.0.1 > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        ((CRASH_COUNT++))
        echo "Connection failed at $(date)"
    fi
    sleep 5
done

TOTAL_TESTS=$(($TEST_DURATION / 5))
SUCCESS_RATE=$(echo "scale=2; (($TOTAL_TESTS - $CRASH_COUNT) * 100) / $TOTAL_TESTS" | bc)
echo "Uptime: $SUCCESS_RATE%"
