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

#ifndef HEAP_H
#define HEAP_H

#include <stddef.h> // no need to hassle over the myriad of different data types provided here, which seem to matter most in the stylized abstract world of the C standard
#include <stdbool.h>

enum heap_mode {
  MAX_HEAP, MIN_HEAP
};

struct heap_element {
  double member;
  struct heap_element** loc_in_buffer; // element is marked as nonexistent when this is set to null
};

typedef struct heap_element* ring_buffer_elem;

struct ring_buffer {
  unsigned size; // could've called this the capacity
  unsigned n_entries;
  ring_buffer_elem* head;
  ring_buffer_elem entries[]; // the alternative would be preprocessor magic with fixed sizes, but I don't think that gives us much benefit for the cost it bears.
};

struct heap { // I like this simple naming scheme best.
  enum heap_mode mode;
  unsigned size;
  unsigned n_entries; // multiple heaps may share a queue, so we need to maintain our own set of counting statistics
  struct ring_buffer* queue; // sadly, this must be a pointer in order to remain standard C because ring_buffer is also variably sized.
  struct heap_element elements[]; // keep all data in one contiguous block---one less layer of indirection (funny grammer, since we would otherwise say "fewer layers")
};


// Let's see how rusty my C(++) is. This shall take advantage of the most elegant parts of C17 (i.e. C11.) Feels nice to get back into the groove!
// Const-correctness is a pain in the ass. Instead, I shall trust myself to properly use my interfaces.

bool belongs_to_this_heap(struct heap* heap, struct heap_element* elem);
struct heap_element* add_value_to_heap(struct heap* heap, double value);
struct heap_element* add_element_to_heap(struct heap* heap, struct heap_element new_elem); // this and the below should not remove from the conveyor-belt queue, since adding it back would cause it to lose its original position.
struct heap_element remove_front_element_from_heap(struct heap* heap); // returns by value to signal transfer of ownership. all these methods exposed gives granular control to the operator
void register_in_queue(struct ring_buffer* queue, struct heap_element* elem); // modifies element to point to a fresh spot on the queue. will expire on its own after some time.
bool expire_stale_entry_in_queue(struct ring_buffer* queue, unsigned n_heaps, ...); // pass pointers to all of the heaps attached to this queue
struct ring_buffer* create_queue(unsigned size);
struct heap* create_heap(enum heap_mode mode, unsigned size, struct ring_buffer* queue);
bool verify_heap(struct heap* heap);
void destroy_queue(struct ring_buffer* queue);
void destroy_heap(struct heap* heap);

#endif
