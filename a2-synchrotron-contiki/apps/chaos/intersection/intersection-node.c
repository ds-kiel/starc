/*******************************************************************************
 * BSD 3-Clause License
 *
 * Copyright (c) 2017 Beshr Al Nahas and Olaf Landsiedel.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************/
/**
 * \file
 *         A2-Synchrotron two-phase commit application for intersections
 * \author
 *         Beshr Al Nahas <beshr@chalmers.se>
 *         Olaf Landsiedel <olafl@chalmers.se>
 *         Patrick Rathje <mail@patrickrathje.de>
 *
 */

#include "contiki.h"
#include <stdio.h> /* For printf() */
#include "net/netstack.h"
#include <stdlib.h>

#undef INITIATOR_NODE
#define INITIATOR_NODE 1

#include "chaos-control.h"
#include "node.h"


#define TILES_WIDTH 6
#define TILES_HEIGHT 6
#define NUM_TILES (TILES_WIDTH * TILES_HEIGHT)



#include "merge-commit.h"
#include "random.h"

#define REPEAT 1
#define MOVE_INTERVAL CLOCK_SECOND


typedef struct __attribute__((packed)) {
    uint8_t size;
    uint8_t tiles[NUM_TILES];
} path_t;


static int pos_to_id(int x, int y) {
  return y*TILES_WIDTH+x;
}

static void id_to_pos(int *x, int *y, int id) {
  *x = id%TILES_WIDTH;
  *y = id/TILES_WIDTH;
}

static void extract_path(path_t* dest, merge_commit_value_t *src, int node_id) {
  int i = 0;
  dest->size = 0;

  for(i = 0; i < NUM_TILES; ++i) {
    if (src->tile_reservations[i] == node_id) {
      // add to array
      dest->tiles[dest->size] = i;
      dest->size++;
    }
  }
}


static void reserve_path(merge_commit_value_t *val, path_t* path, int node_id) {
  int i;
  for(i = 0; i < path->size; ++i) {
    val->tile_reservations[path->tiles[i]] = node_id;
  }
}

static int path_is_reserved(merge_commit_value_t* plan, path_t *path, int node_id) {

  if (path->size == 0) {
    return 0;
  }

  int i = 0;

  for(i = 0; i < path->size; ++i) {
    int x = plan->tile_reservations[path->tiles[i]];
    if (x != node_id) {
      return 0; // Mismatch
    }
  }

  return 1;
}

static int path_available(merge_commit_value_t *plan, path_t* path, int node_id) {
  int i;
  for(i = 0; i < path->size; ++i) {
    int x = plan->tile_reservations[path->tiles[i]];
    if (x != 0 && x != node_id) {
      return 0;
    }
  }
  return 1;
}


static int px = 0;
static int py = 0;
static int dx = 0;
static int dy = 0;

static int opx = 0;
static int opy = 0;

static int accepted = 0;


static path_t own_reservation;


void print_tiles(merge_commit_value_t *val) {
  int x = 0;
  int y = 0;

  for(y = 0; y < TILES_HEIGHT; y++) {
    for(x = 0; x < TILES_WIDTH; x++) {
      printf("%d", val->tile_reservations[y*TILES_WIDTH+x]);
    }
    printf(", ");
  }
}

unsigned short rseed = 0; /* Will be set to a value by cooja! */
static void init_pos_and_dir() {


  int offset = (node_id-1)%3;

  switch((node_id-1)/3) {
    case 0:
      px = -1; py = 3+offset;
      dx = 1; dy = 0;
      break;
    case 1:
      px = 3+offset; py = 6;
      dx = 0; dy = -1;
      break;
    case 2:
      px = 6; py = 2-offset;
      dx = -1; dy = 0;
      break;
    case 3:
      px = 2-offset; py = -1;
      dx = 0; dy = 1;
      break;
  }
}

static void init_reservation() {
  own_reservation.size = 0;
  int i;
  // we initialize reservations here
  for(i = 0; i < TILES_WIDTH; ++i) {
    own_reservation.tiles[own_reservation.size] = pos_to_id(px + dx * (i+1), py + dy * (i+1));
    own_reservation.size++;
  }
}

static void mc_round_begin(const uint16_t round_count, const uint8_t id);


CHAOS_APP(chaos_merge_commit_app, MERGE_COMMIT_SLOT_LEN, MERGE_COMMIT_ROUND_MAX_SLOTS, 1, merge_commit_is_pending, mc_round_begin);

#if NETSTACK_CONF_WITH_CHAOS_NODE_DYNAMIC
#include "join.h"
CHAOS_APPS(&join, &chaos_merge_commit_app);
#else
CHAOS_APPS(&chaos_merge_commit_app);
#endif /* NETSTACK_CONF_WITH_CHAOS_NODE_DYNAMIC */

/* Commit variables */
static merge_commit_value_t mc_value;
static merge_commit_value_t mc_commited_value;
static merge_commit_value_t mc_last_commited_value;
static uint8_t* mc_flags;
static uint8_t  mc_complete = 0, mc_phase = 0;
static uint16_t mc_off_slot;
static uint16_t mc_round_count_local = 0;


// TODO: check type of node id...



