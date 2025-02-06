#!/bin/bash

# Collect all file paths in ../user/bin into a single variable
files=""
for file in ../user/bin/*; 
do
    files="$files $file"
done

# Run ./mkfs with all the collected file paths
./mkfs kfs.raw $files

# Move the kfs.raw file to the kern directory
mv kfs.raw ../kern/

echo "kfs.raw has been created and moved to src/kern"