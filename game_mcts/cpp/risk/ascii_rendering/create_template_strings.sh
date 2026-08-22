#!/usr/bin/bash

# Default values
W_START=62
W_END=79
TW=2
OUTPUT_PREFIX="ascii_rendering/tmpl"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --width-start)
            W_START="$2"
            shift 2
            ;;
        --width-end)
            W_END="$2"
            shift 2
            ;;
        --text-width)
            TW="$2"
            shift 2
            ;;
        --output-prefix)
            OUTPUT_PREFIX="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

for W in $(seq "$W_START" "$W_END"); do
    ./ascii_rendering/create_string_templates \
        optimize \
        -o "${OUTPUT_PREFIX}_${W}_${TW}.txt" \
        --text-width="${TW}" \
        --width="${W}" \
        --batch-size=4096
done