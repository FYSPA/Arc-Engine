#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/../.."
CPP_SRC="$PROJECT_DIR/android/src/main/cpp"

echo "=== Building C++ tests ==="
echo ""

echo "--- RingBuffer tests ---"
g++ -std=c++17 -I"$CPP_SRC" -o "$SCRIPT_DIR/ring_buffer_test" "$SCRIPT_DIR/ring_buffer_test.cpp" -lm
echo "OK"

echo "--- DspProcessor tests ---"
g++ -std=c++17 -I"$SCRIPT_DIR" -o "$SCRIPT_DIR/dsp_processor_test" "$SCRIPT_DIR/dsp_processor_test.cpp" -lm
echo "OK"

echo "--- RingBuffer benchmark ---"
g++ -std=c++17 -I"$CPP_SRC" -o "$SCRIPT_DIR/ring_buffer_benchmark" "$SCRIPT_DIR/ring_buffer_benchmark.cpp" -lm
echo "OK"

echo "--- DspProcessor benchmark ---"
g++ -std=c++17 -I"$SCRIPT_DIR" -o "$SCRIPT_DIR/dsp_processor_benchmark" "$SCRIPT_DIR/dsp_processor_benchmark.cpp" -lm
echo "OK"
echo ""

echo "=== Running C++ tests ==="
echo ""

echo "--- RingBuffer ---"
"$SCRIPT_DIR/ring_buffer_test"
echo ""
echo "--- DspProcessor ---"
"$SCRIPT_DIR/dsp_processor_test"
echo ""

echo "=== Running C++ benchmarks ==="
echo ""

echo "--- RingBuffer benchmark ---"
"$SCRIPT_DIR/ring_buffer_benchmark"
echo ""
echo "--- DspProcessor benchmark ---"
"$SCRIPT_DIR/dsp_processor_benchmark"
echo ""

echo "=== All done ==="
