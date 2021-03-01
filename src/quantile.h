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

#ifndef QUANTILE_H
#define QUANTILE_H

#include "heap.h"

#include <stdbool.h>

/*
  Composable (in pipelines/chains) rolling quantiles of arbitrary time scales.
*/

// Can't hide this structure's implementation in quantile.c because we want to be able to handle it by value. Comprise other structures of it without having many layers of indirection.
struct rolling_quantile {
  struct heap_element current_value;
  unsigned window;
  unsigned portion;
  struct ring_buffer* queue;
  struct heap* left_heap;
  struct heap* right_heap;
  unsigned count;
};

struct rolling_quantile create_rolling_quantile_monitor(unsigned window, unsigned portion); // window should be an odd number. portion is how much probability mass goes to the left side, so (portion+0.5)/window gives the quantile.
double update_rolling_quantile(struct rolling_quantile* monitor, double entry);
int rebalance_rolling_quantile(struct rolling_quantile* monitor); // returns the number of sifts and shifts it had to perform
bool verify_monitor(struct rolling_quantile* monitor);
void destroy_rolling_quantile_monitor(struct rolling_quantile* monitor);

#endif
