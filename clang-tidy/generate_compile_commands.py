#!/usr/bin/env python3
"""
Generate compile_commands.json from Visual Studio project files

Usage:
    python generate_compile_commands.py [options]

Options:
    -b, --build-dir <path>     Build directory containing .vcxproj files (default: ../build_Release)
    -s, --source-dir <path>    Source directory (default: parent directory)
    -o, --output <path>        Output file path (default: ../compile_commands.json)
    -h, --help                 Show this help message

Examples:
    python generate_compile_commands.py
    python generate_compile_commands.py -b ../build_Debug -o ../compile_commands_debug.json
    python generate_compile_commands.py --build-dir ../build --source-dir ../src
"""

import os
import json
import xml.etree.ElementTree as ET
import glob
import re
import argparse
import sys

def find_vcxproj_files(build_dir):
    """Find all .vcxproj files in the build directory"""
    vcxproj_files = []
    for root, dirs, files in os.walk(build_dir):
        for file in files:
            if file.endswith('.vcxproj'):
                vcxproj_files.append(os.path.join(root, file))
    return vcxproj_files

def parse_vcxproj(vcxproj_path):
    """Parse a .vcxproj file and extract compilation information"""
    try:
        tree = ET.parse(vcxproj_path)
        root = tree.getroot()
        
        # Extract namespace
        namespace = {'': 'http://schemas.microsoft.com/developer/msbuild/2003'}
        
        # Find all ClCompile items (source files)
        compile_items = []
        for item_group in root.findall('.//ItemGroup', namespace):
            for cl_compile in item_group.findall('ClCompile', namespace):
                include = cl_compile.get('Include')
                if include:
                    compile_items.append(include)
        
        # Extract preprocessor definitions
        preprocessor_defs = []
        for item_def_group in root.findall('.//ItemDefinitionGroup', namespace):
            for cl_compile in item_def_group.findall('.//ClCompile', namespace):
                for preproc_def in cl_compile.findall('PreprocessorDefinitions', namespace):
                    if preproc_def.text:
                        defs = preproc_def.text.split(';')
                        for def_item in defs:
                            if def_item and def_item != '%(PreprocessorDefinitions)':
                                preprocessor_defs.append(f'-D{def_item}')
        
        # Extract include directories
        include_dirs = []
        for item_def_group in root.findall('.//ItemDefinitionGroup', namespace):
            for cl_compile in item_def_group.findall('.//ClCompile', namespace):
                for add_inc_dir in cl_compile.findall('AdditionalIncludeDirectories', namespace):
                    if add_inc_dir.text:
                        dirs = add_inc_dir.text.split(';')
                        for dir_item in dirs:
                            if dir_item and dir_item != '%(AdditionalIncludeDirectories)':
                                # Convert relative paths to absolute
                                if not os.path.isabs(dir_item):
                                    dir_item = os.path.join(os.path.dirname(vcxproj_path), dir_item)
                                include_dirs.append(f'-I{os.path.normpath(dir_item)}')
        
        return compile_items, preprocessor_defs, include_dirs
        
    except Exception as e:
        print(f"Error parsing {vcxproj_path}: {e}")
        return [], [], []

def generate_compile_commands(build_dir, source_dir):
    """Generate compile_commands.json"""
    compile_commands = []
    
    # Find all .vcxproj files
    vcxproj_files = find_vcxproj_files(build_dir)
    print(f"Found {len(vcxproj_files)} .vcxproj files")
    
    # Common compiler flags for MSVC
    common_flags = [
        '/nologo',
        '/W3',
        '/EHsc',
        '/std:c++17',
        '/permissive-'
    ]
    
    for vcxproj_path in vcxproj_files:
        print(f"Processing: {vcxproj_path}")
        compile_items, preprocessor_defs, include_dirs = parse_vcxproj(vcxproj_path)
        
        for source_file in compile_items:
            # Convert relative paths to absolute
            if not os.path.isabs(source_file):
                source_file = os.path.join(os.path.dirname(vcxproj_path), source_file)
            source_file = os.path.normpath(source_file)
            
            # Only process .cpp, .c, .cxx files
            if not any(source_file.lower().endswith(ext) for ext in ['.cpp', '.c', '.cxx', '.cc']):
                continue
            
            # Check if source file exists
            if not os.path.exists(source_file):
                continue
            
            # Build command
            command_parts = ['cl.exe'] + common_flags + preprocessor_defs + include_dirs + ['/c', source_file]
            
            compile_command = {
                'directory': os.path.dirname(vcxproj_path),
                'command': ' '.join(command_parts),
                'file': source_file
            }
            
            compile_commands.append(compile_command)
    
    return compile_commands

def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description='Generate compile_commands.json from Visual Studio project files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
    python generate_compile_commands.py
    python generate_compile_commands.py -b ../build_Debug -o ../compile_commands_debug.json
    python generate_compile_commands.py --build-dir ../build --source-dir ../src
        """
    )
    
    parser.add_argument('-b', '--build-dir', 
                       default='../build_Release',
                       help='Build directory containing .vcxproj files (default: ../build_Release)')
    
    parser.add_argument('-s', '--source-dir',
                       default='..',
                       help='Source directory (default: parent directory)')
    
    parser.add_argument('-o', '--output',
                       default='../compile_commands.json',
                       help='Output file path (default: ../compile_commands.json)')
    
    return parser.parse_args()

def main():
    args = parse_arguments()
    
    # Convert to absolute paths for consistency
    build_dir = os.path.abspath(args.build_dir)
    source_dir = os.path.abspath(args.source_dir)
    output_file = os.path.abspath(args.output)
    
    # Check if build directory exists
    if not os.path.exists(build_dir):
        print(f"Error: Build directory '{build_dir}' does not exist!")
        print(f"Please make sure you have built the project with CMake first.")
        sys.exit(1)
    
    # Check if source directory exists
    if not os.path.exists(source_dir):
        print(f"Error: Source directory '{source_dir}' does not exist!")
        sys.exit(1)
    
    print(f"Build directory: {build_dir}")
    print(f"Source directory: {source_dir}")
    print(f"Output file: {output_file}")
    print()
    
    print("Generating compile_commands.json...")
    compile_commands = generate_compile_commands(build_dir, source_dir)
    
    if not compile_commands:
        print("Warning: No compile commands generated. This might indicate:")
        print("  - No .vcxproj files found in the build directory")
        print("  - No source files found in the project files")
        print("  - Build directory might be incorrect")
        sys.exit(1)
    
    print(f"Generated {len(compile_commands)} compile commands")
    
    # Create output directory if it doesn't exist
    output_dir = os.path.dirname(output_file)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # Write to file
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(compile_commands, f, indent=2)
        print(f"compile_commands.json written to: {output_file}")
        print()
        print("Success! You can now use clang-tidy with this compile_commands.json file.")
        print("Example usage:")
        print(f"  cd ..")
        print(f"  clang-tidy --config-file=clang-tidy/.clang-tidy <source-file>")
    except Exception as e:
        print(f"Error writing output file: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()