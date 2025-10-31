#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Parse log files in the logs directory and generate CSV files
For data processing with process_csv_data.py script
"""

import os
import re
import pandas as pd
from pathlib import Path
import argparse


def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(description='Parse log files and generate CSV files for data processing')
    parser.add_argument('--logs_dir', type=str, default='logs',
                        help='Directory containing log files (default: logs)')
    parser.add_argument('--csv_dir', type=str, default='csv_data',
                        help='Directory to save CSV files (default: csv_data)')
    return parser.parse_args()


def parse_log_filename(filename):
    """
    Parse log filename to extract components
    
    Args:
        filename: Log filename in format aaa_bbb_ccc_ddd_eee.log
        
    Returns:
        Dictionary containing components: {'page': aaa, 'table': bbb, 'data': ccc, 'injection_rate': eee}
    """
    # Remove .log extension
    base_name = filename[:-4] if filename.endswith('.log') else filename
    
    # Split filename
    parts = base_name.split('_')
    
    if len(parts) < 5:
        print(f"Warning: Incorrect filename format: {filename}")
        return None
    
    return {
        'page': parts[0],
        'table': parts[1],
        'data': parts[2],
        'injection_rate': parts[4]
    }


def extract_packet_latency(log_file_path):
    """
    Extract Packet latency average value from log file
    
    Args:
        log_file_path: Path to log file
        
    Returns:
        Packet latency average value, or None if not found
    """
    try:
        with open(log_file_path, 'r') as f:
            content = f.read()
            
        # Find Packet latency average line
        match = re.search(r'Packet latency average = ([\d.]+)', content)
        if match:
            return float(match.group(1))
        else:
            print(f"Warning: Packet latency average not found in file {log_file_path}")
            return None
    except Exception as e:
        print(f"Error: Error reading file {log_file_path}: {e}")
        return None


def create_csv_directory_structure(base_dir="csv_data"):
    """
    Create CSV directory structure
    
    Args:
        base_dir: CSV data root directory
    """
    if not os.path.exists(base_dir):
        os.makedirs(base_dir)
        print(f"Created directory: {base_dir}")


def process_logs_to_csv(logs_dir="logs", csv_dir="csv_data"):
    """
    Process all log files in logs directory and generate CSV files
    
    Args:
        logs_dir: Log files directory
        csv_dir: CSV output directory
    """
    # Ensure CSV directory exists
    create_csv_directory_structure(csv_dir)
    
    # Check if logs directory exists
    if not os.path.exists(logs_dir):
        print(f"Error: Directory {logs_dir} does not exist")
        return
    
    # Collect all data points, organized by page, table, data group
    data_structure = {}
    
    # Iterate through all files in logs directory
    for filename in os.listdir(logs_dir):
        if not filename.endswith('.log'):
            continue
            
        # Parse filename
        file_info = parse_log_filename(filename)
        if not file_info:
            continue
            
        # Extract Packet latency average
        log_file_path = os.path.join(logs_dir, filename)
        packet_latency = extract_packet_latency(log_file_path)
        
        if packet_latency is None:
            continue
            
        # Initialize data structure
        page = file_info['page']
        table = file_info['table']
        data = file_info['data']
        injection_rate = float(file_info['injection_rate'])
        
        if page not in data_structure:
            data_structure[page] = {}
        if table not in data_structure[page]:
            data_structure[page][table] = {}
        if data not in data_structure[page][table]:
            data_structure[page][table][data] = []
            
        # Add data point
        data_structure[page][table][data].append((injection_rate, packet_latency))
    
    # Create CSV files for each data group
    for page, tables in data_structure.items():
        # Create page directory
        page_dir = os.path.join(csv_dir, page)
        if not os.path.exists(page_dir):
            os.makedirs(page_dir)
            print(f"Created directory: {page_dir}")
            
        for table, data_groups in tables.items():
            # Create table directory
            table_dir = os.path.join(page_dir, table)
            if not os.path.exists(table_dir):
                os.makedirs(table_dir)
                print(f"Created directory: {table_dir}")
                
            for data, data_points in data_groups.items():
                # Sort by injection_rate
                data_points.sort(key=lambda x: x[0])
                
                # Create DataFrame
                df = pd.DataFrame(data_points, columns=['Injection Rate', 'Packet Average Latency'])
                
                # Save as CSV file
                csv_file = os.path.join(table_dir, f"{data}.csv")
                df.to_csv(csv_file, index=False)
                print(f"Created CSV file: {csv_file}")
    
    print(f"\nCSV files generated successfully, saved in {csv_dir} directory")


def main():
    """
    Main function
    """
    # Parse command line arguments
    args = parse_arguments()
    
    # Get directories from command line arguments
    logs_dir = args.logs_dir
    csv_dir = args.csv_dir
    
    print(f"Starting to parse log files from '{logs_dir}' and generate CSV files in '{csv_dir}'...")
    process_logs_to_csv(logs_dir, csv_dir)
    print("Processing completed!")


if __name__ == "__main__":
    main()
