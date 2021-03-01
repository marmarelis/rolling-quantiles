# estimate the average number of values processed per second in offline mode (not streaming,
# although it's all the same for my technique) to compare against scipy. signals that are less
# stationary should induce more tree operations; hence, compare the following for different
# window sizes: Gaussian white noise, Brownian motion, and Levy flights.

# an interesting consequence is that my amortized runtime complexity is well-characterized,
# but in practice it depends on the signal behavior (so nondeterministic for stochastic processes)

import numpy as np
from scipy.signal import medfilt
from scipy.stats import levy
import pandas as pd
import rolling_quantiles as rq
import time
from matplotlib import pyplot as plt
plt.ion()

def measure_runtime(f):
  start = time.perf_counter() # could also try time.monotonic()
  res = f()
  return time.perf_counter() - start, res

signal = np.cumsum(np.random.normal(size=100_000_000))
series = pd.Series(signal) # construct a priori for fairness
window_sizes = np.array([4, 10, 20, 30, 40, 50]) + 1 # odd

rq_times, sc_times, pd_times = [], [], []

for window_size in window_sizes:
  pipe = rq.Pipeline(rq.LowPass(window=window_size, portion=window_size//2, subsample_rate=1))
  rq_time, rq_res = measure_runtime(lambda: pipe.feed(signal))
  sc_time, sc_res = measure_runtime(lambda: medfilt(signal, window_size))
  pd_time, pd_res = measure_runtime(lambda: series.rolling(window_size).quantile(0.5, interpolation="nearest"))
  # rq_res and sc_res will differ slightly at the edges because medfilt pads both sides with zeros as if it were a convolution.
  # I pad at the beginning only, since I employ an online algorithm.
  offset = window_size // 2
  discrepancy = rq_res[1000:2000] - sc_res[(1000-offset):(2000-offset)]
  #print("maximum discrepancy between the two is", np.amax(np.abs(discrepancy)))
  assert np.amax(np.abs(discrepancy)) < 1e-10
  print("runtimes are", rq_time, "versus", sc_time, "versus", pd_time)
  rq_times.append(rq_time)
  sc_times.append(sc_time)
  pd_times.append(pd_time)

plt.plot(window_sizes, rq_times)
plt.plot(window_sizes, sc_times)
plt.plot(window_sizes, pd_times)
