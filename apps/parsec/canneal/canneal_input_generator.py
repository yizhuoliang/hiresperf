#!/usr/bin/python3
import random
import string
import argparse

def generate_random_id(length=4):
    """Generate random component ID of specified length"""
    return ''.join(random.choices(string.ascii_lowercase, k=length))

def generate_netlist(num_elements, grid_size, connections_per_elem):
    """
    Generate a netlist with specified parameters
    num_elements: Number of elements/components
    grid_size: Size of the chip grid (grid_size x grid_size)
    connections_per_elem: Number of connections per element
    """
    # Write header
    print(f"{num_elements}\t{grid_size}\t{grid_size}")
    
    # Generate elements
    for i in range(num_elements):
        # Element ID is a single letter followed by number for uniqueness
        elem_id = f"{chr(97 + (i % 26))}{i//26}"
        
        # Random weight (1 or 2 typically in original file)
        weight = random.randint(1, 2)
        
        # Generate connections
        connections = [generate_random_id() for _ in range(connections_per_elem)]
        
        # Print element line
        print(f"{elem_id}\t{weight}\t" + "\t".join(connections) + "\tEND")

def main():
    parser = argparse.ArgumentParser(description='Generate input netlist for PARSEC canneal')
    parser.add_argument('--size', type=int, default=2000000,
                        help='Number of elements (default: 2M elements ≈ 100MB)')
    parser.add_argument('--grid', type=int, default=2000,
                        help='Grid size (default: 2000x2000)')
    parser.add_argument('--connections', type=int, default=5,
                        help='Number of connections per element (default: 5)')
    
    args = parser.parse_args()
    
    # Generate the netlist
    generate_netlist(args.size, args.grid, args.connections)

if __name__ == "__main__":
    main()