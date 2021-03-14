import numpy as np
import pandas as pd
import rolling_quantiles as rq
from input import example_input

def test_median_scalar_inputs(window_size=3, length=100):
  pipe = rq.Pipeline(rq.HighPass(window=window_size, portion=window_size//2))
  v = example_input(length)
  assert pipe.lag == window_size/2
  for i, x in enumerate(v):
    y = pipe.feed(x)
    if i >= window_size:
      median = np.median(v[(i-window_size+1):(i+1)])
      assert y == (v[i-window_size//2] - median)

def test_median_array_input(window_size=71, length=1000):
  pipe = rq.Pipeline(rq.HighPass(window=window_size, portion=window_size//2))
  x = example_input(length)
  y = pipe.feed(x)
  z = pd.Series(x).rolling(window_size).median()
  lag = window_size//2 # note: as evidenced, high-pass filters do not interpolate on half-windows yet.
  assert pipe.lag == window_size/2
  assert np.equal(
      y[window_size:],
      x[lag+1:-lag] - z.values[window_size:]
    ).all() # exact equality.
