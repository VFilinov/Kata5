import os
import sys

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Use: python {sys.argv[0]} filename text")
        sys.exit(1)
    with open(sys.argv[1], 'r') as infile:
       for line in infile:
           if sys.argv[2] in line:
               sys.exit(0)

    sys.exit(1)
