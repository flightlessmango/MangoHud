import site
import sys
from setuptools import setup

if __name__ == "__main__":
    setup()

# See https://github.com/pypa/pip/issues/7953
site.ENABLE_USER_SITE = "--user" in sys.argv[1:]
