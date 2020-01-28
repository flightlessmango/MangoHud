"""
Generate the contents of the git_sha1.h file.
The output of this script goes to stdout.
"""


import argparse
import os
import os.path
import subprocess
import sys


def get_git_sha1():
    """Try to get the git SHA1 with git rev-parse."""
    git_dir = os.path.join(os.path.dirname(sys.argv[0]), '..', '.git')
    try:
        git_sha1 = subprocess.check_output([
            'git',
            '--git-dir=' + git_dir,
            'rev-parse',
            'HEAD',
        ], stderr=open(os.devnull, 'w')).decode("ascii")
    except:
        # don't print anything if it fails
        git_sha1 = ''
    return git_sha1

def write_if_different(contents):
    """
    Avoid touching the output file if it doesn't need modifications
    Useful to avoid triggering rebuilds when nothing has changed.
    """
    if os.path.isfile(args.output):
        with open(args.output, 'r') as file:
            if file.read() == contents:
                return
    with open(args.output, 'w') as file:
        file.write(contents)

parser = argparse.ArgumentParser()
parser.add_argument('--output', help='File to write the #define in',
                    required=True)
args = parser.parse_args()

git_sha1 = os.environ.get('MESA_GIT_SHA1_OVERRIDE', get_git_sha1())[:10]
if git_sha1:
    write_if_different('#define MESA_GIT_SHA1 " (git-' + git_sha1 + ')"')
else:
    write_if_different('#define MESA_GIT_SHA1 ""')
