# illustration of what my hypothetical API should look like

import numpy as np
import rolling_quantiles as rq

filter = rq.Pipeline( # stateful filter
  rq.LowPass(window=100, portion=50, subsample_rate=2),
  rq.HighPass(window=10, portion=3,  subsample_rate=1))

# expose specialized pipelines like `rq.MedianFilter`

input = np.random.randn(1000)
output = filter.feed(input) # a single `ufunc` entry point that takes in arrays or scalars and spits out an appropriate amount of output


## CONCEPT.

rq.LineUp(rq.Pipeline) # possibly parallelized execution of parallel pipelines

big_input = np.random.randn(100, 1000)
# broadcast. route one row to each pipeline.
big_output = pipes.feed(big_input) # respects Fortran or C ordering to preserve cache locality
