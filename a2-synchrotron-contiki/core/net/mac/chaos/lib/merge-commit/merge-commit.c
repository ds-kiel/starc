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
/*
 * \file
 *         Merge commit library
 * \author
 *         Beshr Al Nahas <beshr@chalmers.se>
 *         Olaf Landsiedel <olafl@chalmers.se>
 *         Valentin Poirot <poirotv@chalmers.se>
 *         Patrick Rathje <mail@patrickrathje.de>
 */

#include "contiki.h"
#include <string.h>

#include "chaos.h"
#include "chaos-random-generator.h"
#include "node.h"
#include "merge-commit.h"
#include "chaos-config.h"

#ifndef FAILURES_RATE
#define FAILURES_RATE 0
#endif

#undef ENABLE_COOJA_DEBUG
#define ENABLE_COOJA_DEBUG COOJA
#include "dev/cooja-debug.h"

/* Continue final flood until we receive a full packet? */
#define RELIABLE_FF 1

#ifndef N_TX_COMPLETE
#define N_TX_COMPLETE 9
#endif

#ifndef CHAOS_RESTART_MIN
#define CHAOS_RESTART_MIN 6
#endif

#ifndef CHAOS_RESTART_MAX
#define CHAOS_RESTART_MAX 10
#endif

#define FLAGS_LEN_X(X)   (((X) >> 3) + (((X) & 7) ? 1 : 0))
#define FLAGS_LEN   (FLAGS_LEN_X(MAX_NODE_COUNT))

#if NETSTACK_CONF_WITH_CHAOS_NODE_DYNAMIC
#define FLAGS_ESTIMATE FLAGS_LEN_X(MAX_NODE_COUNT)
#else
#define FLAGS_ESTIMATE FLAGS_LEN_X(CHAOS_NODES)
#endif


#define ARR_INDEX_X(X) ((X)/8)
#define ARR_OFFSET_X(X) ((X)%8)

#define ARR_INDEX ARR_INDEX_X(chaos_node_index)
#define ARR_OFFSET ARR_OFFSET_X(chaos_node_index)


#define MERGE_COMMIT_MAX_COMMIT_SLOT (MERGE_COMMIT_ROUND_MAX_SLOTS / 3)

#ifndef COMMIT_THRESHOLD
#define COMMIT_THRESHOLD 0
/*((MERGE_COMMIT_MAX_COMMIT_SLOT)/2)*/
#endif


#if MERGE_COMMIT_ADVANCED_STATS

//static uint8_t bit_count(uint8_t u) { return (u - (u >> 1) - (u >> 2) - (u >> 3) - (u >> 4) - (u >> 5) - (u >> 6) - (u >> 7)); }
static uint8_t bit_count(uint8_t x)
{
  const uint8_t one_bits[] = {0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4};
  return one_bits[x&0x0f] + one_bits[x>>4];
}
merge_commit_advanced_slot_stats_t merge_commit_advanced_stats[MERGE_COMMIT_ROUND_MAX_SLOTS];
#endif


typedef struct __attribute__((packed)) {
  merge_commit_t mc;
  uint8_t flags_and_leaves[FLAGS_ESTIMATE*2];
} merge_commit_local_t;


extern void merge_commit_merge_callback(merge_commit_t* rx_mc, merge_commit_t* tx_mc);

// Enable me if maximum is wanted
#if 0
void merge_commit_merge_callback(merge_commit_t* rx_mc, merge_commit_t* tx_mc) {
  int ret = memcmp(&rx_mc->value, &tx_mc->value, sizeof(merge_commit_value_t));

  if (ret < 0) {
    memcpy(&rx_mc->value, &tx_mc->value, sizeof(merge_commit_value_t));
    return 1;
  } else if (ret > 0) {
    memcpy(&tx_mc->value, &rx_mc->value, sizeof(merge_commit_value_t));
    return 1;
  } else {
    return 0; // already equal
  }
}
#endif

static uint8_t did_tx = 0, has_initial_join_masks = 0;
static int complete = 0, off_slot, completion_slot, rx_progress = 0;
static int tx_count_complete = 0;
static int invalid_rx_count = 0;
static int got_valid_rx = 0;
static unsigned short restart_threshold;
static merge_commit_local_t mc_local; /* used only for house keeping and reporting */
static uint8_t* tx_flags_final = 0;
static uint8_t delta_at_slot = 0;
static uint8_t joined, left, rejoin_needed, was_initiator = 0;

static uint8_t join_masks[FLAGS_LEN];
uint8_t merge_commit_wanted_join_state = MERGE_COMMIT_WANTED_JOIN_STATE_LEAVE;
uint8_t merge_commit_wanted_type = TYPE_UNKNOWN;
uint8_t merge_commit_wanted_election_priority = 0;

