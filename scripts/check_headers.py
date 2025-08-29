#!/usr/bin/env python3
"""
Check that C++ header and source files have proper SPDX headers and documentation.
"""

import argparse
import re
import sys
from pathlib import Path
from typing import List, Optional, Tuple

# Expected header format patterns (matching slang library style)
# Format: //------------------------------------------------------------------------------
#         // Filename.ext
#         // Brief description of the file
#         //
#         // SPDX-FileCopyrightText: ...
#         // SPDX-License-Identifier: ...
#         //------------------------------------------------------------------------------
HEADER_PATTERN = re.compile(
    r'^//[-]+\n'
    r'// (.+)\n'
    r'// (.+)\n'
    r'//\n'
    r'// SPDX-FileCopyrightText: (.+)\n'
    r'// SPDX-License-Identifier: (.+)\n'
    r'//[-]+',
    re.MULTILINE
)

# Minimal SPDX-only pattern (missing description)
SIMPLE_HEADER_PATTERN = re.compile(
    r'^// SPDX-FileCopyrightText: (.+)\n'
    r'// SPDX-License-Identifier: (.+)',
    re.MULTILINE
)

class HeaderCheckResult:
    """Result of checking a single file's header."""
    
    def __init__(self, filepath: Path):
        self.filepath = filepath
        self.has_header = False
        self.has_doxygen = False
        self.has_spdx = False
        self.filename_matches = False
        self.errors: List[str] = []
        self.warnings: List[str] = []

def check_file_header(filepath: Path) -> HeaderCheckResult:
    """Check if a file has the proper header format."""
    result = HeaderCheckResult(filepath)
    
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            # Read first 10 lines (should be enough for header)
            lines = []
            for _ in range(10):
                line = f.readline()
                if not line:
                    break
                lines.append(line)
            
            header_text = ''.join(lines)
            
            # Check for full header with filename and description
            match = HEADER_PATTERN.match(header_text)
            if match:
                result.has_header = True
                result.has_doxygen = True  # Has description
                result.has_spdx = True
                
                # Check if filename matches actual filename
                file_in_header = match.group(1)
                actual_filename = filepath.name
                if file_in_header == actual_filename:
                    result.filename_matches = True
                else:
                    result.errors.append(
                        f"Filename mismatch: '{file_in_header}' != '{actual_filename}'"
                    )
                
                # Check SPDX fields
                copyright = match.group(3)
                license_id = match.group(4)
                
                if not copyright:
                    result.errors.append("Empty SPDX-FileCopyrightText")
                if not license_id:
                    result.errors.append("Empty SPDX-License-Identifier")
                    
            else:
                # Check for simple SPDX header without description
                simple_match = SIMPLE_HEADER_PATTERN.match(header_text)
                if simple_match:
                    result.has_header = True
                    result.has_spdx = True
                    result.warnings.append("Missing filename and description comment")
                    
                    copyright = simple_match.group(1)
                    license_id = simple_match.group(2)
                    
                    if not copyright:
                        result.errors.append("Empty SPDX-FileCopyrightText")
                    if not license_id:
                        result.errors.append("Empty SPDX-License-Identifier")
                else:
                    result.errors.append("Missing or malformed header")
                    
    except Exception as e:
        result.errors.append(f"Error reading file: {e}")
    
    return result

def find_cpp_files(directory: Path, patterns: List[str]) -> List[Path]:
    """Find all C++ header and source files in directory."""
    files = []
    for pattern in patterns:
        files.extend(directory.rglob(pattern))
    return sorted(files)

def main():
    parser = argparse.ArgumentParser(
        description="Check C++ file headers for proper SPDX and documentation format"
    )
    parser.add_argument(
        "paths",
        nargs="*",
        default=["include", "src", "tests/cpp"],
        help="Paths to check (default: include src tests/cpp)"
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="Attempt to fix missing or incorrect headers (not implemented)"
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Show all files checked, not just errors"
    )
    parser.add_argument(
        "--patterns",
        default="*.h,*.cpp,*.hpp,*.cc",
        help="File patterns to check (comma-separated)"
    )
    
    args = parser.parse_args()
    
    # Parse file patterns
    patterns = args.patterns.split(',')
    
    # Find all files to check
    all_files = []
    for path_str in args.paths:
        path = Path(path_str)
        if path.is_file():
            all_files.append(path)
        elif path.is_dir():
            all_files.extend(find_cpp_files(path, patterns))
        else:
            print(f"Warning: Path not found: {path}", file=sys.stderr)
    
    if not all_files:
        print("No files found to check", file=sys.stderr)
        return 1
    
    # Check all files
    total_files = len(all_files)
    error_count = 0
    warning_count = 0
    
    print(f"Checking {total_files} files...")
    print()
    
    for filepath in all_files:
        result = check_file_header(filepath)
        
        has_issues = bool(result.errors or result.warnings)
        
        if args.verbose or has_issues:
            # Show relative path if possible
            try:
                display_path = filepath.relative_to(Path.cwd())
            except ValueError:
                display_path = filepath
            
            if result.errors:
                print(f"❌ {display_path}")
                for error in result.errors:
                    print(f"   ERROR: {error}")
                error_count += 1
            elif result.warnings:
                print(f"⚠️  {display_path}")
                for warning in result.warnings:
                    print(f"   WARNING: {warning}")
                warning_count += 1
            elif args.verbose:
                print(f"✅ {display_path}")
    
    # Summary
    print()
    print("=" * 60)
    print(f"Checked {total_files} files")
    print(f"Errors: {error_count}")
    print(f"Warnings: {warning_count}")
    print(f"Clean: {total_files - error_count - warning_count}")
    
    if error_count > 0:
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())