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

struct rolling_quantile create_rolling_quantile_monitor(unsigned window, unsigned portion) {
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
  };
  return monitor;
}

void destroy_rolling_quantile_monitor(struct rolling_quantile* monitor) {
  destroy_heap(monitor->left_heap);
  destroy_heap(monitor->right_heap);
  destroy_queue(monitor->queue);
}

/*
  Game plan.
    We shall first expel the stale entry, then add the new entry to its rightful receptacle based on its ordering wrt the current value.
    If a NaN is added, we will simply count it as a cycle without a new observation: old will be expelled with no replenishing.
    If the whole window empties, the current_value will assume its last valid value. It will never revert to NaN as it was initialized.
*/
double update_rolling_quantile(struct rolling_quantile* monitor, double next_entry) {
  unsigned left_entries = monitor->left_heap->n_entries;
  unsigned right_entries = monitor->right_heap->n_entries;
  unsigned total_entries = left_entries + right_entries + 1;
  if (isnan(monitor->current_value.member)) { // total_entries will be 1 regardless of whether current_value has anything in it. we want to be careful, since NaNs will also signal missing values coming in
    monitor->current_value.member = next_entry;
    register_in_queue(monitor->queue, &monitor->current_value);
    monitor->count += 1;
    return next_entry;
  }
  if ((monitor->count >= monitor->window) && (total_entries > 1)) {
    bool expired_in_heap = expire_stale_entry_in_queue(monitor->queue, 2, monitor->left_heap, monitor->right_heap);
    if (!expired_in_heap) {
      struct heap* some_heap = (right_entries > 0)? monitor->right_heap : monitor->left_heap; // pick arbitrarily
      struct heap_element substitute_elem = remove_front_element_from_heap(some_heap);
      monitor->current_value = substitute_elem;
    }
  }
  if (!isnan(next_entry)) {
    struct heap* heap_for_next = (next_entry > monitor->current_value.member)? monitor->right_heap : monitor->left_heap;
    struct heap_element* next_elem = add_value_to_heap(heap_for_next, next_entry);
    register_in_queue(monitor->queue, next_elem);
  }
  monitor->count += 1;
  rebalance_rolling_quantile(monitor); // should run a provably deterministic number of times (once?)
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
  struct heap_element expelled_elem = remove_front_element_from_heap(overdue_heap); // take from the correct heap to restore balance.
  struct heap* other_heap = (overdue_heap == monitor->right_heap)? monitor->left_heap : monitor->right_heap; // is it worth avoiding two separate branches of slightly redundant code?
  if (!isnan(monitor->current_value.member)) {
    add_element_to_heap(other_heap, monitor->current_value);
  }
  monitor->current_value = expelled_elem;
  return rebalance_rolling_quantile(monitor) + 1; // is non-tail-call recursion *always* dangerous? each round performs one set of "remove and add"
}

bool verify_monitor(struct rolling_quantile* monitor) {
  return verify_heap(monitor->left_heap) && verify_heap(monitor->right_heap);
}
