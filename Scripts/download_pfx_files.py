#!/usr/bin/env python3
"""Download only .pfx files from a WebDAV server.

This script uses the kataglyphis_webdavclient package to list folders/files
and downloads only files ending with the .pfx extension while preserving
remote folder structure under the provided local base path.

Usage:
    python scripts/download_pfx_files.py <hostname> <username> <password> <remote_base_path> <local_base_path>

The script intentionally keeps logic simple and relies on WebDavClient for
listing folders/files and authentication.
"""
from __future__ import annotations

import argparse
import urllib.parse
import sys
import os
import traceback
import getpass
import platform
from pathlib import Path

import requests
from kataglyphis_webdavclient.webdavclient import WebDavClient


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Download .pfx files from WebDAV server.")
    parser.add_argument("hostname", help="WebDAV server hostname")
    parser.add_argument("username", help="WebDAV server username")
    parser.add_argument("password", help="WebDAV server password")
    parser.add_argument("remote_base_path", help="Remote base path on the WebDAV server")
    parser.add_argument("local_base_path", help="Local base path to save files")
    return parser.parse_args()


def join_remote_url(*parts: str) -> str:
    cleaned = [p.strip('/') for p in parts if p]
    return '/'.join(cleaned)


def main() -> None:
    args = parse_args()

    client = WebDavClient(args.hostname, args.username, args.password)

    # Basic environment/debug info to help diagnose path errors on Windows
    try:
        print("DEBUG: Environment information:")
        print(f"  user={getpass.getuser()}")
        print(f"  platform={platform.platform()}")
        print(f"  cwd={Path.cwd()}")
        print(f"  python_executable={sys.executable}")
        print(f"  local_base_path_arg={args.local_base_path!r}")
        print(f"  remote_base_path_arg={args.remote_base_path!r}")
    except Exception:
        print("DEBUG: Failed to print environment info")
        print(traceback.format_exc())

    # iterative traversal - only download files that end with .pfx (case-insensitive)
    stack = [args.remote_base_path]
    global_remote_base_path = args.remote_base_path

    while stack:
        current_remote = stack.pop()
        url = join_remote_url(client.hostname, current_remote)
        try:
            files_on_host = client.list_files(url)
        except Exception as e:
            # log via print because logger may not be configured in CI
            print(f"Failed to list files for {url}: {e}")
            files_on_host = []

        for remote_file_path in files_on_host:
            # Get filename relative to remote base path
            try:
                file_name = client.filter_after_global_base_path(remote_file_path, global_remote_base_path)
            except Exception:
                # fallback to last path segment
                file_name = Path(urllib.parse.unquote(remote_file_path)).name

            decoded_filename = urllib.parse.unquote(file_name)
            if not decoded_filename.lower().endswith('.pfx'):
                # skip non-.pfx files
                continue

            # Build remote file URL
            remote_file_url = join_remote_url(client.hostname, current_remote, file_name)

            # Determine local path to save, preserve folder structure
            sub_path = client.get_sub_path(remote_file_path, global_remote_base_path)
            # remove filename from sub_path if present
            if sub_path.endswith(decoded_filename):
                sub_path = sub_path[: len(sub_path) - len(decoded_filename)]
            if sub_path == decoded_filename:
                sub_path = ""

            # SANITIZE: ensure sub_path is a relative path (strip any leading slashes)
            # on Windows a leading slash would make Path treat it as absolute and drop
            # the provided local base path, often causing "The system cannot find the path specified." errors.
            raw_sub_path = sub_path
            sub_path = sub_path.lstrip('/\\') if isinstance(sub_path, str) else str(sub_path).lstrip('/\\')

            local_file_path = Path(args.local_base_path) / Path(sub_path) / decoded_filename
            local_folder = local_file_path.parent

            # Extra debug before attempting to create directories / write files
            try:
                print("DEBUG: Preparing to save file")
                print(f"  remote_file_url={remote_file_url}")
                print(f"  raw_sub_path={raw_sub_path!r}")
                print(f"  sanitized_sub_path={sub_path!r}")
                print(f"  local_file_path={local_file_path} (absolute={local_file_path.is_absolute()})")
                try:
                    resolved = local_file_path.resolve(strict=False)
                    print(f"  resolved_local_file_path={resolved}")
                except Exception:
                    print("  resolved_local_file_path: <failed to resolve>")
                print(f"  local_folder={local_folder} (exists={local_folder.exists()})")
                # Print parent chain for diagnosis
                try:
                    parents = [str(p) for p in local_folder.parents]
                    print(f"  local_folder_parents={parents}")
                except Exception:
                    print("  local_folder_parents: <failed>")
                # Check access on top-level base path
                try:
                    base_exists = Path(args.local_base_path).exists()
                    print(f"  base_exists={base_exists}")
                    print(f"  os.access(base, W_OK)={os.access(args.local_base_path, os.W_OK)}")
                except Exception:
                    print("  base path access checks failed")
                    print(traceback.format_exc())
            except Exception:
                print("DEBUG: Failed debug prints before mkdir")
                print(traceback.format_exc())

            try:
                if not local_folder.exists():
                    # Attempt to create and print intermediate steps
                    print(f"DEBUG: Creating directory {local_folder}")
                    local_folder.mkdir(parents=True, exist_ok=True)
                    print(f"DEBUG: Directory created: {local_folder}")
            except Exception as e:
                print(f"Failed to create local directory {local_folder}: {e}")
                print(traceback.format_exc())
                # continue to next file instead of crashing the whole script
                continue

            print(f"Downloading {remote_file_url} -> {local_file_path}")
            try:
                resp = requests.get(remote_file_url, auth=client.auth, stream=True, timeout=30)
                if resp.status_code == 200:
                    try:
                        print(f"DEBUG: Opening file for write: {local_file_path}")
                        with local_file_path.open('wb') as fh:
                            for chunk in resp.iter_content(chunk_size=8192):
                                if chunk:
                                    fh.write(chunk)
                        print(f"DEBUG: Successfully wrote file: {local_file_path}")
                    except Exception as e:
                        print(f"Failed writing to {local_file_path}: {e}")
                        if isinstance(e, OSError):
                            print(f"  OSError errno={getattr(e, 'errno', None)} strerror={getattr(e, 'strerror', None)}")
                        print(traceback.format_exc())
                else:
                    print(f"Failed to download {remote_file_url}: HTTP {resp.status_code}")
            except Exception as e:
                print(f"Error downloading {remote_file_url}: {e}")
                print(traceback.format_exc())

        # discover subfolders and add to stack
        try:
            folders = client.list_folders(current_remote)
        except Exception as e:
            print(f"Failed to list folders for {current_remote}: {e}")
            folders = []

        for folder in folders:
            relative = '/'.join([p for p in [current_remote.rstrip('/'), folder] if p])
            stack.append(relative)


if __name__ == '__main__':
    main()
