#!/bin/bash
# Keep stdin open for 300 seconds to allow model to fully respond
exec 3<&0
(sleep 300; kill $$ 2>/dev/null) &
sleep_pid=$!
./build/skylark --model ~/.cache/huggingface/hub/models--litert-community--gemma-4-E2B-it-litert-lm --debug <&3
kill $sleep_pid 2>/dev/null