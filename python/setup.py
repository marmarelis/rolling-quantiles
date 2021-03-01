from setuptools.extension import Extension
from setuptools import setup # using this instead of numpy.distutils.core, as there seem to be incompatibilities with the "new way" of defining setups
from numpy.distutils.misc_util import get_info
import numpy as np
import os
import shutil
from glob import glob

# it feels like there's a billion different ways to do things: not very Pythonic! There is even overlap between pyproject.toml and setup.cfg!
# this all feels completely like a work in progress.

# NOTE (I learned this the hard way): DO NOT TRY TO IMPORT THIS PACKAGE FROM A PYTHON CONSOLE IN THIS DIRECTORY.
# IT WILL GRAVITATE TO THE LOCAL COPY, AND FAIL TO LOCATE TRITON.

source_files = sum((glob(os.path.join("..", "src", f"*.{ext}")) for ext in ["h", "c"]), start=[])
os.makedirs("src", exist_ok=True)
for file in source_files:
  shutil.copy(file, "src")

ext_files = ["filter.c", "heap.c", "quantile.c", "python.c"] # cryptic errors all ove rthe place...

setup(
  ext_package = "rolling_quantiles", # important to specify that triton's fully qualified name should be rolling_quantiles.triton
  ext_modules = [
    Extension("triton", # does a triton/__init__.py need to exist as a placeholder marker for my extension module?
      [os.path.join("src", file) for file in ext_files],
      include_dirs = [np.get_include()],
      extra_compile_args=["-O3"])
  ]
)
