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

#include <stdlib.h>
#include <tgmath.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

struct ring_buffer* create_queue(unsigned size) {
  unsigned buffer_size = size * sizeof(ring_buffer_elem);
  struct ring_buffer* buffer = malloc(sizeof(struct ring_buffer) + buffer_size);
  buffer->size = size;
  buffer->n_entries = 0;
  buffer->head = &buffer->entries[0]; // slight semantic (NOT teleological) distinction between this and `buffer->entries`
  memset(buffer->entries, 0, buffer_size);
  return buffer;
}

struct heap* create_heap(enum heap_mode mode, unsigned size, struct ring_buffer* queue) {
  unsigned n_entries = size; // not necessarily trivial
  struct heap* data = calloc(1, sizeof(struct heap) + n_entries*sizeof(struct heap_element)); // calloc in order to ensure our elements are zeroed out
  data->mode = mode;
  data->size = size;
  data->queue = queue;
  return data;
}

void destroy_queue(struct ring_buffer* queue) {
  free(queue);
}

void destroy_heap(struct heap* heap) {
  free(heap);
}

bool is_ring_buffer_full(struct ring_buffer* buffer) {
  return buffer->n_entries == buffer->size;
}

bool is_ring_buffer_empty(struct ring_buffer* buffer) {
  return buffer->n_entries == 0;
}

void advance_ring_buffer(struct ring_buffer* buffer) {
  buffer->head++;
  if (buffer->head == (buffer->entries + buffer->size)) {
    buffer->head = buffer->entries;
  }
}

static // buffer does not have to be full as long as it isn't empty, but it might return NULL if it isn't full
ring_buffer_elem extract_oldest_entry_from_ring_buffer(struct ring_buffer* buffer) { // removes the entry too, and increments the head
  struct heap_element* entry = *buffer->head;
  *buffer->head = NULL;
  return entry;
}

static
ring_buffer_elem* get_next_position_in_ring_buffer(struct ring_buffer* buffer) {
  return buffer->head;
}

static
void xor_swap(void* a, void* b, size_t size) { // overkill hahah. do it byte by byte so that we are data-type agnostic
  char* a_bytes = a;
  char* b_bytes = b;
  for (size_t i = 0; i < size; i += 1) {
    a_bytes[i] ^= b_bytes[i];
    b_bytes[i] ^= a_bytes[i];
    a_bytes[i] ^= b_bytes[i];
  }
}

static
void plain_swap(void* a, void* b, size_t size) {
  char* a_bytes = a;
  char* b_bytes = b;
  for (size_t i = 0; i < size; i += 1) {
    char c = a_bytes[i];
    a_bytes[i] = b_bytes[i];
    b_bytes[i] = c;
  }
}

static
void swap(void* a, void* b, size_t size) {
  plain_swap(a, b, size);
}

static
void swap_elements_in_heap(struct heap_element* a, struct heap_element* b) {
  swap(&a->member, &b->member, sizeof(double));
  /*if (a->loc_in_buffer && b->loc_in_buffer) { swap with actual addresses, rather than queue entries
    swap(a->loc_in_buffer, b->loc_in_buffer, sizeof(struct heap_element*));
  } else*/
  if (a->loc_in_buffer) {
    *a->loc_in_buffer = b;
  }
  if (b->loc_in_buffer) {
    *b->loc_in_buffer = a;
  }
  swap(&a->loc_in_buffer, &b->loc_in_buffer, sizeof(struct heap_element**));
}

