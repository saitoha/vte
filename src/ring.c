/*
 * Copyright (C) 2002 Red Hat, Inc.
 *
 * This is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include "debug.h"
#include "ring.h"

static void
_vte_free_row_data(VteRowData *row)
{
	if (!row)
		return;
	g_array_free(row->cells, TRUE);
	g_slice_free(VteRowData, row);
}

#ifdef VTE_DEBUG
static void
_vte_ring_validate(VteRing * ring)
{
	long i, max;
	g_assert(ring != NULL);
	g_assert(ring->length <= ring->max);
	max = ring->delta + ring->length;
	for (i = ring->delta; i < max; i++) {
		g_assert(_vte_ring_contains(ring, i));
		g_assert(ring->array[i % ring->max] != NULL);
	}
}
#else
#define _vte_ring_validate(ring) G_STMT_START {} G_STMT_END
#endif

/**
 * _vte_ring_new:
 * @max_elements: the maximum size the new ring will be allowed to reach
 * @free_func: a #VteRingFreeFunc
 * @data: user data for @free
 *
 * Allocates a new ring capable of holding up to @max_elements elements at a
 * time, using @free to free them when they are removed from the ring.  The
 * @data pointer is passed to the @free callback whenever it is called.
 *
 * Returns: a new ring
 */
VteRing *
_vte_ring_new(glong max_elements)
{
	VteRing *ret = g_slice_new0(VteRing);
	ret->cached_item = -1;
	ret->max = MAX(max_elements, 2);
	ret->array = g_malloc0(sizeof(gpointer) * ret->max);
	return ret;
}

VteRing *
_vte_ring_new_with_delta(glong max_elements, glong delta)
{
	VteRing *ret;
	ret = _vte_ring_new(max_elements);
	ret->delta = delta;
	return ret;
}

/**
 * _vte_ring_insert:
 * @ring: a #VteRing
 * @position: an index
 * @data: the new item
 *
 * Inserts a new item (@data) into @ring at the @position'th offset.  If @ring
 * already has an item stored at the desired location, it will be freed before
 * being replaced by the new @data.
 *
 */
void
_vte_ring_insert(VteRing * ring, long position, gpointer data)
{
	long point, i;

	g_return_if_fail(ring != NULL);
	g_return_if_fail(position >= ring->delta);
	g_return_if_fail(position <= ring->delta + ring->length);
	g_return_if_fail(data != NULL);

	_vte_debug_print(VTE_DEBUG_RING,
			"Inserting at position %ld.\n"
			" Delta = %ld, Length = %ld, Max = %ld.\n",
			position, ring->delta, ring->length, ring->max);
	_vte_ring_validate(ring);

	/* Initial insertion, or append. */
	if (position == ring->length + ring->delta) {
		/* If there was something there before, free it. */
		_vte_free_row_data (ring->array[position % ring->max]);
		/* Set the new item, and if the buffer wasn't "full", increase
		 * our idea of how big it is, otherwise increase the delta so
		 * that this becomes the "last" item and the previous item
		 * scrolls off the *top*. */
		ring->array[position % ring->max] = data;
		if (ring->length == ring->max) {
			ring->delta++;
			if (ring->delta > ring->cached_item) {
				_vte_ring_set_cache (ring, -1, NULL);
			}

		} else {
			ring->length++;
		}
		_vte_debug_print(VTE_DEBUG_RING,
				" Delta = %ld, Length = %ld, "
				"Max = %ld.\n",
				ring->delta, ring->length, ring->max);
		_vte_ring_validate(ring);
		return;
	}

	if (position <= ring->cached_item) {
		_vte_ring_set_cache (ring, -1, NULL);
	}

	/* All other cases.  Calculate the location where the last "item" in the
	 * buffer is going to end up in the array. */
	point = ring->delta + ring->length - 1;
	while (point < 0) {
		point += ring->max;
	}

	if (ring->length == ring->max) {
		/* If the buffer's full, then the last item will have to be
		 * "lost" to make room for the new item so that the buffer
		 * doesn't grow (here we scroll off the *bottom*). */
		_vte_free_row_data (ring->array[point % ring->max]);
	} else {
		/* We don't want to discard the last item. */
		point++;
	}

	/* We need to bubble the remaining valid elements down.  This isn't as
	 * slow as you probably think it is due to the pattern of usage. */
	for (i = point; i > position; i--) {
		ring->array[i % ring->max] = ring->array[(i - 1) % ring->max];
	}

	/* Store the new item and bump up the length, unless we've hit the
	 * maximum length already. */
	ring->array[position % ring->max] = data;
	ring->length = CLAMP(ring->length + 1, 0, ring->max);
	_vte_debug_print(VTE_DEBUG_RING,
			" Delta = %ld, Length = %ld, Max = %ld.\n",
			ring->delta, ring->length, ring->max);
	_vte_ring_validate(ring);
}