int merge_commit_get_flags_length() {
  return FLAGS_ESTIMATE;
}
int merge_commit_get_masks_length() {
  return FLAGS_ESTIMATE;
}

int merge_commit_has_joined() { return  joined;}
int merge_commit_has_left() { return left;}

int merge_commit_get_flags_and_leaves_overall_length() {
  return merge_commit_get_flags_length() + merge_commit_get_masks_length();
}

static inline uint8_t* merge_commit_get_flags(merge_commit_t* mc) {
  return mc->flags_and_leaves;
}

static inline uint8_t* merge_commit_get_leaves(merge_commit_t* mc) {
  return mc->flags_and_leaves+FLAGS_ESTIMATE;
}

inline void handle_advanced_stats(merge_commit_t *tx_mc, uint16_t slot_count) {
#if MERGE_COMMIT_ADVANCED_STATS
  {
    merge_commit_advanced_slot_stats_t slot_stats;

    slot_stats.has_node_index = chaos_has_node_index;
    slot_stats.node_index = chaos_node_index;
    slot_stats.node_count = chaos_node_count;
    slot_stats.phase = tx_mc->phase;
    slot_stats.type = tx_mc->type;
    slot_stats.is_initiator = IS_INITIATOR();
    slot_stats.config_msb = (join_get_config() >> 8)&0xFF;
    slot_stats.config_lsb = join_get_config()&0xFF;

    uint8_t* tx_flags = merge_commit_get_flags(tx_mc);
    uint8_t i;
    slot_stats.flag_progress = 0;
    for (i = 0; i < FLAGS_LEN; i++) {
      slot_stats.flag_progress += bit_count(tx_flags[i]);
    }
    // and finally copy the stats to the overall array
    memcpy(&merge_commit_advanced_stats[slot_count], &slot_stats, sizeof(slot_stats));
  }
#endif
}


inline void force_rejoin(uint8_t* tx_flags, uint8_t* tx_leaves) {

  // we think that we are part of the network
  if (chaos_has_node_index) {
    // but it seems like we have missed a commit msg
    // so we reset everything for this round and behave as a forwarder only!

    // if we are the initiator, we reset everything
    // if we are, then probably another initiator has commited without our knowledge
    if (IS_INITIATOR()) {
      chaos_set_is_initiator(0);
    }

    // BUT we have to clear the flags...
    tx_flags[ARR_INDEX] &= ~(1 << ARR_OFFSET);
    tx_leaves[ARR_INDEX] &= ~(1 << ARR_OFFSET);

    chaos_has_node_index = 0;
    chaos_node_index = 0;

    rejoin_needed = 1; // we need to rejoin even if we just want to leave!
  }
  has_initial_join_masks = 0; // we also need to reset our join masks, they might be old
}


inline uint8_t merge_flags(uint8_t* tx_flags, uint8_t* tx_leaves, uint8_t* rx_flags, uint8_t* rx_leaves,
                          uint8_t *flags_complete_ref, uint8_t *rx_complete_ref) {
  int i;
  uint8_t rx_complete = 1;
  uint8_t flags_complete = 1;

  uint8_t tx = 0;

  if (!has_initial_join_masks) {
    // TODO: The join masks might be not valid, if we are not part of the network and thus did not receive the flags in the first phase!
    // So
    for(i = 0; i < FLAGS_LEN; i++) {
      join_masks[i] = (~rx_leaves[i]) | tx_flags[i] | rx_flags[i];
    }
    has_initial_join_masks = 1;
  }


  for(i = 0; i < FLAGS_LEN; i++) {
    tx |= (tx_leaves[i] != rx_leaves[i]) || (tx_flags[i] != rx_flags[i]);

    tx_leaves[i] |= rx_leaves[i];

    tx_flags[i] |= rx_flags[i];

    // we remove the entries in the join mask that have left the network
    // but we update our join mask based on live nodes
    if ((tx_flags[i]&join_masks[i]) != join_masks[i]) {
      flags_complete = 0;
    }
    if ((rx_flags[i]&join_masks[i]) != join_masks[i]) {
      rx_complete = 0;
    }
  }

  *flags_complete_ref = flags_complete;
  *rx_complete_ref = rx_complete;

  return tx;
}


inline uint8_t handle_rejoin(merge_commit_t* tx_mc, merge_commit_t* rx_mc) {
  uint8_t tx = 0;
  if (tx_mc->rejoin_slot != rx_mc->rejoin_slot) {
    tx = 1;
    // we use our own if we have an index NOOP otherwise
    if (!tx_mc->rejoin_slot) {
      tx_mc->rejoin_slot = rx_mc->rejoin_slot;
      tx_mc->rejoin_index = rx_mc->rejoin_index;
    }
  }

  // check if we could rejoin the network
  if(!chaos_has_node_index && tx_mc->rejoin_slot == node_id){
    //printf("Rejoined with index %d\n", tx_mc->rejoin_index);
    chaos_node_index = tx_mc->rejoin_index;
    chaos_has_node_index = 1;
    joined = 1;
    rejoin_needed = 0;
  }
  return tx;
}

