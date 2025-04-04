import math
import sys
import os
import re
from sgfmill import sgf


if __name__ == "__main__":

  if len(sys.argv) != 2:
      print(f"Use: python {sys.argv[0]} input_file")
      sys.exit(1)

  input_file = sys.argv[1]

  results = []
  if input_file.lower().endswith(".sgfs"):
      with open(input_file, "r") as f:
          for line_number, line in enumerate(f, 1):
              game = sgf.Sgf_game.from_bytes(line)

  else:
      print(f"{sys.argv[1]} can be with extension .sgfs")
      sys.exit(1)





with open("foo.sgf", "rb") as f:
    game = sgf.Sgf_game.from_bytes(f.read())
winner = game.get_winner()
board_size = game.get_size()
root_node = game.get_root()
b_player = root_node.get("PB")
w_player = root_node.get("PW")
for node in game.get_main_sequence():
    print(node.get_move())