static
void trickle_down(struct heap* heap, unsigned i) { // conscious of the tags in the queue that may be invalidated
  struct heap_element* node         = heap->elements + i;
  struct heap_element* first_child  = heap->elements + (2*i + 1);
  struct heap_element* second_child = heap->elements + (2*i + 2);
  struct heap_element* limit = heap->elements + heap->n_entries;
  if (heap->mode == MAX_HEAP) {
    if (first_child >= limit) {
      if (second_child >= limit)
        return;
      if (node->member < second_child->member)
        swap_elements_in_heap(second_child, node);
      return;
    }
    if (second_child >= limit) {
      if (node->member < first_child->member)
        swap_elements_in_heap(first_child, node);
      return;
    }
    if ((node->member > first_child->member) && (node->member > second_child->member))
      return;
    if (first_child->member > second_child->member) {
      swap_elements_in_heap(first_child, node);
      trickle_down(heap, 2*i + 1);
    } else {
      swap_elements_in_heap(second_child, node);
      trickle_down(heap, 2*i + 2); // tail-call optimized
    }
  } else if (heap->mode == MIN_HEAP) {
    if (first_child >= limit) { // redundant, since the first child is always right before the second child.
      if (second_child >= limit)
        return;
      if (node->member > second_child->member)
        swap_elements_in_heap(second_child, node);
      return;
    }
    if (second_child >= limit) {
      if (node->member > first_child->member)
        swap_elements_in_heap(first_child, node);
      return;
    }
    if ((node->member < first_child->member) && (node->member < second_child->member))
      return;
    if (first_child->member < second_child->member) {
      swap_elements_in_heap(first_child, node);
      trickle_down(heap, 2*i + 1);
    } else {
      swap_elements_in_heap(second_child, node);
      trickle_down(heap, 2*i + 2);
    }
  }
}

static
unsigned trickle_up(struct heap* heap, unsigned i) {
  if (i == 0) return 0;
  unsigned pos = i;
  unsigned parent_index = (i-1) / 2;
  struct heap_element* node   = &heap->elements[i];
  struct heap_element* parent = &heap->elements[parent_index]; // division should be efficient
  if (heap->mode == MAX_HEAP) {
    if (node->member > parent->member) {
      swap_elements_in_heap(parent, node);
      return trickle_up(heap, parent_index);
    }
  } else if (heap->mode == MIN_HEAP) {
    if (node->member < parent->member) {
      swap_elements_in_heap(parent, node);
      return trickle_up(heap, parent_index);
    }
  }
  return pos;
}

bool belongs_to_this_heap(struct heap* heap, struct heap_element* elem) { // when we come from a queue connected to many heaps, we need to locate the heap that contains each element
  return (elem >= heap->elements) && (elem < (heap->elements + heap->n_entries));
}

void remove_front_element_from_heap(struct heap* heap, struct heap_element* dest) { // the circular queue still maintains its order, and simply skips over the entries that have already been extracted when it's their time to expire
  if (heap->n_entries == 0) {
    *dest = (struct heap_element) { .member = NAN, .loc_in_buffer = NULL };
    return;
  }
  struct heap_element* last_node = heap->elements + heap->n_entries - 1;
  struct heap_element* root_node = heap->elements;
  //struct heap_element extremum = *root_node;
  swap_elements_in_heap(root_node, last_node);
  heap->n_entries -= 1;
  trickle_down(heap, 0);
  //*extremum.loc_in_buffer = NULL; // clear our entry in the queue so that it doesn't mess up the guy that takes our address. keep track of loc_in_buffer so that it can be updated later.
  swap_elements_in_heap(last_node, dest); // `last_node` cannot be affected by the trickler
  if (last_node->loc_in_buffer != NULL) {
    // since `swap_elements_in_heap` is quite aggressive with restoring previously-null queue entries, clear this out for the case that it is never brought back
    // honestly, the (teleological! post hoc?) reasoning behind this line is a little confusing
    *last_node->loc_in_buffer = NULL;
  }
}

double view_front_of_heap(struct heap* heap) {
  if (heap->n_entries == 0)
    return NAN;
  return heap->elements[0].member;
}

struct heap_element* add_value_to_heap(struct heap* heap, double value) {
  // note: cannot swap into this local variable, even though its own `loc_in_buffer` is empty
  struct heap_element new_entry = {
    .member = value,
    .loc_in_buffer = NULL };
  return add_element_to_heap(heap, new_entry);
}

