#!/bin/bash

echo "Building and starting HTTP server demo..."
cd /large_nfs/risk-game-ai

# Build
bazel build //cpp/visualisations:http_server_demo

# Run in background
bazel-bin/cpp/visualisations/http_server_demo &
SERVER_PID=$!

echo "Waiting for server to start..."
sleep 2

echo "Fetching HTML from http://localhost:8080..."
curl -s http://localhost:8080 > /tmp/served_plot.html

echo "Server has served the content and stopped."

echo ""
echo "Content preview:"
head -35 /tmp/served_plot.html

echo ""
echo "Full HTML saved to /tmp/served_plot.html"
echo "You can open it in a browser: \$BROWSER file:///tmp/served_plot.html"