inline uint8_t initiator_try_rejoin_node(merge_commit_t *tx_mc, join_data_t* join_data_tx) {
  uint8_t tx = 0;
  int i;

  if (IS_INITIATOR() && !tx_mc->rejoin_slot) {
    // We check if there are any nodes that only want to rejoin! We need to do it before the commit since we are waiting for their flags
    for (i = 0; i < join_data_tx->slot_count; i++) {
      node_id_t n = join_data_tx->slots[i];
      if( n ){
        int chaos_index = join_get_index_for_node_id(n);
        if (chaos_index >= 0) {
          //printf("Rejoined node %d at index %d\n", n, chaos_index);
          tx = 1;
          tx_mc->rejoin_slot = n;
          tx_mc->rejoin_index = chaos_index;
          break;
        }
      }
    }
  }
  return tx;
}

inline uint8_t handle_election_round(uint16_t round_count, uint16_t slot_count, merge_commit_t* tx_mc, merge_commit_t* rx_mc) {

  uint8_t* tx_leaves = merge_commit_get_leaves(tx_mc);
  uint8_t* rx_leaves = merge_commit_get_leaves(rx_mc);

  uint8_t* tx_flags = merge_commit_get_flags(tx_mc);
  uint8_t* rx_flags = merge_commit_get_flags(rx_mc);

  join_data_t* join_data_tx = &tx_mc->join_data;
  join_data_t* join_data_rx = &rx_mc->join_data;

  uint8_t tx = 0;


  // we got a matching config and matching type, nice!
  // we now convert our own round type if needed
  if (tx_mc->type == TYPE_UNKNOWN) {
    // in case of an election, we need to prepare things
    // first we reset everything!
    memset(&tx_mc->election, 0, MAX(sizeof(tx_mc->election), sizeof(tx_mc->value)));
    tx_mc->type = TYPE_ELECTION_AND_HANDOVER;

    if (chaos_has_node_index) {
      tx_mc->election.leader_node_id = node_id;
      tx_mc->election.priority = merge_commit_wanted_election_priority; // it is okay to set this here, because we set it once, wont be changed afterwards
    }
  }

  //be careful: do not mix the different phases
  if (tx_mc->phase == rx_mc->phase) {
    // same phase

    // first calculate the current leaves
    uint8_t rx_complete, flags_complete;
    tx |= merge_flags(tx_flags, tx_leaves, rx_flags, rx_leaves, &flags_complete, &rx_complete);

    if (tx_mc->phase == PHASE_MERGE) {

      tx |= handle_rejoin(tx_mc, rx_mc);

      // Join logic, we need to do it for the rejoin
      uint8_t join_merge_delta = 0;
      tx |= join_merge_data(join_data_tx, join_data_rx, &join_merge_delta);
      if (join_merge_delta) {
        delta_at_slot = slot_count;
      }

      if (tx_mc->election.priority != rx_mc->election.priority) {
        tx = 1;
        if (tx_mc->election.priority < rx_mc->election.priority) {
          tx_mc->election.priority = rx_mc->election.priority;
          tx_mc->election.leader_node_id = rx_mc->election.leader_node_id;
        } // else we keep our value ;)
      } else if (tx_mc->election.leader_node_id < rx_mc->election.leader_node_id) {
        // we need to use the highest here, otherwise node id 0 would be elected :)
        // If we want to use the lowest id, then we will also have to copy
        // the leader_node_id and priority when initializing the election packet
        tx = 1;
        tx_mc->election.leader_node_id = rx_mc->election.leader_node_id;
      }

      uint8_t i = 0;
      for(i = 0; i < MAX_NODE_COUNT; ++i) {
        tx |= tx_mc->election.joined_nodes[i] != rx_mc->election.joined_nodes[i];
        // copy slot to our joined nodes list, if
        if (tx_mc->election.joined_nodes[i] == 0) {
          tx_mc->election.joined_nodes[i] = rx_mc->election.joined_nodes[i];
        }
      }

      //printf("checking %d, %d\n",node_id, tx_mc->election.leader_node_id);
      if (chaos_has_node_index && tx_mc->election.leader_node_id == node_id && flags_complete && tx_mc->election.joined_nodes[chaos_node_index] == node_id) {

        //printf("DEBUG I AM THE NEW INITIATOR\n");
        chaos_set_is_initiator(1); // We are now the new initiator :) YEAH!

        // copy everything to our chaos list
        memcpy(&joined_nodes, tx_mc->election.joined_nodes, sizeof(joined_nodes));

        // We commit!
        memset(tx_flags, 0, merge_commit_get_flags_length());
        tx_flags[ARR_INDEX] |= 1 << (ARR_OFFSET);

        // Next phase \o/
        tx_mc->phase = PHASE_COMMIT;

        // Reset the rejoin index
        tx_mc->rejoin_slot = 0;
        tx_mc->rejoin_index = 0;

        // then remove every node that wants to leave
        // BUT: Not our own ;)
        tx_leaves[ARR_INDEX] &= ~(1 << (ARR_OFFSET));
        for (i = 0; i < MAX_NODE_COUNT; ++i) {
          node_id_t nid = joined_nodes[i];

          //printf("leaves check  %d %d %d (%d, %d) %d\n", i, nid, (tx_leaves[ARR_INDEX_X(i)] & (1 << (ARR_OFFSET_X(i)))) == 0, ARR_INDEX_X(i), ARR_OFFSET_X(i), tx_leaves[ARR_INDEX_X(i)]);

          if (nid != 0) {
            // check if node wants to leave

            if (tx_leaves[ARR_INDEX_X(i)] & (1 << (ARR_OFFSET_X(i)))) {
              // node has left! -> remove it
              //printf("Removing node %d with index %d\n", nid, i);
              joined_nodes[i] = 0;
              chaos_node_count--;
            }
          }
        }

        join_data_tx->node_count = chaos_node_count;
        join_data_tx->commit = 1;
        tx = 1;
        leds_on(LEDS_GREEN);
      } else if (join_merge_delta) {
        // We check if there are any nodes that only want to rejoin! We need to do it before the commit since we are waiting for their flags
        tx |= initiator_try_rejoin_node(tx_mc, join_data_tx);
      }
    } else if (tx_mc->phase == PHASE_COMMIT) {
      if (flags_complete) {
        // TODO: There might be a case, in which we have complete flags, but not all left nodes have received that message
        // This case happens, if a node did not receive the first packet with the initial flags (in that case, the flags are correct, because the leaving node set its participation flag or its leave flag was zero)
        // But in the commit phase, we cannot really set it to complete in case we have not received a message in the first phase!

        tx = 1;
        if(!complete){
          completion_slot = slot_count;
        }
        complete = 1;
        rx_progress |= rx_complete; /* received a complete packet */
      }
    }
  } else if (tx_mc->phase < rx_mc->phase) {
    // received phase is more advanced than local one -> switch to received state (and set own flags)
    memcpy(tx_mc, rx_mc, sizeof(merge_commit_t) + merge_commit_get_flags_and_leaves_overall_length());

    if (IS_INITIATOR() && tx_mc->election.leader_node_id != node_id) {
      // Whoops! Seems like another one just got the lead ;)
      // So we will remove ourself as the leader...
      chaos_set_is_initiator(0);
    }

    chaos_node_count = join_data_rx->node_count;

    // check if we could leave the network!
    if (chaos_has_node_index) {
      tx_flags[ARR_INDEX] |= 1 << (ARR_OFFSET);
      // we now check if we have successfully left
      if (tx_leaves[ARR_INDEX] & (1 << (ARR_OFFSET))) {
        chaos_has_node_index = 0; // we need to join again!
        chaos_node_index = 0;
        left = 1;
      }
    }

    tx = 1;
  } else {//tx_mc_pc->phase > rx_mc_pc->phase
    //local phase is more advanced. Drop received one and just transmit to allow others to catch up
    tx = 1;
  }

  return tx;
}


