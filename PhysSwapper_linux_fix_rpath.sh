#!/bin/bash

# note, don't modify 'vphysics_jolt' because that needs to search for the SIMD modules beside itself
files=$(find ./bin -type f \( -name "*vphysics_jolt*" -o -name "*vphysics_bullet*" \) ! -name "*.dbg")

for file in $files; do
    echo "Fixing rpath in file: $file"
    patchelf --print-rpath "$file"
    patchelf --force-rpath --set-rpath '$ORIGIN/../../../../../bin' "$file"
done