// there is a shortcut path for inserting and then immediately extracting. Consider implementing that as a special case.
struct heap_element* add_element_to_heap(struct heap* heap, struct heap_element new_elem) { // returns new heap element, not -> if it popped its oldest member in order to make space, then it returns heap_element with loc_in_buffer repurposed to act like an optional value's flag
  if (heap->n_entries == heap->size)
    return NULL; // (struct heap_element) { .member = NAN, .loc_in_buffer = NULL };
  unsigned index_to_place = heap->n_entries;
  heap->elements[index_to_place] = new_elem;
  if (new_elem.loc_in_buffer != NULL) { // if this element was taken from a different heap, rectify its stale pointer. do it before trickling up, so that the correct pointer is propagated
    *new_elem.loc_in_buffer = heap->elements + index_to_place;
  }
  heap->n_entries += 1;
  unsigned index_placed = trickle_up(heap, index_to_place);
  return heap->elements + index_placed;
}

void register_in_queue(struct ring_buffer* queue, struct heap_element* elem) {
  queue->n_entries += 1;
  elem->loc_in_buffer = get_next_position_in_ring_buffer(queue);
  *elem->loc_in_buffer = elem;
}

/*
  Return value.
  -> if -1, the queue was already empty
  -> if 0, the expired entry did not belong to a heap
  -> if positive, then the index of the expired entry's heap (1-based)
 */
int expire_stale_entry_in_queue(struct ring_buffer* queue, unsigned n_heaps, ...) {
  //if (!is_ring_buffer_full(queue)) drastic change of behavior since this...
  //  return true;
  if (is_ring_buffer_empty(queue))
    return -1;
  struct heap_element* oldest_elem = extract_oldest_entry_from_ring_buffer(queue);
  if (oldest_elem == NULL)
    return -1;
  if (queue->n_entries > 0) { // this better not happen, but have a safeguard just in case...
    queue->n_entries -= 1;
  }
  va_list heaps;
  va_start(heaps, n_heaps);
  unsigned i;
  for (i = 0; i < n_heaps; i += 1) {
    struct heap* heap = va_arg(heaps, struct heap*);
    if (!belongs_to_this_heap(heap, oldest_elem))
      continue;
    //*oldest_elem->loc_in_buffer = NULL; // signal that it's already been removed. since we already advanced the buffer, we may not have to do this in practice.
    struct heap_element* last_elem = heap->elements + heap->n_entries - 1;
    heap->n_entries -= 1;
    if (last_elem != oldest_elem) {
      double oldest_value = oldest_elem->member;
      double last_value = last_elem->member;
      *oldest_elem = *last_elem; // last_entry will stay in the queue after another entry is added, since oldest_entry will be thrown instead. no need to void last_entry since we'll immediately add a new one on top of it
      *last_elem->loc_in_buffer = oldest_elem; // in the end, we are swapping without care for the ultimate contents of the old last_elem
      unsigned index_of_oldest = oldest_elem - heap->elements;
      if ((heap->mode == MIN_HEAP && oldest_value < last_value) ||
          (heap->mode == MAX_HEAP && oldest_value > last_value)) {
        trickle_down(heap, index_of_oldest); // we moved the last guy on top of the oldest, so we may have to trickle it down again
      } else {
        trickle_up(heap, index_of_oldest); // DID THIS: can I somehow avoid having to do both? as it is, the last element that got transplanted may have to go up or down depending on which parent it lands. I know! If this really becomes an issue, I may compare this element to the previous occupant to know which direction it should take.
      }
    }
    break;
  }
  va_end(heaps);
  if (i < n_heaps) { // did we locate an owner heap?
    return (int)(i + 1);
  } else {
    return 0;
  }
}

bool verify_heap(struct heap* heap) {
  for (unsigned i = 0; i < heap->n_entries; i += 1) {
    unsigned left_child = 2*i + 1;
    unsigned right_child = 2*i + 2;
    if (heap->mode == MAX_HEAP &&
       ((left_child < heap->n_entries && heap->elements[i].member < heap->elements[left_child].member) ||
       (right_child < heap->n_entries && heap->elements[i].member < heap->elements[right_child].member))) {
      return false;
    }
    if (heap->mode == MIN_HEAP &&
       ((left_child < heap->n_entries && heap->elements[i].member > heap->elements[left_child].member) ||
       (right_child < heap->n_entries && heap->elements[i].member > heap->elements[right_child].member))) {
      return false;
    }
  }
  return true;
}
