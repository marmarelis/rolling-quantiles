import pytest

import rolling_quantiles as rq

def test_window_size():
  with pytest.raises(ValueError):
    rq.Pipeline(rq.LowPass())

def test_interpolator_bounds():
  with pytest.raises(ValueError):
    rq.Pipeline(rq.LowPass(
      window=10, portion=2, subsample_rate=1, quantile=0.5, alpha=2.0))
  with pytest.raises(ValueError):
    rq.Pipeline(rq.LowPass(
      window=10, portion=2, subsample_rate=1, quantile=2.5))
