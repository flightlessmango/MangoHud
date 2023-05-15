import re
import subprocess

class Test:
    def __init__(self):
        self.options = {}
        self.error_count = 0
        # self.files_changed()
        self.get_options()
        self.get_param_defaults()
        self.find_options_in_readme()
        self.find_options_in_conf()

        if self.error_count > 0:
            print(f"number of errors: {self.error_count}")
            exit(1)

    def get_options(self):
        regex = r"\((.*?)\)"
        with open('../src/overlay_params.h') as f:
            for line in f:
                if ("OVERLAY_PARAM_BOOL" in line or "OVERLAY_PARAM_CUSTOM"
                    in line) and not "#" in line:
                    match = re.search(regex, line)
                    if match:
                        self.options[match.group(1)] = None

    def find_options_in_readme(self):
        with open("../README.md") as f:
            file = f.read()
            for option in self.options:
                if not option in file:
                    self.error_count += 1
                    print(f"Option: {option} is not found in README.md")

    def find_options_in_conf(self):
        with open("../data/MangoHud.conf") as f:
            file = f.read()
            for option, val in self.options.items():
                if not option in file:
                    self.error_count += 1
                    print(f"Option: {option} is not found in MangoHud.conf")
                if option in file:
                    option = "# " + option
                    for line in file.splitlines():
                        if option in line:
                            line = line.strip().split("=")
                            if len(line) != 2:
                                continue

                            key = line[0].strip("#").strip()
                            value = line[1].strip()
                            if "," in value:
                                value = value.split(",")

                            if self.options[key] != value:
                                self.error_count += 1
                                print(f"Sample config: option: {key} value is not the same as default")
                                print(f"default: {self.options[key]}, config: {value}")
                                print("")

    def get_param_defaults(self):
        # Open the C++ file
        with open('../src/overlay_params.cpp', 'r') as f:
            # Read the contents of the file
            contents = f.read()

            # Define the name of the function to search for
            function_name = 'set_param_defaults'

            # Define a regular expression to match the function definition
            function_regex = re.compile(r"void\s+" +
                                        function_name + r"\s*\(([^)]*)\)\s*{(.+?)\s*}\s*\n",
                                        re.MULTILINE | re.DOTALL)

            # Find the match of the regular expression in the file contents
            match = function_regex.search(contents)

            # If the function is found, extract the contents
            if match:
                # Extract the contents of the function
                function_contents = match.group(2)
                for line in function_contents.splitlines():
                    # FIXME: Some variables get stored as string in a string
                    if not "enabled" in line:
                        line = line.replace("params->", "")
                        line = line.strip().strip(";").split("=")
                        if len(line) != 2:
                            continue

                        key = line[0].strip()
                        value = line[1].strip()
                        # convert to a list if it contains curly bracket
                        if "{" in value:
                            value = value.replace("{", "").replace("}", "").strip().split(", ")
                            # If option has color in it's name we can assume it's value is
                            # one or more colors and that they are in binary.
                            # We want to convert this from binary because the config
                            # will not be in this format
                            if "color" in key:
                                value = [hex[2:] for hex in value]
                                value = [string.upper() for string in value]

                        # same reasoning as above
                        if "color" in key and type(value) is str:
                            value = value[2:]
                            value = value.upper()

                        if "fps_sampling_period" in key:
                            value = re.sub(r';\s*/\*.*?\*/', '', value)
                            value = str(int(int(value) / 1000000))
                        # FIXME sometimes we get a string inside a string
                        # which breaks comparisons
                        self.options[key] = value

Test()