#!/bin/bash

LOGFILE="chat_server_test_results_$(date +%Y%m%d_%H%M%S).txt"

# Function to timestamp logs
log() {
    echo "$(date +'%Y-%m-%d %H:%M:%S') - $1" | tee -a $LOGFILE
}

log "=== Chat Server Full Test Started ==="

# Step 1: Start server
log "Starting server..."
./server &
SERVER_PID=$!
sleep 3

# Step 2: Check baseline memory
log "Checking baseline memory usage"
BASELINE_MEM=$(ps -o rss= -p $SERVER_PID)
log "Baseline memory: ${BASELINE_MEM} KB"

# Step 3: Add 1000 users to user database (simulate large DB)
log "Adding 1000 users to users.db"
rm -f users.db
for i in {1..1000}; do
    echo "user$i pass$i 0 $(date +%s)" >> users.db
    # Sleep to prevent file system overload
    sleep 0.01
done
log "Added 1000 users"

# Step 4: Connect multiple clients to test concurrency (max clients limit 50)
log "Testing maximum concurrent clients (~50)"

PIDS=()
for i in {1..50}; do
    ./client 127.0.0.1 &
    PIDS+=($!)
    sleep 0.05
    log "Client $i connected"
done

# Wait for clients to stabilize
sleep 15

# Step 5: Check memory usage after clients connected
MEM_AFTER_CLIENTS=$(ps -o rss= -p $SERVER_PID)
log "Memory after clients connected: ${MEM_AFTER_CLIENTS} KB"
MEM_PER_CLIENT=$(( ($MEM_AFTER_CLIENTS - $BASELINE_MEM) / 50 ))
log "Approximate memory per client: ${MEM_PER_CLIENT} KB"

# Step 6: Test Latency
log "Testing message latency"

# Run a small latency client that sends a ping and times round-trip (simple version)
# Here we simulate latency measurement by timestamping output from client

# Cleanup old latency results
rm -f latency_results.txt

for i in {1..5}; do
  START_TIME=$(date +%s%3N)
  echo "ping" | timeout 5 ./client 127.0.0.1 > /tmp/client_out$i.txt 2>&1
  END_TIME=$(date +%s%3N)
  LATENCY=$((END_TIME - START_TIME))
  echo "Ping $i latency: $LATENCY ms" | tee -a latency_results.txt
  sleep 1
done
log "Latency test completed"

# Step 7: Stress Test for 30 minutes
log "Starting 30-minute stress test for uptime and connection stability"

START_TIME=$(date +%s)
DURATION=1800 # seconds = 30 minutes
FAILURES=0

while [ $(( $(date +%s) - $START_TIME )) -lt $DURATION ]; do
    timeout 10 ./client 127.0.0.1 > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        ((FAILURES++))
        log "Connection failed at $(date)"
    fi
    sleep 5
done

TESTS=$(( $DURATION / 5 ))
SUCCESS_RATE=$(echo "scale=2; ( ($TESTS - $FAILURES) * 100 / $TESTS )" | bc)
log "Stress test complete with uptime: $SUCCESS_RATE%"

# Step 8: Clean up all clients
log "Cleaning up client processes"
for pid in "${PIDS[@]}"; do
    kill $pid 2>/dev/null
    wait $pid 2>/dev/null
    log "Killed client pid $pid"
done

# Step 9: Stop server
log "Stopping server"
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null

# Step 10: Summarize latency results
log "Latency test results:"
cat latency_results.txt | tee -a $LOGFILE

log "=== Chat Server Full Test Completed ==="
