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

#include "heap.h"
#include "quantile.h"
#include "filter.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

void test_single_heap(void) {
  struct ring_buffer* queue = create_queue(9);
  struct heap* heap = create_heap(MAX_HEAP, 10, queue);
  struct heap_element* elem;
  for (double i = 1.0; i < 15.0; i += 1.0) {
    elem = add_value_to_heap(heap, i);
    expire_stale_entry_in_queue(queue, 1, heap);
    register_in_queue(queue, elem);
  }
  for (unsigned i = 0; i < 10; i += 1) {
    printf("%f\n", remove_front_element_from_heap(heap).member);
  }
}

void test_multiple_heaps(void) {
  struct ring_buffer* queue = create_queue(9);
  struct heap* heap1 = create_heap(MAX_HEAP, 10, queue);
  struct heap* heap2 = create_heap(MAX_HEAP, 10, queue);
  struct heap* heap = heap1;
  for (double i = 1.0; i < 50.0; i += 1.0) {
    heap = heap==heap1? heap2 : heap1;
    struct heap_element* elem = add_value_to_heap(heap, i);
    expire_stale_entry_in_queue(queue, 2, heap1, heap2);
    register_in_queue(queue, elem);
  }
  for (unsigned i = 0; i < 10; i += 1) {
    printf("%f\n", remove_front_element_from_heap(heap).member);
  }
}

double generate_random_value(void) {
  return (double)rand() / (double)RAND_MAX;
}

void test_quantile(void) {
  printf("Testing...\n");
  struct rolling_quantile monitor = create_rolling_quantile_monitor(5, 2);
  double test_entries[] = {4.0, 2.0, 3.0, 2.5, 4.5, 3.5, 2.7, 3.9, 3.8, 3.1};
  unsigned test_size = sizeof(test_entries) / sizeof(double);
  for (unsigned i = 0; i < test_size; i += 1) {
    double quantile = update_rolling_quantile(&monitor, test_entries[i]);
    printf("%f\n", quantile);
  }
}

void stress_test_quantile_for_correctness(unsigned size, unsigned n_iterations) {
  printf("Stress-testing...\n");
  if (size % 2 == 0) size += 1;
  unsigned middle = (size-1)/2;
  struct rolling_quantile monitor = create_rolling_quantile_monitor(size, middle);
  double* window = malloc(size*sizeof(double));
  double* buffer = malloc(size*sizeof(double));
  bool* unsorted = malloc(size*sizeof(bool));
  unsigned window_pos = 0;
  for (unsigned i = 0; i < size; i += 1) {
    double value = generate_random_value();
    update_rolling_quantile(&monitor, value);
    window[i] = value;
  }
  for (unsigned t = 0; t < n_iterations; t += 1) {
    double value = generate_random_value();
    struct timespec timespec;
    clock_gettime(CLOCK_REALTIME, &timespec);
    double begin_time = (double)timespec.tv_sec + ((double)timespec.tv_nsec / 1e9);
    double pred_median = update_rolling_quantile(&monitor, value);
    clock_gettime(CLOCK_REALTIME, &timespec);
    double end_time = (double)timespec.tv_sec + ((double)timespec.tv_nsec / 1e9);
    printf("%.3e seconds; ", end_time - begin_time);
    window[window_pos++] = value;
    if (window_pos == size)
      window_pos = 0;
    // perform selection sort now, building up our one buffer
    for (unsigned i = 0; i < size; i += 1)
      unsorted[i] = true;
    for (unsigned i = 0; i < size; i += 1) {
      double min = INFINITY;
      unsigned min_ind; // UNINITIALIZED
      for (unsigned j = 0; j < size; j += 1) {
        if ((window[j] <= min) && unsorted[j]) {
          min = window[j];
          min_ind = j;
        }
      }
      buffer[i] = min;
      unsorted[min_ind] = false;
    }
    // now buffer is sorted
    double median = buffer[middle];
    //for (unsigned i = 0; i < size; i += 1) printf("    %f    ", window[i]);
    //for (unsigned i = 0; i < monitor->left_heap->n_entries; i += 1) printf("\n%f", monitor->left_heap->elements[i].member);
    //printf("\n %f\n", monitor->current_value.member);
    //for (unsigned i = 0; i < monitor->right_heap->n_entries; i += 1) printf("%f\n", monitor->right_heap->elements[i].member);
    printf("%f %f %f %d %d\n", value, pred_median, median, pred_median==median, verify_monitor(&monitor));
  }
}

void test_pipeline(void) {
  struct cascade_description descriptions[] = {
    {.window = 10, .portion = 2, .subsample_rate = 2, .mode = LOW_PASS},
    {.window = 3,  .portion = 1, .subsample_rate = 1, .mode = HIGH_PASS}
  };
  struct filter_pipeline* pipeline = create_filter_pipeline(2, descriptions);
  double test_entries[] = {4.0, 2.0, 3.0, 2.5, 1.5, 1.2, 1.7, 0.9, 0.8, 1.1, 0.1, 0.3};
  unsigned test_size = sizeof(test_entries) / sizeof(double);
  for (unsigned i = 0; i < test_size; i += 1) {
    double output = feed_filter_pipeline(pipeline, test_entries[i]);
    printf("%f\n", output);
  }
  destroy_filter_pipeline(pipeline);
}

int main(void) {
  //test_quantile();
  //stress_test_quantile_for_correctness(3001, 10000);
  test_pipeline();
}
