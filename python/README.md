# Rolling Quantiles for NumPy
## Hyper-efficient and composable filters.

* Simple, clean, intuitive interface.
* Supports streaming data or bulk processing.
* Python 3 bindings for a compact library written in pure C.

### A Quick Tour

```python
import numpy as np
import rolling_quantiles as rq

pipe = rq.Pipeline( # rq.Pipeline is the only stateful object
  # declare a cascade of filters by a sequence of immutable description objects
  rq.LowPass(window=200, portion=100, subsample_rate=2),
    # the above takes a median (100 out of 200) of the most recent 200 points
    # and then spits out every other one
  rq.HighPass(window=10, portion=3,  subsample_rate=1))
    # that subsampled rolling median is then fed into this filter that takes a
    # 30% quantile on a window of size 10, and subtracts it from its raw input

# the pipeline exposes a set of read-only attributes that describe it
pipe.lag # = 60.0, the effective number of time units that the real-time output
         #   is delayed from the input
pipe.stride # = 2, how many inputs it takes to produce an output
            #  (>1 due to subsampling)


input = np.random.randn(1000)
output = pipe.feed(input) # the core, singular exposed method

# every other output will be a NaN to demarcate unready values
subsampled_output = output[1::pipe.stride]
```

See the [Github repository](https://github.com/marmarelis/rolling-quantiles) for more details.