inline uint8_t handle_coordination_round(uint16_t round_count, uint16_t slot_count, merge_commit_t* tx_mc, merge_commit_t* rx_mc) {


  uint8_t* tx_leaves = merge_commit_get_leaves(tx_mc);
  uint8_t* rx_leaves = merge_commit_get_leaves(rx_mc);

  uint8_t* tx_flags = merge_commit_get_flags(tx_mc);
  uint8_t* rx_flags = merge_commit_get_flags(rx_mc);

  join_data_t* join_data_tx = &tx_mc->join_data;
  join_data_t* join_data_rx = &rx_mc->join_data;

  uint8_t tx = 0;

  if (tx_mc->type == TYPE_UNKNOWN) {
    tx_mc->type = TYPE_COORDINATION; // everything is already prepared :)
  }

  //be careful: do not mix the different phases
  if (tx_mc->phase == rx_mc->phase) {
    // same phase

    // first calculate the current leaves
    uint8_t rx_complete, flags_complete;
    tx |= merge_flags(tx_flags, tx_leaves, rx_flags, rx_leaves, &flags_complete, &rx_complete);

    if (tx_mc->phase == PHASE_MERGE) {

      tx |= handle_rejoin(tx_mc, rx_mc);
      merge_commit_merge_callback(rx_mc, tx_mc);

      // Join logic, TODO: If the nodes wanting to join have higher ids, we might never have a node rejoined!
      // But: They could persist that they want to rejoin and set a flag, or use the rejoin slot(s) directly.
      // Since no progress is made if the node is really not joined, this is safe (but not necessarily live...)
      uint8_t join_merge_delta = 0;
      tx |= join_merge_data(join_data_tx, join_data_rx, &join_merge_delta);
      if (join_merge_delta) {
        delta_at_slot = slot_count;
      }

      if (IS_INITIATOR()) {
        if (flags_complete &&
            (slot_count >= MERGE_COMMIT_MAX_COMMIT_SLOT
             || (COMMIT_THRESHOLD && /*delta_at_slot > 0 && */
                 slot_count >= delta_at_slot + COMMIT_THRESHOLD))) {
          //LEDS_ON(LEDS_RED);
          memset(tx_flags, 0, merge_commit_get_flags_length());
          tx_flags[ARR_INDEX] |= 1 << (ARR_OFFSET);

          // Next phase \o/
          tx_mc->phase = PHASE_COMMIT;
          //TODO: we could also abort here

          // join commit
          COOJA_DEBUG_PRINTF("commit! with %d joins", join_data_tx->slot_count);
          uint8_t chaos_node_count_before_commit = chaos_node_count;

          // first add the nodes
          int i;
          for (i = 0; i < join_data_tx->slot_count; i++) {
            if (join_data_tx->slots[i]) {
              int chaos_index = add_node(join_data_tx->slots[i], chaos_node_count_before_commit);
              if (chaos_index >= 0) {
                //printf("Added node %d at index %d\n", join_data_tx->slots[i], chaos_index);
                join_data_tx->indices[i] = chaos_index;
                // remove the leave flag
                tx_leaves[ARR_INDEX_X(chaos_index)] &= ~(1 << (ARR_OFFSET_X(chaos_index)));
              } else {
                join_data_tx->overflow |= 1;
                join_data_tx->slots[i] = 0; // reset the node index, otherwise the nodes will use index 0!
              }
            }
          }

          // Reset the rejoin index
          tx_mc->rejoin_slot = 0;
          tx_mc->rejoin_index = 0;

          // then remove every node that wants to leave
          // BUT: Not our own ;)
          tx_leaves[ARR_INDEX] &= ~(1 << (ARR_OFFSET));

          for (i = 0; i < MAX_NODE_COUNT; ++i) {
            node_id_t nid = joined_nodes[i];

            //printf("leaves check  %d %d %d (%d, %d) %d\n", i, nid, (tx_leaves[ARR_INDEX_X(i)] & (1 << (ARR_OFFSET_X(i)))) == 0, ARR_INDEX_X(i), ARR_OFFSET_X(i), tx_leaves[ARR_INDEX_X(i)]);

            if (nid != 0) {
              // check if node wants to leave

              if (tx_leaves[ARR_INDEX_X(i)] & (1 << (ARR_OFFSET_X(i)))) {
                // node has left! -> remove it
                //printf("Removing node %d with index %d\n", nid, i);
                joined_nodes[i] = 0;
                chaos_node_count--;
              }
            }
          }

          // we also need to update our join masks ;)
          for (i = 0; i < FLAGS_LEN; i++) {
            join_masks[i] |= ~tx_leaves[i];
          }

          join_data_tx->node_count = chaos_node_count;
          join_data_tx->commit = 1;

          tx = 1;
          leds_on(LEDS_GREEN);
        } else if (join_merge_delta) {
          // We check if there are any nodes that only want to rejoin! We need to do it before the commit since we are waiting for their flags
          tx |= initiator_try_rejoin_node(tx_mc, join_data_tx);
        }
      }
    } else if (tx_mc->phase == PHASE_COMMIT) {
      if (flags_complete) {
        // TODO: There might be a case, in which we have complete flags, but not all left nodes have received that message
        // This case happens, if a node did not receive the first packet with the initial flags (in that case, the flags are correct, because the leaving node set its participation flag or its leave flag was zero)
        // But in the commit phase, we cannot really set it to complete in case we have not received a message in the first phase!

        tx = 1;
        if(!complete){
          completion_slot = slot_count;
        }
        complete = 1;
        rx_progress |= rx_complete; /* received a complete packet */
      }
    }
  } else if (tx_mc->phase < rx_mc->phase) {
    // received phase is more advanced than local one -> switch to received state (and set own flags)
    memcpy(tx_mc, rx_mc, sizeof(merge_commit_t) + merge_commit_get_flags_and_leaves_overall_length());

    chaos_node_count = join_data_rx->node_count;

    int i;
    // we also need to update our join masks ;)
    for(i = 0; i < FLAGS_LEN; i++) {
      join_masks[i] |= ~tx_leaves[i];
    }

    // we are behind, check if we could join the network
    if( !chaos_has_node_index ){
      int i;
      // We have to check all slots since they are not ordered any more
      for (i = 0; i < join_data_rx->slot_count; i++) {
        if (join_data_rx->slots[i] == node_id) {
          chaos_node_index = join_data_rx->indices[i];
          chaos_has_node_index = 1;
          //printf("Joined with index %d\n", chaos_node_index);
          //LEDS_ON(LEDS_RED);
          COOJA_DEBUG_PRINTF("JOINED");
          joined = 1;
          rejoin_needed = 0;
          break;
        }
      }
    }

    if (chaos_has_node_index) {
      tx_flags[ARR_INDEX] |= 1 << (ARR_OFFSET);
      // we now check if we have successfully left
      if (tx_leaves[ARR_INDEX] & (1 << (ARR_OFFSET))) {
        chaos_has_node_index = 0; // we need to join again!
        chaos_node_index = 0;
        left = 1;
      }
    } else {
      // OVERFLOW TODO
      join_data_tx->overflow = 1;
    }

    tx = 1;
    //leds_on(LEDS_BLUE);
  } else {//tx_mc_pc->phase > rx_mc_pc->phase
    //local phase is more advanced. Drop received one and just transmit to allow others to catch up
    tx = 1;
  }

  return tx;
}