/**
 * _vte_ring_insert_preserve:
 * @ring: a #VteRing
 * @position: an index
 * @data: the new item
 *
 * Inserts a new item (@data) into @ring at the @position'th offset.  If @ring
 * already has an item stored at the desired location, it (and any successive
 * items) will be moved down, items that need to be removed will be removed
 * from the *top*.
 *
 */
void
_vte_ring_insert_preserve(VteRing * ring, long position, gpointer data)
{
	long point, i;
	VteRowData **tmp;
	VteRowData *stack_tmp[128];

	g_return_if_fail(position <= _vte_ring_next(ring));

	_vte_debug_print(VTE_DEBUG_RING,
			"Inserting+ at position %ld.\n"
			" Delta = %ld, Length = %ld, Max = %ld.\n",
			position, ring->delta, ring->length, ring->max);
	_vte_ring_validate(ring);

	if (position <= ring->cached_item) {
		_vte_ring_set_cache (ring, -1, NULL);
	}

	/* Allocate space to save existing elements. */
	point = _vte_ring_next(ring);
	i = MAX(1, point - position);

	/* Save existing elements. */
	tmp = stack_tmp;
	if ((guint) i > G_N_ELEMENTS (stack_tmp))
		tmp = g_new0(VteRowData *, i);
	for (i = position; i < point; i++) {
		tmp[i - position] = _vte_ring_index(ring, i);
	}

	/* Remove the existing elements. */
	for (i = point; i > position; i--) {
		_vte_ring_remove(ring, i - 1, FALSE);
	}

	/* Append the new item. */
	_vte_ring_append(ring, data);

	/* Append the old items. */
	for (i = position; i < point; i++) {
		_vte_ring_append(ring, tmp[i - position]);
	}

	/* Clean up. */
	if (tmp != stack_tmp)
		g_free(tmp);
}

/**
 * _vte_ring_remove:
 * @ring: a #VteRing
 * @position: an index
 * @free_element: %TRUE if the item should be freed
 *
 * Removes the @position'th item from @ring, freeing it only if @free_element is
 * %TRUE.
 *
 */
void
_vte_ring_remove(VteRing * ring, long position, gboolean free_element)
{
	long i;
	_vte_debug_print(VTE_DEBUG_RING,
			"Removing item at position %ld.\n"
			" Delta = %ld, Length = %ld, Max = %ld.\n",
			position, ring->delta, ring->length, ring->max);
	_vte_ring_validate(ring);

	if (position <= ring->cached_item) {
		_vte_ring_set_cache (ring, -1, NULL);
	}

	i = position % ring->max;
	/* Remove the data at this position. */
	if (free_element)
		_vte_free_row_data (ring->array[position % ring->max]);
	ring->array[position % ring->max] = NULL;

	/* Bubble the rest of the buffer up one notch.  This is also less
	 * of a problem than it might appear, again due to usage patterns. */
	for (i = position; i < ring->delta + ring->length - 1; i++) {
		ring->array[i % ring->max] = ring->array[(i + 1) % ring->max];
	}

	/* Store a NULL in the position at the end of the buffer and decrement
	 * its length (got room for one more now). */
	ring->array[(ring->delta + ring->length - 1) % ring->max] = NULL;
	if (ring->length > 0) {
		ring->length--;
	}
	_vte_debug_print(VTE_DEBUG_RING,
			" Delta = %ld, Length = %ld, Max = %ld.\n",
			ring->delta, ring->length, ring->max);
	_vte_ring_validate(ring);
}

/**
 * _vte_ring_append:
 * @ring: a #VteRing
 * @data: the new item
 *
 * Appends a new item to the ring.  If an item must be removed to make room for
 * the new item, it is freed.
 *
 */
void
_vte_ring_append(VteRing * ring, gpointer data)
{
	g_assert(data != NULL);
	_vte_ring_insert(ring, ring->delta + ring->length, data);
}

/**
 * _vte_ring_free:
 * @ring: a #VteRing
 * @free_elements: %TRUE if items in the ring should be freed
 *
 * Frees the ring and, optionally, each of the items it contains.
 *
 */
void
_vte_ring_free(VteRing *ring, gboolean free_elements)
{
	long i;
	if (free_elements) {
		for (i = 0; i < ring->max; i++) {
			/* Remove this item. */
			_vte_free_row_data (ring->array[i]);
		}
	}
	g_free(ring->array);
	g_slice_free(VteRing, ring);
}
