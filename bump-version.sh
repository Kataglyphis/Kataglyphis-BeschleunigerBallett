#!/usr/bin/env bash
# Usage: ./bump-version.sh <new_version>
# Example: ./bump-version.sh 1.6.0

NEW_VERSION=$1

if [ -z "$NEW_VERSION" ]; then
    echo "Error: No version provided."
    echo "Usage: ./bump-version.sh <new_version>"
    exit 1
fi

echo "$NEW_VERSION" > version.txt
echo "Version bumped to $NEW_VERSION in version.txt"
