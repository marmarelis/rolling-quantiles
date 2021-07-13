# A Python module is basically a file. A Python package is a directory that acts as a parent module with many submodules.

__version__ = "1.1.0"

from .triton import *

# expose a rolling-median convenience method as a direct replacement to scipy.signal.medfilt
def medfilt(signal, window_size):
  import numpy as np # don't pollute the top-level namespace
  pipeline = Pipeline(
    LowPass(window=window_size, quantile=0.5, subsample_rate=1))
  return pipeline.feed(np.array(signal))