inline uint8_t handle_received_packet(uint16_t round_count, uint16_t slot_count, merge_commit_t* tx_mc, merge_commit_t* rx_mc) {

  uint8_t* tx_leaves = merge_commit_get_leaves(tx_mc);
  uint8_t* tx_flags = merge_commit_get_flags(tx_mc);

  join_data_t* join_data_tx = &tx_mc->join_data;
  join_data_t* join_data_rx = &rx_mc->join_data;

  uint8_t tx = 0;

  // we now check if the transmitted join config and round type matches with ours
  // we also need to rejoin, in case that we have overriden our coordination data with election data
  // But: if we were elected, we cant rejoin, since we are the initiator now...

  // if we are not part of a network yet and this is our first packet, we will reuse the received configuration
  // this prevents that we will destroy newly created networks with a lower config value ;)
  if (!chaos_has_node_index && tx_mc->type == TYPE_UNKNOWN) {
    join_set_config(join_data_rx->config);
    join_data_tx->config = join_get_config();
  }

  if (join_get_config() < join_data_rx->config) {
    //printf("Configuration mismatch mine: %d, theirs: %d\n", join_get_config(), join_data_rx->config);
    // we force a rejoin and also reset the flags in our packet
    force_rejoin(tx_flags, tx_leaves);
    // after mismatch and rejoin, we can use the new config value too
    join_set_config(join_data_rx->config);
    join_data_tx->config = join_get_config();

    // since our whole packet may be invalid, we copy everything from the received one!
    memcpy(tx_mc, rx_mc, sizeof(merge_commit_t) + merge_commit_get_flags_and_leaves_overall_length());
    tx = 1;
  } else if (join_get_config() > join_data_rx->config) {
    tx = 1; // ignore packet, it is outdated
  } else {
    // configuration is correct
    if (tx_mc->type == TYPE_ELECTION_AND_HANDOVER && rx_mc->type == TYPE_COORDINATION) {
      // the newly elected initiator ignores all coordination packets, while in its election round, the configuration number is updated at the end of the round
      if (!was_initiator && IS_INITIATOR()) {
        tx = 1;
      } else {
        tx = 1;
        // all other nodes will switch from election to coordination, but they don't participate directly (since they have overriden their value)
        memcpy(tx_mc, rx_mc, sizeof(merge_commit_t) + merge_commit_get_flags_and_leaves_overall_length());
      }
    } else if (tx_mc->type == TYPE_COORDINATION && rx_mc->type == TYPE_ELECTION_AND_HANDOVER) {
      // the received package is old (configs were already checked)
      tx = 1; // ignore it and retransmit!
    } else {
      // in this case, our own typ is either the same as the receied one, or unknown :)
      if (rx_mc->type == TYPE_ELECTION_AND_HANDOVER) {
        tx = handle_election_round(round_count, slot_count, tx_mc, rx_mc);
      } else {
        tx = handle_coordination_round(round_count, slot_count, tx_mc, rx_mc);
      }
    }
  }
  return tx;
}

