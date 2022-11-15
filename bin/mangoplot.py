#!/usr/bin/env python

import sys
from pathlib import Path

import csv

import numpy as np

import matplotlib.pyplot as plt
from matplotlib.widgets import Cursor

from matplotlib.ticker import EngFormatter

from typing import List, Union


def identity(val):
    return val


def get_integer(val):
    if is_integer(val):
        return int(val)
    else:
        raise ValueError("Casting a non integer value: ", val)


def is_integer(s):
    try:
        int(s)
        return True
    except ValueError:
        return False


def get_float(val):
    if is_float(val):
        return float(val)
    else:
        return float("nan")


def is_float(s):
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
                 filename_var_separator="|",
                 skip_lines=0):

        self.datafiles = []
        self.result_names_col = None
        self.result_values_col = None
        self.sim_settings_names_col = None
        self.sim_settings_values_col = None

        if data_folder_path:
            self.load_from_folder(
                data_folder_path,
                csv_separator,
                filename_var_separator,
                skip_lines)

    def load_from_folder(self,
                         data_folder_path,
                         csv_separator=" ",
                         filename_var_separator="|",
                         skip_lines=0):
        r"""
            Load all CSV files form the given folder
        """
        filepaths = list(Path(data_folder_path).rglob("*.csv"))

        self.datafiles = []
        N = len(filepaths)

        print("Loading {} benchmark files".format(N))
        for filepath in filepaths:
            self.datafiles.append(
                DataFile(
                    str(filepath),
                    csv_separator=csv_separator,
                    filename_var_separator=filename_var_separator,
                    skip_lines=skip_lines
                )
            )

        self.datafiles.sort()


class DataFile:
    r"""
        A class that represents a single CSV file, can load CSV files
        with arbitrary separators. It can return separately any column
        of the file and any mathematical combinations of its columns.
    """

    def __init__(self,
                 filepath="",
                 filename_var_separator="|",
                 csv_separator=" ",
                 skip_lines=0):
        self.csv_separator = csv_separator
        self.filepath = Path(filepath)
        self.filename = self.filepath.name
        self.filename_var_separator = filename_var_separator
        self.variables = dict()

        self.skip_lines = skip_lines

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
        self.variables[name] = value

    def get_variable(self, name):
        return self.variables[name]

    def _read_column_names(self):
        with open(self.filepath) as openFile:
            reader = csv.reader(openFile, delimiter=self.csv_separator)

            for _ in range(self.skip_lines):
                reader.__next__()

            row_content = reader.__next__()

            for col, val in enumerate(row_content):
                col_name = val
                if col_name:
                    first = True
                    while col_name in self.column_name_to_index:
                        if first:
                            col_name += "_"
                            first = False
                        col_name += "b"
                    self.column_name_to_index[col_name] = col

    def _load_data(self):
        r"""
        Loads the CSV file pointed by `csv_file` into memory.
        """

        def extend_columns(new_column_num):
            current_row_num = 0
            if self.columns:
                current_row_num = len(self.columns[0])
                assert (all([len(column) == current_row_num for column in self.columns]))

            current_column_num = len(self.columns)
            if new_column_num >= current_column_num:
                self.columns += [["" for j in range(current_row_num)] for i in range(new_column_num - current_column_num)]

        with open(self.filepath) as openFile:
            reader = csv.reader(openFile, delimiter=self.csv_separator)
            self._is_data_loaded = True

            for row_number, row_content in enumerate(reader):
                if row_number > self.skip_lines:
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

        if data_type not in data_caster_dict.keys():
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
    bench_folder_path = None
    if len(sys.argv) > 1:
        bench_folder_path = sys.argv[1]
    else:
        bench_folder_path = input("Please enter benchmark folder path: ")

    if not Path(bench_folder_path).is_dir():
        print(("The entered path \n"
               "{}\n"
               "is not a folder").format(bench_folder_path))
        exit(1)

    fps_subdivs = 1.0  # one division every fps_subdivs FPS

    y_labels = []  # bench files
    x_labels = []  # FPS subidivions

    database = Database(bench_folder_path, csv_separator=',', skip_lines=2)
    distributions = []

    if len(database.datafiles) == 0:
        print(("The folder \n   {} \n contains no CSV file"
               "(make sure they have the .csv extension)").format(bench_folder_path))
        exit(1)

    for datafile in database.datafiles:
        bar_distribution = []

        # sort array to get percentiles
        fps_array = np.sort(datafile.get("fps"))

        # save percentiles
        if len(fps_array) < 10000:
            print("{} simulation isn't long enough for \
                   precise statistics".format(datafile.filename))
            datafile.set_variable("selected", False)
            continue

        # Save label only if this file has long enough simulation
        y_labels.append(datafile.filename.split('.')[0])
        datafile.set_variable("selected", True)

        # Save percentiles
        datafile.set_variable("0.1%", fps_array[int(float(len(fps_array))*0.001)])
        datafile.set_variable("1%", fps_array[int(float(len(fps_array))*0.01)])
        datafile.set_variable("50%", fps_array[int(float(len(fps_array))*0.5)])

        datafile.set_variable("average fps", np.average(fps_array))

        for frame_num, fps in enumerate(fps_array):
            if fps > 1000:
                print("something is wrong")
                continue
            index = int(fps/fps_subdivs)
            for i in range(len(bar_distribution), index+1):
                bar_distribution.append(0)
            bar_distribution[index] += 1
        distributions.append(bar_distribution)

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
    im = ax.imshow(distributions,
                   aspect="auto",
                   extent=[0, max_size*fps_subdivs, 0, num_benchs])

    for i in range(len(y_labels)+1):
        ax.axhline(float(i), color='white', lw=5)

    i = 0
    for datafile in database.datafiles:
        if datafile.get_variable("selected"):
            kwargs = dict(ymin=(num_benchs-i-1+0.15)/num_benchs,
                          ymax=(num_benchs-i-0.15)/num_benchs,
                          lw=5)

            ax.axvline(datafile.get_variable("0.1%"),
                       color='#f45d7bff',
                       label=("0.1%" if i == 0 else None), **kwargs)

            ax.axvline(datafile.get_variable("1%"),
                       color='#c879c1ff',
                       label=("1%" if i == 0 else None), **kwargs)

            ax.axvline(datafile.get_variable("50%"),
                       color='#7b4182ff',
                       label=("50%" if i == 0 else None), **kwargs)

            ax.axvline(datafile.get_variable("average fps"),
                       color='#336f74ff',
                       label=("Average" if i == 0 else None), **kwargs)
            i += 1

    ax.set_yticks(np.arange(len(y_labels)-0.5, 0, -1), labels=y_labels)
    ax.grid(False)

    ax.ticklabel_format(axis='x', style='plain')

    formatter0 = EngFormatter(unit='FPS')
    ax.xaxis.set_major_formatter(formatter0)

    plt.tight_layout()
    plt.legend()

    cursor = Cursor(ax,
                    horizOn=False,
                    color='#6c49abff',
                    linewidth=4,
                    useblit=True)

    plt.show()