PROCESS(mc_process, "Merge-Commit process");
PROCESS_THREAD(mc_process, ev, data)
{
  // TODO: init commit value
PROCESS_BEGIN();
  while(1) {
    PROCESS_YIELD();

    if(chaos_has_node_index){
      if (mc_phase == PHASE_COMMIT) {
        printf("Commit completed\n");
        printf("OFFSLOT: %d\n", mc_off_slot);

        // Set latest known commit value
        memcpy(&mc_last_commited_value, &mc_commited_value, sizeof(merge_commit_value_t));

        if (path_is_reserved(&mc_commited_value, &own_reservation, node_id)) {
          print_tiles(&mc_commited_value);
          printf("Node id %d was accepted\n", node_id);
          accepted = 1;
        } else if (own_reservation.size > 0 && !path_is_reserved(&mc_value, &own_reservation, node_id)){
          // TODO: At this point, we really just want to find ANY way ;)
          // Add this with a parameter
          if (path_available(&mc_last_commited_value, &own_reservation, node_id)) {
            reserve_path(&mc_value, &own_reservation, node_id);
          }
        }
      } else {
        printf("Commit NOT completed\n");
      }
    } else {
      printf("{rd %u res} 2pc: waiting to join, n: %u\n", mc_round_count_local, chaos_node_count);
    }
    // COMMIT HAS FINISHED!
    // CHECK IF IT WAS SUCCESSFUL!
  }
PROCESS_END();
}



PROCESS(movement_process, "movement process");

PROCESS_THREAD(movement_process, ev, data)
{

  static struct etimer timer;
  PROCESS_BEGIN();


  // first set the position based on our mode id and move to the position
  init_pos_and_dir();
  init_reservation();
  opx = px;
  opy = py;

  memset(&mc_last_commited_value, 0, sizeof(merge_commit_value_t));

  etimer_set(&timer, MOVE_INTERVAL);
  do {
    // Reset original position
    px = opx;
    py = opy;
    printf("#MoveTo %d.0 %d.0\n", px, py);

    own_reservation.size = TILES_WIDTH;

    // TODO: Add multiple directions
    // Add this with a parameter

    // TODO: At this point, we really just want to find ANY way ;)
    // Add this with a parameter
    if (path_available(&mc_last_commited_value, &own_reservation, node_id)) {
      reserve_path(&mc_value, &own_reservation, node_id);
    }

    // insert extra wait_for_request time
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer)); etimer_reset(&timer);

    while (1) {
      if (accepted) {
        printf("#Move %d.0 %d.0\n", dx, dy);
        px += dx;
        py += dy;

        // TODO: We want to free the tiles, that we have already visited!
        // Add this with a parameter

        // check if we should release the reservation
        if (abs(opx-px) + abs(opy-py) > TILES_WIDTH /*TODO*/) {
          accepted = 0;

          // Release
          own_reservation.size = 0;
          // Update collect value
          memset(&mc_value, 0, sizeof(merge_commit_value_t));
          break;
        }
      }

      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer)); etimer_reset(&timer);
    }

    // some extra waiting time ;)
    static int random_waits = 0;

    etimer_restart(&timer);
    random_waits = abs((random_rand() % 10))+5;

    for(; random_waits > 0; random_waits--) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer)); etimer_reset(&timer);
    }

  } while(REPEAT);

  PROCESS_END();
}


PROCESS(main_process, "Main intersection process");
AUTOSTART_PROCESSES(&main_process);
PROCESS_THREAD(main_process, ev, data)
{
  PROCESS_BEGIN();
  NETSTACK_MAC.on();


  if (rseed == 0) {
    rseed = node_id;
  }
  random_init(rseed);
  printf("Starting main process with seed: %d\n", rseed);

  process_start(&movement_process, NULL);
  process_start(&mc_process, NULL);
  PROCESS_YIELD();
  PROCESS_END();
}

static void mc_round_begin(const uint16_t round_count, const uint8_t id){
  memcpy(&mc_commited_value, &mc_value, sizeof(merge_commit_value_t));
  mc_complete = merge_commit_round_begin(round_count, id, &mc_commited_value, &mc_phase, &mc_flags);
  mc_off_slot = merge_commit_get_off_slot();
  mc_round_count_local = round_count;
  process_poll(&mc_process);
}


void merge_commit_merge_callback(merge_commit_t* rx_mc, merge_commit_t* tx_mc) {

  int i = 0;
  int c = 0;
  merge_commit_value_t new;
  memset(&new, 0, sizeof(merge_commit_value_t));

  for(i = 1; i <= chaos_node_count; ++i) {

    path_t path;
    extract_path(&path, &rx_mc->value, i);

    merge_commit_value_t *plans[2];
    plans[0] = &tx_mc->value;
    plans[1] = &rx_mc->value;

    // TODO: We might want to try to add another route for ourselfs here
    // Add with a parameter

    c = 0;
    while(c < (sizeof(plans) / sizeof(merge_commit_t *))) {
      extract_path(&path, plans[c], i);
      if (path.size > 0 && path_available(&new, &path, i)) {
        reserve_path(&new, &path, i);
        break; // We could reserve our path ;)
      }
      c++;
    }
  }

  // now copy the generated reservations
  memcpy(&tx_mc->value, &new, sizeof(merge_commit_value_t));
}