static chaos_state_t
process(uint16_t round_count, uint16_t slot_count, chaos_state_t current_state, int chaos_txrx_success, size_t payload_length, uint8_t* rx_payload, uint8_t* tx_payload, uint8_t** app_flags)
{

  LEDS_ON(LEDS_RED);
  merge_commit_t* tx_mc = (merge_commit_t*)tx_payload;
  merge_commit_t* rx_mc = (merge_commit_t*)rx_payload;

  chaos_state_t next_state = CHAOS_RX;

  if( IS_INITIATOR() && current_state == CHAOS_INIT ){
    next_state = CHAOS_TX; //for the first tx of the initiator: no increase of tx_count here
    got_valid_rx = 1;      //to enable retransmissions
  }
  else if (current_state == CHAOS_RX) {

    // check if the transmission was successful
    if (chaos_txrx_success) {
      got_valid_rx = 1;
      uint8_t tx = handle_received_packet(round_count, slot_count, tx_mc, rx_mc);
      if(tx){
        next_state = CHAOS_TX;
        if( complete ){
          tx_count_complete++;
        }
      }

    } else if(got_valid_rx) {
      invalid_rx_count++;
      if(invalid_rx_count > restart_threshold){
        next_state = CHAOS_TX;
        invalid_rx_count = 0;
        if( complete ){
          tx_count_complete++;
        }
        restart_threshold = chaos_random_generator_fast() % (CHAOS_RESTART_MAX - CHAOS_RESTART_MIN) + CHAOS_RESTART_MIN;
      }
    }

  } else if(current_state == CHAOS_TX && (rx_progress || !RELIABLE_FF) && tx_count_complete >= N_TX_COMPLETE){
    next_state = CHAOS_OFF;
    leds_off(LEDS_GREEN);
  }


  /**
   * Final Cleanup and flag setting
   */
  *app_flags = tx_mc->flags_and_leaves;
  int end = (slot_count >= MERGE_COMMIT_ROUND_MAX_SLOTS - 1) || (next_state == CHAOS_OFF);


#if FAILURES_RATE
  #warning "INJECT_FAILURES!!"
  if(!IS_INITIATOR() && chaos_random_generator_fast() < 1*(CHAOS_RANDOM_MAX/(FAILURES_RATE))){
    next_state = CHAOS_OFF;
    end = 1; // we still want the cleanups and so on
  }
#endif

  if(end){
    memcpy(&mc_local.mc.value, &tx_mc->value, sizeof(merge_commit_value_t));
    mc_local.mc.phase = tx_mc->phase;
    mc_local.mc.type = tx_mc->type;
    tx_flags_final = tx_mc->flags_and_leaves;
    off_slot = slot_count;

    // check what has changed, maybe the initator changed?
    if (!IS_INITIATOR() && was_initiator) {
      // cleanup our lists!
      memset(&joined_nodes, 0, sizeof(joined_nodes));
    }

    if (IS_INITIATOR() || was_initiator) {
      //sort joined_nodes_map to speed up search (to enable the use of binary search) when adding new nodes
      join_reset_nodes_map();
      join_init_free_slots();
    }

    // increase the join config at the end to unify config number handling
    // => if the sequence number matches, the packet is relevant for the specific type
    // The direct incrementation could improve the handling of the concurrent election / coordination rounds
    // but this would need further checks, as the commit would hold a different value.
    if (mc_local.mc.phase == PHASE_COMMIT) {
      join_increase_config();
    }
  }

  if( next_state == CHAOS_TX ){
    did_tx = 1;
  }

  /* Advanced statistics */
  handle_advanced_stats(tx_mc, slot_count);
  LEDS_OFF(LEDS_RED);
  return next_state;
}

