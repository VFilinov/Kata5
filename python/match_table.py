import argparse
import elo
from elo import GameRecord
import itertools
import math
import os
import re

from dataclasses import dataclass
from sgfmill import sgf
from typing import List, Dict, Tuple, Set, Sequence

class RenjuGameResultTable(elo.GameResultSummary):

    def __init__(
        self,
        elo_prior_games: float,
        estimate_first_player_advantage: bool,
    ):
        super().__init__(elo_prior_games, estimate_first_player_advantage)

    # @override
    def print_table(self):
        width_cell = 8
        dash = "-".center(width_cell)
        info = super().get_elos()
        players = info.get_players()
        results = super().get_game_results()
        row = []
        row.append(dash)
        for pla1 in players:
            row.append(pla1.center(width_cell))
        row.append("Total".center(width_cell))

        print("|".join(row))

        for pla1 in players:
            row = []
            row.append(pla1.ljust(width_cell))
            total = 0
            for pla2 in players:
                if (pla1 == pla2):
                    row.append(dash)
                    continue
                else:
                    pla1_pla2 = results[(pla1, pla2)] if (pla1, pla2) in results else GameRecord(pla1,pla2)
                    win = pla1_pla2.win + 0.5 * pla1_pla2.draw
                    total = total + win
                    row.append(str(win).center(width_cell))
            row.append(str(total).center(width_cell))
            print("|".join(row))

    # @override
    def is_game_file(self, input_file: str) -> bool:
        lower = input_file.lower()
        return input_file.endswith(".sgf") or input_file.endswith(".sgfs")

    # @override
    def get_game_records(self, input_file: str) -> List[GameRecord]:
        if input_file.lower().endswith(".sgfs"):
            with open(input_file, "rb") as f:
                sgfs_strings = f.readlines()

            records = []
            for sgf in sgfs_strings:
                record = self.sgf_string_to_game_record(sgf, input_file)
                if record is not None:
                    records.append(record)
            return records
        else:
            with open(input_file, "rb") as f:
                sgf = f.read()

            records = []
            record = self.sgf_string_to_game_record(sgf, input_file)
            if record is not None:
                records.append(record)
            return records

    def sgf_string_to_game_record(self, sgf_string, debug_source = None) -> GameRecord:
        try:
            # sgfmill for some reason can't handle rectangular boards, even though it's part of the SGF spec.
            # So lie and say that they're square, so that we can load them.
            sgf_string = re.sub(r'SZ\[(\d+):\d+\]', r'SZ[\1]', sgf_string.decode("utf-8"))
            sgf_string = sgf_string.encode("utf-8")

            game = sgf.Sgf_game.from_bytes(sgf_string)
            winner = game.get_winner()
        except ValueError:
            print ('\033[91m'+f"A sgf string is damaged in {debug_source}, and its record has been skipped!"+ '\x1b[0m')
            return
        pla_black = game.get_player_name('b')
        pla_white = game.get_player_name('w')

        game_record = GameRecord(player1=pla_black,player2=pla_white)
        if (winner == 'b'):
            game_record.win += 1
        elif (winner == 'w'):
            game_record.loss += 1
        else:
            game_record.draw += 1
        return game_record



if __name__ == "__main__":
    description = """
    Create table of results from SGF/SGFs files.
    """
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument(
        "input-files-or-dirs",
        help="sgf/sgfs files or directories of them",
        nargs="+",
    )
    parser.add_argument(
        "-recursive",
        help="Recursively search subdirectories of input directories",
        required=False,
        action="store_true",
    )
    args = vars(parser.parse_args())
    print(args)

    input_files_or_dirs = args["input-files-or-dirs"]
    recursive = args["recursive"]

    game_result_summary = RenjuGameResultTable(
        elo_prior_games=0,
        estimate_first_player_advantage=False,
    )
    for input_file_or_dir in input_files_or_dirs:
        game_result_summary.add_games_from_file_or_dir(input_file_or_dir, recursive=recursive)

    game_result_summary.print_table()
