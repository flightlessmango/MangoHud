#!/usr/bin/env python

r"""
    Script to plot all the MangoHud benchmarks contained in a given folder.
"""
from pathlib import Path
import argparse
import csv

from typing import List, Union

import numpy as np

import matplotlib.pyplot as plt
from matplotlib.widgets import Cursor
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.ticker import EngFormatter

plt.rcParams['font.family'] = "Lato,serif"
plt.rcParams['font.weight'] = "600"

background_color = "#1A1C1D"
legend_facecolor = "#585f63"
legend_textcolor = "#cccbc9"
text_color = "#e8e6e3"
mango_color = "#BB770A"
graphbox_linewidth = 1.5

mango_cmap = LinearSegmentedColormap.from_list("mango_heat", [background_color, mango_color])

def identity(val):
    r"""
        returns the value as-is
    """
    return val


def get_integer(val: str) -> int:
    r"""
        interprets the str 'val' as an integer and returns it
    """
    if is_integer(val):
        return int(val)
    else:
        raise ValueError("Casting a non integer value: ", val)


def is_integer(s: str) -> bool:
    r"""
        tests if 's' is an integer and returns a bool
    """
    try:
        int(s)
        return True
    except ValueError:
        return False


def get_float(val):
    r"""
        interprets the str 'val' as a float and returns it
    """
    if is_float(val):
        return float(val)
    else:
        return float("nan")


def is_float(s: str) -> bool:
    r"""
        tests if 's' is an float and returns a bool
    """
    try:
        float(s)
        return True
    except ValueError:
        return False


class Database:
    r"""
        A class that contains all the csv files within
        the folder that it is instanced with
    """
    def __init__(self,
                 data_folder_path=None,
                 csv_separator=" ",
                 filename_var_separator="|"):

        self.datafiles = []
        self.result_names_col = None
        self.result_values_col = None
        self.sim_settings_names_col = None
        self.sim_settings_values_col = None

        if data_folder_path:
            self.load_from_folder(
                data_folder_path,
                csv_separator,
                filename_var_separator)

    def load_from_folder(self,
                         data_folder_path,
                         csv_separator=" ",
                         filename_var_separator="|"):
        r"""
            Load all CSV files form the given folder
        """
        filepaths = list(Path(data_folder_path).rglob("*.csv"))

        self.datafiles = []
        N = len(filepaths)

        print(f"Loading {N} benchmark files")
        for filepath in filepaths:
            try:
                datafile = BenchmarkFile(
                    str(filepath),
                    csv_separator=csv_separator,
                    filename_var_separator=filename_var_separator)
                self.datafiles.append(datafile)
            except Exception:
                pass

        self.datafiles.sort()


