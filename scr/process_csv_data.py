#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CSV Data Processing and Visualization Script
For processing CSV data with specific directory structure and generating statistical plots
"""

import os
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.lines as mlines
import numpy as np
import argparse
from pathlib import Path
import matplotlib

# Set English font
matplotlib.rcParams['font.family'] = 'DejaVu Sans'
matplotlib.rcParams['axes.unicode_minus'] = False  # Correctly display negative signs


def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(description='Process CSV data and generate statistical plots')
    parser.add_argument('--csv_dir', type=str, default='csv_data',
                        help='Directory containing CSV data (default: csv_data)')
    parser.add_argument('--output', type=str, default='statistical_plots.svg',
                        help='Output file name (default: statistical_plots.svg)')
    return parser.parse_args()


def scan_csv_directory(csv_dir):
    """
    Scan CSV directory, parse directory structure and return data information
    
    Args:
        csv_dir: CSV data root directory
        
    Returns:
        Dictionary containing all CSV file information, structure as:
        {
            "Page1": {
                "Table1": {
                    "Data1": "file_path",
                    "Data2": "file_path",
                    ...
                },
                ...
            },
            ...
        }
    """
    data_structure = {}
    
    # Ensure directory exists
    if not os.path.exists(csv_dir):
        print(f"Error: Directory {csv_dir} does not exist")
        return data_structure
    
    # Traverse directory structure: csv_data/PageTitle/TableTitle/DataTitle.csv
    for page_dir in os.listdir(csv_dir):
        page_path = os.path.join(csv_dir, page_dir)
        if not os.path.isdir(page_path):
            continue
            
        data_structure[page_dir] = {}
        
        for table_dir in os.listdir(page_path):
            table_path = os.path.join(page_path, table_dir)
            if not os.path.isdir(table_path):
                continue
                
            data_structure[page_dir][table_dir] = {}
            
            for csv_file in os.listdir(table_path):
                if csv_file.endswith('.csv'):
                    data_title = csv_file[:-4]  # Remove .csv extension
                    csv_path = os.path.join(table_path, csv_file)
                    data_structure[page_dir][table_dir][data_title] = csv_path
    
    return data_structure


def read_csv_data(csv_path):
    """
    Read CSV file data
    
    Args:
        csv_path: CSV file path
        
    Returns:
        Tuple containing x and y data (x_data, y_data)
    """
    try:
        df = pd.read_csv(csv_path)
        # Assume first column is Injection Rate, second column is Packet Average Latency
        x_data = df.iloc[:, 0].values  # Injection Rate
        y_data = df.iloc[:, 1].values  # Packet Average Latency
        return x_data, y_data
    except Exception as e:
        print(f"Error reading CSV file {csv_path}: {e}")
        return None, None


def generate_statistical_plots(data_structure, output_file="statistical_plots.svg"):
    """
    Generate statistical plots
    
    Args:
        data_structure: Dictionary containing all CSV file information
        output_file: Output image file name
    """
    if not data_structure:
        print("No data to plot")
        return
    
    # Calculate total pages and maximum tables per page
    page_count = len(data_structure)
    max_tables_per_page = max(len(tables) for tables in data_structure.values())
    
    # Create figure, one row per page, dynamic columns based on max tables
    fig, axes = plt.subplots(page_count, max_tables_per_page, figsize=(4*max_tables_per_page, 4*page_count))
    
    # Ensure axes is always a 2D array for consistent indexing
    if page_count == 1 and max_tables_per_page == 1:
        axes = np.array([[axes]])
    elif page_count == 1:
        axes = axes.reshape(1, -1)
    elif max_tables_per_page == 1:
        axes = axes.reshape(-1, 1)
    
    # Collect all data labels for legend
    all_data_labels = set()
    
    # First pass: collect all data labels
    for page_title, tables in data_structure.items():
        for table_title, data_files in tables.items():
            for data_title in data_files.keys():
                all_data_labels.add(data_title)
    
    # Sort labels for consistent ordering
    all_data_labels = sorted(list(all_data_labels))
    
    # Create a mapping from label to color and marker
    colors = plt.cm.tab10(np.linspace(0, 1, len(all_data_labels)))
    markers = ['o', 's', '^', 'D', 'v', '<', '>', 'p', '*', 'h']
    
    # Sort pages alphabetically for plotting
    sorted_pages = sorted(data_structure.items(), key=lambda x: x[0])
    
    # Traverse each page
    for page_idx, (page_title, tables) in enumerate(sorted_pages):
        # Add page title at the bottom of the row with more space
        fig.text(0.5, 1.0 - 0.33 * (page_idx + 1), page_title, 
                ha='center', va='center', fontsize=16)
        
        # Sort tables by title alphabetically
        sorted_tables = sorted(tables.items(), key=lambda x: x[0])
        
        # Traverse each table
        for table_idx, (table_title, data_files) in enumerate(sorted_tables):
            ax = axes[page_idx, table_idx]
            
            # Traverse each data file
            for data_title, csv_path in data_files.items():
                x_data, y_data = read_csv_data(csv_path)
                if x_data is not None and y_data is not None:
                    # Get consistent color and marker for this data label
                    label_idx = all_data_labels.index(data_title)
                    color = colors[label_idx % len(colors)]
                    marker = markers[label_idx % len(markers)]
                    
                    ax.plot(x_data, y_data, marker=marker, color=color, label=data_title)
            
            # Set table title
            ax.set_title(table_title, fontsize=12, fontweight='bold')
            
            # Set axis labels with bold font
            ax.set_xlabel('Injection Rate (0.001 packet/node/cycle)', fontweight='bold')
            ax.set_ylabel('Packet Average Latency (cycle)', fontweight='bold')
            
            # Add legend to each subplot in the upper left with sorted labels
            if data_files:
                # Sort data labels alphabetically for consistent legend order
                sorted_labels = sorted(data_files.keys())
                
                # Create legend handles with sorted labels
                legend_handles = []
                for label in sorted_labels:
                    for data_title, csv_path in data_files.items():
                        if data_title == label:
                            x_data, y_data = read_csv_data(csv_path)
                            if x_data is not None and y_data is not None:
                                label_idx = all_data_labels.index(data_title)
                                color = colors[label_idx % len(colors)]
                                marker = markers[label_idx % len(markers)]
                                # Create a line plot with the same style but without label
                                line, = ax.plot(x_data, y_data, marker=marker, color=color)
                                legend_handles.append(line)
                                break
                
                ax.legend(handles=legend_handles, labels=sorted_labels, loc='upper left', fontsize=8, framealpha=0.8)
            
            # Add grid
            ax.grid(True, linestyle='--', alpha=0.7)
        
        # Hide unused subplots
        for col_idx in range(len(sorted_tables), max_tables_per_page):
            axes[page_idx, col_idx].set_visible(False)
    
    # Fine-tune spacing between subplots
    plt.subplots_adjust(
        left=0.08,    # 左边距
        right=0.95,   # 右边距
        bottom=0.08,  # 下边距
        top=0.95,     # 上边距
        wspace=0.4,   # 子图水平间距
        hspace=0.6    # 子图垂直间距
    )
    
    # Save image as SVG
    plt.savefig(output_file, format='svg', dpi=300, bbox_inches='tight')
    print(f"Statistical plot saved to: {output_file}")
    
    # Show image
    plt.show()


def main():
    """
    Main function
    """
    # Parse command line arguments
    args = parse_arguments()
    
    # CSV data directory from command line
    csv_dir = args.csv_dir
    output_file = args.output
    
    # Scan CSV directory
    print(f"Scanning directory: {csv_dir}")
    data_structure = scan_csv_directory(csv_dir)
    
    if not data_structure:
        print("No CSV data found. Please ensure directory structure is: csv_data/PageTitle/TableTitle/DataTitle.csv")
        return
    
    # Print data structure
    print("\nDiscovered data structure:")
    # Sort pages alphabetically
    sorted_pages = sorted(data_structure.items(), key=lambda x: x[0])
    for page_title, tables in sorted_pages:
        print(f"Page: {page_title}")
        # Sort tables alphabetically
        sorted_tables = sorted(tables.items(), key=lambda x: x[0])
        for table_title, data_files in sorted_tables:
            print(f"  Table: {table_title}")
            for data_title in data_files.keys():
                print(f"    Data: {data_title}")
    
    # Generate statistical plots
    print("\nGenerating statistical plots...")
    generate_statistical_plots(data_structure, output_file)


if __name__ == "__main__":
    main()
