# for pytest. I do not hook this up to pyproject.toml as it is intended or perhaps best practice.

import numpy as np
import pandas as pd
import rolling_quantiles as rq
from input import example_input

def test_median_scalar_inputs(window_size=3, length=100): # no interpolation yet
  pipe = rq.Pipeline(rq.LowPass(window=window_size, portion=window_size//2))
  v = example_input(length)
  for i, x in enumerate(v):
    y = pipe.feed(x)
    if i >= window_size:
      assert y == np.median(v[(i-window_size+1):(i+1)])

def test_median_array_input(window_size=71, length=1000):
  pipe = rq.Pipeline(rq.LowPass(window=window_size, portion=window_size//2))
  x = example_input(length)
  y = pipe.feed(x)
  z = pd.Series(x).rolling(window_size).median()
  assert np.equal(y[window_size:], z.values[window_size:]).all() # exact equality, since no arithmetic is done on the numbers
