/*
  Copyright 2021 Myrl Marmarelis

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#ifndef FILTER_H
#define FILTER_H

#include "quantile.h"

/*
  For a high-pass, wherein I would subtract a smoothed signal from the raw, I
  would need to keep track of the temporal order so that I can refer back to
  the "middle" value.
    (IN TESTING) Perhaps it can be done rather straightforwardly by means
      of the ring_buffer, but I have not ventured into that question yet. I simply
      mention this because it appears to be a logical addition to the pipeline
      functionality/"DSL".
 */

/*
  The high-pass filter does not support missing values demarcated by NaN, as
  that mode relies upon the raw signal's availability. One could affix a
  low-pass filter onto a high-pass intake to "smooth out" the NaNs before
  they have a chance of entering the high-pass filter down the line.
 */

enum cascade_mode {
  HIGH_PASS, LOW_PASS
};

struct cascade_description {
  unsigned window;
  unsigned portion;
  struct interpolation interpolation; // if NAN, refer to `portion`
  unsigned subsample_rate;
  enum cascade_mode mode;
};

struct high_pass_buffer;

struct cascade_filter {
  struct rolling_quantile monitor;
  unsigned clock;
  unsigned subsample_rate;
  struct high_pass_buffer* high_pass_buffer; // set to NULL when a low pass is desired
};

struct filter_pipeline {
  unsigned n_filters;
  struct cascade_filter filters[];
};

struct cascade_filter create_cascade_filter(struct cascade_description description);
struct filter_pipeline* create_filter_pipeline(unsigned n_filters, struct cascade_description* descriptions);
double feed_filter_pipeline(struct filter_pipeline* pipeline, double entry);
bool verify_pipeline(struct filter_pipeline* pipeline);
void destroy_filter_pipeline(struct filter_pipeline* pipeline);


#endif
