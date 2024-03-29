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

#include "quantile.h"
#include "heap.h"

#include <stdlib.h>
#include <tgmath.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

const struct interpolation NO_INTERPOLATION = { .target_quantile = NAN };

struct rolling_quantile create_rolling_quantile_monitor(unsigned window, unsigned portion, struct interpolation interp) {
  //if (window % 2 == 0) this only makes sense for the median special case.
  //  return NULL;
  struct ring_buffer* queue = create_queue(window);
  struct rolling_quantile monitor = {
    .queue = queue,
    .left_heap = create_heap(MAX_HEAP, portion + 1, queue),
    .right_heap = create_heap(MIN_HEAP, window - portion, queue), // - 1 and then + 1
    .current_value = (struct heap_element) {.member = NAN, .loc_in_buffer = NULL}, // to keep track of queue position
    .window = window,
    .portion = portion,
    .count = 0,
    .interpolation = interp,
  };
  return monitor;
}

void destroy_rolling_quantile_monitor(struct rolling_quantile* monitor) {
  destroy_heap(monitor->left_heap);
  destroy_heap(monitor->right_heap);
  destroy_queue(monitor->queue);
}

static bool is_between_zero_and_one(double val) { // null and unit
  return (val >= 0.0) && (val <= 1.0);
}

bool validate_interpolation(struct interpolation interp) {
  return isnan(interp.target_quantile) || (
    is_between_zero_and_one(interp.target_quantile) &&
    is_between_zero_and_one(interp.alpha) &&
    is_between_zero_and_one(interp.beta));
}

double compute_interpolation_target(unsigned window, struct interpolation interp) {
  double real_portion = (double)window * interp.target_quantile;
  double correction = interp.alpha +
    interp.target_quantile*(1.0 - interp.alpha - interp.beta);
  return real_portion + correction;
}

static double interpolate_current_rolling_quantile(struct rolling_quantile* monitor) {
  struct interpolation interp = monitor->interpolation; // is copy worth the locality?
  double target = compute_interpolation_target(monitor->window, interp);
  double gamma = target - floor(target); // must be between 0 and 1, but avoid checking for the sake of performance
  int index = (int)floor(target) - 1; // subtract one because `portion` refers to the number of items in the left heap (but `target_portion` does *not*)
  int portion = (int)monitor->portion;
  double current = monitor->current_value.member;
  if (index == portion) {
    if (monitor->right_heap->n_entries == 0)
      return current;
    double next = view_front_of_heap(monitor->right_heap);
    return (1.0-gamma)*current + gamma*next;
  } else if (index == (portion-1)) {
    if (monitor->left_heap->n_entries == 0)
      return current;
    double previous = view_front_of_heap(monitor->left_heap);
    return (1.0-gamma)*previous + gamma*current;
  }
  return NAN; // monitor.portion is uncalibrated/corrupted
}

/*
  Game plan.
    We shall first expel the stale entry, then add the new entry to its rightful receptacle based on its ordering wrt the current value.
    If a NaN is added, we will simply count it as a cycle without a new observation: old will be expelled with no replenishing.
    *Do not* contaminate the heaps with NaNs. That may cause their rebalancing to spiral out of control.
    Flushing. If the whole window empties, effectively reset the filter and revert `current_value` to its initial state.
*/
double update_rolling_quantile(struct rolling_quantile* monitor, double next_entry) {
  //unsigned left_entries = monitor->left_heap->n_entries;
  unsigned right_entries = monitor->right_heap->n_entries;
  //unsigned total_entries = left_entries + right_entries + 1;
  // we control the advancement ourselves, since it must happen exactly once per call to this method
  // this makes life much easier than engineering an overly clever ring-buffer interface
  advance_ring_buffer(monitor->queue);
  if (isnan(monitor->current_value.member)) { // total_entries will be 1 regardless of whether current_value has anything in it. we want to be careful, since NaNs will also signal missing values coming in
    if (isnan(next_entry))
      return NAN;
    monitor->current_value.member = next_entry;
    register_in_queue(monitor->queue, &monitor->current_value);
    monitor->count += 1;
    return next_entry;
  }
  int expired_in_heap = expire_stale_entry_in_queue(monitor->queue, 2, monitor->left_heap, monitor->right_heap);
  if (expired_in_heap == 0) { // expired, but did not belong to a heap
    if (monitor->queue->n_entries == 0) { // there do not exist other entries
      // basically reset and go again
      monitor->current_value.member = NAN;
      return update_rolling_quantile(monitor, next_entry); // a delicate corner case, looping us back to the top. tread carefully
    }
    struct heap* some_heap = (right_entries > 0)? monitor->right_heap : monitor->left_heap; // pick arbitrarily
    remove_front_element_from_heap(some_heap, &monitor->current_value);
  } // else if (expired_in_heap == -1) { ... } // there was nothing to expire
  if (!isnan(next_entry)) {
    struct heap* heap_for_next = (next_entry > monitor->current_value.member)? monitor->right_heap : monitor->left_heap;
    struct heap_element* next_elem = add_value_to_heap(heap_for_next, next_entry);
    if (next_elem == NULL) // BY DESIGN SHOULD NEVER HAPPEN
      printf("TRIED TO ADD TO A FULL HEAP\n");
    register_in_queue(monitor->queue, next_elem);
  }
  monitor->count += 1;
  rebalance_rolling_quantile(monitor); // should run a provably deterministic number of times (once?)
  if (!isnan(monitor->interpolation.target_quantile))
    return interpolate_current_rolling_quantile(monitor);
  return monitor->current_value.member;
}

int rebalance_rolling_quantile(struct rolling_quantile* monitor) {
  unsigned left_entries = monitor->left_heap->n_entries;
  unsigned right_entries = monitor->right_heap->n_entries;
  unsigned total_entries = left_entries + right_entries + 1;
  unsigned left_target = (monitor->portion * total_entries) / monitor->window; // builds up gradually when the pipeline is not yet saturated
  if (left_entries == left_target)
    return 0; // if-clauses with lone return statements don't need brackets in my book
  struct heap* overdue_heap = (left_entries < left_target)? monitor->right_heap : monitor->left_heap;
  struct heap_element holdover = monitor->current_value;
  remove_front_element_from_heap(overdue_heap, &monitor->current_value); // take from the correct heap to restore balance. expelled element is transferred into our current slot
  struct heap* other_heap = (overdue_heap == monitor->right_heap)? monitor->left_heap : monitor->right_heap; // is it worth avoiding two separate branches of slightly redundant code?
  if (!isnan(holdover.member)) {
    // this part does not rely on the actual address of `holdover`/`current_value`, thankfully
    add_element_to_heap(other_heap, holdover); // the method knows that `*holdover.loc_in_buffer` is stale after copying
  }
  return rebalance_rolling_quantile(monitor) + 1; // is non-tail-call recursion *always* dangerous? each round performs one set of "remove and add"
}

/*
  Consists of various sanity checks and tests on integrity.
*/
bool verify_monitor(struct rolling_quantile* monitor) {
  double left = view_front_of_heap(monitor->left_heap);
  if (!isnan(left) && (left > monitor->current_value.member))
   return false;
  double right = view_front_of_heap(monitor->right_heap);
  if (!isnan(right) && (right < monitor->current_value.member))
    return false;
  return verify_heap(monitor->left_heap) && verify_heap(monitor->right_heap);
}
