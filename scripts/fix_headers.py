#!/usr/bin/env python3
"""
Convert headers to match slang library format.
"""

import re
import sys
from pathlib import Path

# Pattern to match old header format with doxygen
OLD_HEADER_PATTERN = re.compile(
    r'^//[-]+\n'
    r'// (.+)\n'
    r'// (.+)\n'
    r'//\n'
    r'// SPDX-FileCopyrightText: (.+)\n'
    r'// SPDX-License-Identifier: (.+)\n'
    r'//[-]+',
    re.MULTILINE
)

# Also match variant without closing dashes
OLD_HEADER_PATTERN2 = re.compile(
    r'^//[-]+\n'
    r'// (.+)\n'
    r'// (.+)\n'
    r'//\n'
    r'// SPDX-FileCopyrightText: (.+)\n'
    r'// SPDX-License-Identifier: (.+)',
    re.MULTILINE
)

# Pattern for already simplified format (no dashes)
SIMPLE_HEADER_PATTERN = re.compile(
    r'^// (.+)\n'
    r'// (.+)\n'
    r'//\n'
    r'// SPDX-FileCopyrightText: (.+)\n'
    r'// SPDX-License-Identifier: (.+)',
    re.MULTILINE
)

def convert_file(filepath: Path) -> bool:
    """Convert a single file's header format to slang library style."""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        
        original_content = content
        
        # Function to create the new header format
        def create_header(filename, description, copyright, license_id):
            # Verify filename matches
            if filename != filepath.name:
                print(f"Warning: Filename mismatch in {filepath}: '{filename}' != '{filepath.name}'")
                filename = filepath.name
            
            return (f"//------------------------------------------------------------------------------\n"
                   f"// {filename}\n"
                   f"// {description}\n"
                   f"//\n"
                   f"// SPDX-FileCopyrightText: {copyright}\n"
                   f"// SPDX-License-Identifier: {license_id}\n"
                   f"//------------------------------------------------------------------------------")
        
        # Try to match and replace old doxygen format
        def replace_old_header(match):
            return create_header(match.group(1), match.group(2), match.group(3), match.group(4))
        
        # Try first pattern (with closing dashes)
        content, count1 = OLD_HEADER_PATTERN.subn(replace_old_header, content, count=1)
        
        # If first didn't match, try second pattern (without closing dashes)
        if count1 == 0:
            content, count2 = OLD_HEADER_PATTERN2.subn(replace_old_header, content, count=1)
            total_count = count2
        else:
            total_count = count1
        
        # If old format didn't match, try simple format (no dashes)
        if total_count == 0:
            def replace_simple_header(match):
                return create_header(match.group(1), match.group(2), match.group(3), match.group(4))
            
            content, count3 = SIMPLE_HEADER_PATTERN.subn(replace_simple_header, content, count=1)
            total_count = count3
        
        if total_count > 0:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"✅ Updated: {filepath}")
            return True
        else:
            # Check if it already has the correct format
            if content.startswith("//------------------------------------------------------------------------------\n// " + filepath.name):
                print(f"⏭️  Already correct: {filepath}")
            elif content.startswith("// SPDX-"):
                print(f"⚠️  SPDX-only header: {filepath}")
            else:
                print(f"❌ No matching header: {filepath}")
            return False
            
    except Exception as e:
        print(f"❌ Error processing {filepath}: {e}")
        return False

def find_cpp_files(directory: Path) -> list[Path]:
    """Find all C++ files in directory."""
    files = []
    patterns = ['*.h', '*.hpp', '*.cpp', '*.cc']
    for pattern in patterns:
        files.extend(directory.rglob(pattern))
    return sorted(files)

def main():
    # Process all C++ files in the repository
    directories = [
        Path("include"),
        Path("src"),
        Path("tests/cpp")
    ]
    
    all_files = []
    for dir_path in directories:
        if dir_path.exists():
            all_files.extend(find_cpp_files(dir_path))
    
    if not all_files:
        print("No files found to process")
        return 1
    
    print(f"Processing {len(all_files)} files...")
    print()
    
    updated_count = 0
    for filepath in all_files:
        if convert_file(filepath):
            updated_count += 1
    
    print()
    print("=" * 60)
    print(f"Updated {updated_count} out of {len(all_files)} files")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())