int merge_commit_is_pending(const uint16_t round_count){
  return 1;
}

int merge_commit_did_tx(){
  return did_tx;
}

uint16_t merge_commit_get_off_slot(){
  return off_slot;
}

int merge_commit_round_begin(const uint16_t round_number, const uint8_t app_id, merge_commit_value_t* merge_commit_value, uint8_t* phase, uint8_t* type, uint8_t** final_flags)
{
  LEDS_ON(LEDS_RED);
  did_tx = 0;
  has_initial_join_masks = 0;
  got_valid_rx = 0;
  complete = 0;
  tx_count_complete = 0;
  invalid_rx_count = 0;
  off_slot = MERGE_COMMIT_ROUND_MAX_SLOTS;
  completion_slot = 0;
  tx_flags_final = 0;
  rx_progress = 0;
  joined = 0;
  left = 0;
  was_initiator = IS_INITIATOR();

  delta_at_slot = 0;


#if MERGE_COMMIT_ADVANCED_STATS
  memset(merge_commit_advanced_stats, 0, sizeof(merge_commit_advanced_stats));
#endif

  /* init random restart threshold */
  restart_threshold = chaos_random_generator_fast() % (CHAOS_RESTART_MAX - CHAOS_RESTART_MIN) + CHAOS_RESTART_MIN;

  memset(&mc_local, 0, sizeof(mc_local));
  mc_local.mc.phase = PHASE_MERGE; // valid for both the election and coordination

  // we never want an unknown type
  if (merge_commit_wanted_type != TYPE_COORDINATION && merge_commit_wanted_type != TYPE_ELECTION_AND_HANDOVER) {
     merge_commit_wanted_type = TYPE_COORDINATION;
  }

  if (IS_INITIATOR()) {
    rejoin_needed = 0;

    if(merge_commit_wanted_join_state == MERGE_COMMIT_WANTED_JOIN_STATE_LEAVE) {
      if (chaos_node_count > 1) {
        mc_local.mc.type = TYPE_ELECTION_AND_HANDOVER;
        // We need to do a handover before we can leave!
        // Problem: If there is another node trying to leave, it should not need to be elected...
      } else {
        // we can just leave ;)
        chaos_set_is_initiator(0);
        mc_local.mc.type = TYPE_UNKNOWN; // only an initiator may init the type ;)
        chaos_has_node_index = 0;
        left = 1;
        chaos_node_count = 0;

      }
    } else {
      mc_local.mc.type = merge_commit_wanted_type;
    }
  } else {
    mc_local.mc.type = TYPE_UNKNOWN; // only the initiator may init the type ;)
  }

  // but not sure which type yet
  if (chaos_has_node_index && (mc_local.mc.type == TYPE_UNKNOWN || mc_local.mc.type == TYPE_COORDINATION)) {
    // We prepare for the coordination round :)
    memcpy(&mc_local.mc.value, merge_commit_value, sizeof(merge_commit_value_t));
  } else if(mc_local.mc.type == TYPE_ELECTION_AND_HANDOVER) {

    // we save the priority
    mc_local.mc.election.leader_node_id = node_id;
    mc_local.mc.election.priority = merge_commit_wanted_election_priority;

    //printf("Init leader node id %d, %d\n",mc_local.mc.election.leader_node_id,mc_local.mc.election.priority);
    // and we copy the chaos node list, if we think we are the initiator
    if (IS_INITIATOR()) {
      memcpy(&mc_local.mc.election.joined_nodes, joined_nodes, sizeof(joined_nodes));
    }
  }


  // initialize the masks
  int i;
  for(i = 0; i < FLAGS_LEN; ++i) {
    join_masks[i] = 0;
  }


  if (chaos_has_node_index) {
    mc_local.mc.join_data.node_count = chaos_node_count;
    /* set my flag */
    uint8_t* flags = merge_commit_get_flags(&mc_local.mc);
    flags[ARR_INDEX] |= 1 << (ARR_OFFSET);
    mc_local.mc.join_data.config = join_get_config(); // we only include our current config in case that we have a chaos index
  }

  uint8_t* leaves = merge_commit_get_leaves(&mc_local.mc);

  // Add join and leave behaviour
  if (IS_INITIATOR()) {
    // Initialize leave_flags for the slots that are

    // we mark every chaos index that is present
    int i = 0;
    for(i = 0; i < MAX_NODE_COUNT; ++i) {
      node_id_t nid = joined_nodes[i];
      if (nid >  0) {
        // node is present
        join_masks[ARR_INDEX_X(i)] |= 1 << (ARR_OFFSET_X(i));
      }
    }
    has_initial_join_masks = 1;

    for(i = 0; i < FLAGS_LEN; ++i) {
      leaves[i] = ~join_masks[i]; // mark left nodes
    }

  }

  // we think that all nodes are present from the beginning
  if (chaos_has_node_index && merge_commit_wanted_join_state == MERGE_COMMIT_WANTED_JOIN_STATE_LEAVE) {
    // we try to leave the network, so we remove us
    leaves[ARR_INDEX] |= (1 << (ARR_OFFSET));
    //printf("Trying to leave \n");
  } else if (!IS_INITIATOR() && !chaos_has_node_index && (rejoin_needed || merge_commit_wanted_join_state == MERGE_COMMIT_WANTED_JOIN_STATE_JOIN)){
    // we try to join the network
    mc_local.mc.join_data.slots[0] = node_id;
    mc_local.mc.join_data.slot_count = 1;
  }

  LEDS_OFF(LEDS_RED);
  chaos_round(round_number, app_id, (const uint8_t const*)&mc_local, sizeof(mc_local.mc) + merge_commit_get_flags_and_leaves_overall_length(), MERGE_COMMIT_SLOT_LEN_DCO, MERGE_COMMIT_ROUND_MAX_SLOTS, merge_commit_get_flags_length(), process);
  memcpy(&mc_local.mc.flags_and_leaves, tx_flags_final, merge_commit_get_flags_and_leaves_overall_length());

  memcpy(merge_commit_value, &mc_local.mc.value, sizeof(merge_commit_value_t));
  *final_flags = mc_local.flags_and_leaves;
  *phase = mc_local.mc.phase;
  *type = mc_local.mc.type;

  return completion_slot;
}



