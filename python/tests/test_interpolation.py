import numpy as np
import pandas as pd
from scipy.stats.mstats import mquantiles
import rolling_quantiles as rq
from input import example_input

def test_innocuous_interpolation(window_size=1001, length=10000):
  pipe = rq.Pipeline(rq.LowPass(window=window_size, quantile=0.5))
  x = example_input(length)
  y = pipe.feed(x)
  z = pd.Series(x).rolling(window_size).median()
  assert np.equal(y[window_size:], z.values[window_size:]).all()

def test_typical_interpolation(window_size=40, quantile=0.2):
  x = example_input(window_size) # one window only, due to scipy
  pipe = rq.Pipeline(rq.LowPass(window=window_size, quantile=quantile))
  y = pipe.feed(x)
  z = mquantiles(x, quantile, alphap=1, betap=1)
  assert z == y[-1]

# a flavor of fuzzing
def test_fancy_interpolation(window_size=10, n_trials=200): # small windows may be more prone to boundary/edge-condition bugs
  for trial in range(n_trials):
    x = example_input(window_size)
    quantile = np.random.uniform()
    alpha, beta = np.random.uniform(size=2)
    pipe = rq.Pipeline(rq.LowPass(window=window_size, quantile=quantile, alpha=alpha, beta=beta))
    y = pipe.feed(x)
    z = mquantiles(x, quantile, alphap=alpha, betap=beta)
    assert z == y[-1]
