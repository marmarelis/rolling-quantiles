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

#include "filter.h"

#include <stdlib.h>
#include <tgmath.h>
#include <stdbool.h>

// only supports adding and obtaining the middle element
struct high_pass_buffer {
  // this `head` is unsigned (rather than a pointer) because we will do math on it
  // points to the element right after the latest entry
  unsigned head;
  unsigned size;
  bool full;
  double entries[];
};

static struct high_pass_buffer* create_high_pass_buffer(unsigned size) {
  struct high_pass_buffer* buffer = malloc(sizeof(struct high_pass_buffer) + sizeof(double)*size);
  buffer->head = 0;
  buffer->size = size;
  buffer->full = false;
  // buffer->entries remains uninitialized on purpose
  return buffer;
}

static void add_to_high_pass_buffer(struct high_pass_buffer* buffer, double value) {
  if (buffer->head == buffer->size) {
    buffer->full = true; // always set---would be more expensive to read and conditionally write
    buffer->head = 0;
  }
  buffer->entries[buffer->head++] = value;
}

static double find_high_pass_buffer_middle(struct high_pass_buffer* buffer) {
  if (!buffer->full) {
    // match the below, which subtracts in the other direction. we're implictly rounding up, in a way, by not subtracting one
    int half = buffer->head / 2; // should optimize to bit shifts. don't use the remainder, (buffer->head % 2)
    return buffer->entries[half];
  }
  int half = (buffer->size / 2) + (buffer->size % 2);
  // by not subtracting one from head (and rounding `half` up,) I index the element to the right of the middle with even sizes
  int index = (int)buffer->head - half;
  if (index < 0)
    index = (int)buffer->size + index;
  return buffer->entries[index];
}

static void destroy_high_pass_buffer(struct high_pass_buffer* buffer) {
  free(buffer);
}

struct cascade_filter create_cascade_filter(struct cascade_description description) {
  unsigned portion = description.portion;
  double target = description.interpolation.target_quantile;
  if (!isnan(target)) {
    double target = compute_interpolation_target(
      description.window, description.interpolation);
    portion = (unsigned)floor(target) - 1;
  }
  struct cascade_filter filter = {
    .monitor = create_rolling_quantile_monitor(
      description.window, portion, description.interpolation),
    .clock = 0,
    .subsample_rate = description.subsample_rate,
    .high_pass_buffer = NULL,
  };
  if (description.mode == HIGH_PASS) {
    filter.high_pass_buffer = create_high_pass_buffer(description.window);
  }
  return filter;
}

struct filter_pipeline* create_filter_pipeline(unsigned n_filters, struct cascade_description* descriptions) {
  for (struct cascade_description* description = descriptions;
      description != (descriptions + n_filters); description += 1) {
    if (!validate_interpolation(description->interpolation))
      return NULL; // before allocating anything
  }
  struct filter_pipeline* pipeline = malloc(
    sizeof(struct filter_pipeline) + n_filters*sizeof(struct cascade_filter));
  pipeline->n_filters = n_filters;
  for (unsigned i = 0; i < n_filters; i += 1) {
    pipeline->filters[i] = create_cascade_filter(descriptions[i]);
  }
  return pipeline;
}

double feed_filter_pipeline(struct filter_pipeline* pipeline, double entry) {
  double trickling_value = entry;
  for (unsigned i = 0; i < pipeline->n_filters; i += 1) { // trickle down the pipeline
    struct cascade_filter* filter = pipeline->filters + i;
    double quantile = update_rolling_quantile(&filter->monitor, trickling_value);
    if (filter->high_pass_buffer != NULL) { // explicit conditional for enhanced clarity
      add_to_high_pass_buffer(filter->high_pass_buffer, trickling_value);
      double middle = find_high_pass_buffer_middle(filter->high_pass_buffer);
      trickling_value = middle - quantile;
    } else {
      trickling_value = quantile;
    }
    if ((++filter->clock) < filter->subsample_rate)
      return NAN;
    filter->clock = 0;
  }
  return trickling_value; // made it all the way through the torturous path!
}

bool verify_pipeline(struct filter_pipeline* pipeline) {
  for (unsigned i = 0; i < pipeline->n_filters; i += 1) {
    if (!verify_monitor(&pipeline->filters[i].monitor))
      return false;
  }
  return true;
}

void destroy_filter_pipeline(struct filter_pipeline* pipeline) {
  for (unsigned i = 0; i < pipeline->n_filters; i += 1) {
    destroy_rolling_quantile_monitor(&pipeline->filters[i].monitor);
    struct high_pass_buffer* buffer = pipeline->filters[i].high_pass_buffer;
    if (buffer != NULL) destroy_high_pass_buffer(buffer);
  }
  free(pipeline);
}