class BenchmarkFile:
    r"""
        A class that represents a single CSV file, can load CSV files
        with arbitrary separators. It can return separately any column
        of the file and any mathematical combinations of its columns.
    """

    def __init__(self,
                 filepath="",
                 filename_var_separator="|",
                 csv_separator=" "):
        self.csv_separator = csv_separator
        self.filepath = Path(filepath)
        self.filename = self.filepath.name
        self.filename_var_separator = filename_var_separator
        self.variables = dict()

        self.skip_lines = None

        self.columns = []
        self.column_name_to_index = dict()

        self._is_data_loaded = False

        if not self.filepath.is_file():
            raise Exception("CSV file does not exist")

        self._read_column_names()

    def __lt__(self, other):
        stem = self.filename[:-4]  # remove the trailing ".csv"
        other_stem = other.filename[:-4]
        if stem.startswith(other_stem):
            return False
        elif stem.startswith(other_stem):
            return True
        else:
            return stem < other_stem

    def set_variable(self, name, value):
        r"""
            Saves a variable within the datafile instance
            Note: it will not be saved to disk, it's just a helper method to
                  attach variables to a given data file.
        """
        self.variables[name] = value

    def get_variable(self, name):
        r"""
            Retrieves a saved variable in the instance
        """
        return self.variables[name]

    def _read_column_names(self):
        r"""
            Read the first few lines of the benchmark file
            to look for the row taht contains the benchmark's
            column names i.e. "fps", "frametime", "cpu_load"... etc
            and save the columns names and their index

            Note: we decide that we found the right row by looking if it
                  contains "fps"
                  not the best approach, but it works TM
        """

        with open(self.filepath) as open_file:
            reader = csv.reader(open_file, delimiter=self.csv_separator)

            found_fps_column = False
            for row_number, row_content in enumerate(reader):
                if row_number > 4:
                    # if we're past the 4th row, break the loop
                    break

                if "fps" in row_content:
                    self.skip_lines = row_number + 1
                    found_fps_column = True

                    for col, col_name in enumerate(row_content):
                        if col_name in self.column_name_to_index:
                            raise Exception("Two columns have the same name")
                        self.column_name_to_index[col_name] = col

            if not found_fps_column:
                raise Exception("Not a benchmark file")

    def _load_data(self):
        r"""
            Load the benchmark data into memory.
        """

        def extend_columns(new_column_num):
            current_row_num = 0
            if self.columns:
                current_row_num = len(self.columns[0])
                assert (all([len(column) == current_row_num for column in self.columns]))

            current_column_num = len(self.columns)
            if new_column_num >= current_column_num:
                self.columns += [["" for j in range(current_row_num)] for i in range(new_column_num - current_column_num)]

        # no need to load data if it's already loaded
        if self._is_data_loaded:
            return

        with open(self.filepath) as open_file:
            reader = csv.reader(open_file, delimiter=self.csv_separator)
            self._is_data_loaded = True

            for row_number, row_content in enumerate(reader):
                if row_number <= self.skip_lines:
                    continue

                extend_columns(len(row_content))
                for col, val in enumerate(row_content):
                    self.columns[col].append(val)

        # Delete any eventual empty column
        if all([val == "" for val in self.columns[-1]]):
            del self.columns[-1]

    def get_column_names(self) -> List[str]:
        r"""
        Returns the list of columns names of the csv file.
        """

        return list(self.column_name_to_index.keys())

    def get(self, col: str, data_type: str = "float") \
            -> Union[List[float], List[str], List[int], List[complex]]:
        r"""
        Returns the column `col`.

        Parameters
        ----------

        col : str
            The desired column name to retrieve, or its index

        data_type : str
            "string", "integer" or "float", the type to cast
            the data to before returning it.

        Returns
        -------

        A list of `data_type` containing the column `col`

        """

        if not self._is_data_loaded:
            self._load_data()

        if len(self.columns) == 0:
            raise ValueError("Datafile empty, can't return any data")

        data_caster_dict = {
            "string": identity,
            "float": get_float,
            "integer": get_integer
        }

        if data_type not in data_caster_dict:
            raise ValueError("the given `data_type' doesn't match any "
                             "known types. Which are `string', `integer', "
                             "`float' or `complex'")

        if is_integer(col):
            # the column's index is given
            return [data_caster_dict[data_type](val) for val in self.columns[col]]

        if col in self.column_name_to_index:
            # a column name has been given
            return [data_caster_dict[data_type](val)
                    for val in self.columns[self.column_name_to_index[col]]]

        raise Exception("Column {} does not exist".format(col))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description='Plot all the MangoHud benchmarks contained in a given folder.')

    parser.add_argument('folder', metavar='folder', nargs=1,
                        help='path the a MangoHud benchmark folder')

    args = parser.parse_args()

    bench_folder_path = Path(args.folder[0])

    if not bench_folder_path.is_dir():
        print(f"The path '{bench_folder_path.absolute()}' "
               "does not point to an existing folder")
        exit(1)

    fps_subdivs = 1.0  # one division every fps_subdivs FPS

    y_labels = []  # bench files
    x_labels = []  # FPS subidivions

    database = Database(bench_folder_path, csv_separator=',')
    distributions = []

    if len(database.datafiles) == 0:
        print(f"The folder \n   {bench_folder_path.absolute()} \n"
               "contains no CSV file "
               "(make sure they have the .csv extension)")
        exit(1)

    for datafile in database.datafiles:
        bar_distribution = []

        # sort array to get percentiles
        fps_array = np.sort(datafile.get("fps"))

        # save percentiles
        if len(fps_array) < 10000:
            print(f"'{datafile.filename}' simulation "
                   "isn't long enough for precise statistics")
            datafile.set_variable("selected", False)
            continue

        # Save label only if this file has long enough simulation
        y_labels.append(datafile.filename[:-4])
        datafile.set_variable("selected", True)

        # Save percentiles
        datafile.set_variable("0.1%", fps_array[int(float(len(fps_array))*0.001)])
        datafile.set_variable("1%", fps_array[int(float(len(fps_array))*0.01)])
        datafile.set_variable("50%", fps_array[int(float(len(fps_array))*0.5)])

        datafile.set_variable("average fps", np.average(fps_array))

        for frame_num, fps in enumerate(fps_array):
            if fps > 1000:
                print("FPS value above 1000, omitting outlier.")
                continue
            index = int(fps/fps_subdivs)
            for i in range(len(bar_distribution), index+1):
                bar_distribution.append(0)
            bar_distribution[index] += 1
        distributions.append(bar_distribution)

    if not distributions:
        print("Nothing to plot, exiting.")
        exit(1)

    num_benchs = len(distributions)
    max_size = 0
    for distrib in distributions:
        max_size = max(max_size, len(distrib))
    for distrib in distributions:
        for i in range(len(distrib), max_size):
            distrib.append(0)

    for i in range(max_size):
        x_labels.append(str(fps_subdivs * i))

    fig, ax = plt.subplots()

    # change color of the graph box to the same color as the text
    for spine in ['left', 'right', 'bottom', 'top']:
      ax.spines[spine].set_color(text_color)
      ax.spines[spine].set_linewidth(graphbox_linewidth)

    im = ax.imshow(distributions,
                   aspect="auto",
                   extent=[0, max_size*fps_subdivs, 0, num_benchs],
                   cmap=mango_cmap)

    # draw thick line that separates each benchmark
    for i in range(len(y_labels)+1):
        ax.axhline(float(i), color=text_color, lw=graphbox_linewidth)

    i = 0
    for datafile in database.datafiles:
        if datafile.get_variable("selected"):
            kwargs = dict(ymin=(num_benchs-i-1+0.15)/num_benchs,
                          ymax=(num_benchs-i-0.15)/num_benchs,
                          lw=3)

            ax.axvline(datafile.get_variable("0.1%"),
                       color='#35260f',
                       label=("0.1%" if i == 0 else None), **kwargs)

            ax.axvline(datafile.get_variable("1%"),
                       color='#6E4503',
                       label=("1%" if i == 0 else None), **kwargs)

            ax.axvline(datafile.get_variable("50%"),
                       color='#0967BA',
                       label=("50%" if i == 0 else None), **kwargs)

            ax.axvline(datafile.get_variable("average fps"),
                       color='#003A6E',
                       label=("Average" if i == 0 else None), **kwargs)
            i += 1

    ax.tick_params(axis='y', colors=text_color)
    ax.tick_params(axis='x', colors=text_color)
    ax.set_yticks(np.arange(len(y_labels)-0.5, 0, -1), labels=y_labels)
    ax.grid(False)

    fig.set_facecolor(background_color)

    ax.ticklabel_format(axis='x', style='plain')

    formatter0 = EngFormatter(unit='FPS')
    ax.xaxis.set_major_formatter(formatter0)

    plt.tight_layout()
    plt.legend(facecolor=legend_facecolor, labelcolor=legend_textcolor)

    cursor = Cursor(ax,
                    horizOn=False,
                    color='#6c49abff',
                    linewidth=4,
                    useblit=True)

    plt.show()
