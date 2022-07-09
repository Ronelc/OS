#include "MemoryConstants.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include <cstdlib>
#include <cmath>

/*
 * Initialize a new table  in specific frame.
 */
void init_table (uint64_t frame) {
  for (uint64_t line = 0; line < PAGE_SIZE; line++) {
    PMwrite (frame * PAGE_SIZE + line, 0);
  }
}

/*
 * Initialize the virtual memory.
 */
void VMinitialize () {
  init_table (0);
}

/**
 * find an empty frame in table or evict one
 * @param depth - current depth
 * @param maxFrameIndex - max index frame in use
 * @param baseAdd - frame to start the search with
 * @param pageToInsert - page we want to insert to the page table
 * @param pagePath - address of the current page
 * @param tableToKeep - table we need for next level
 * @param parentAdd - address of frame_parent
 * @param max_distance - current maximal distance
 * @param evict_page - page to evict
 * @param return_frame - empty frame to use
 * @param frame_parent - parent of return_frame to unlink
 * @param foundUnusedTable - boolean variable represent case of found unused table.
 * @return number of frame insert the page to it
 */
uint64_t
dfs (uint64_t depth, uint64_t *maxFrameIndex, uint64_t baseAdd,
     uint64_t pageToInsert, uint64_t pagePath, uint64_t *tableToKeep,
     uint64_t parentAdd, uint64_t *max_distance, uint64_t *evict_page,
     uint64_t *return_frame, uint64_t *frame_parent, uint64_t *foundUnusedTable) {

  uint64_t curr_page_distance = 0; // min distance for current page
  bool table_is_empty = true; //

  for (int line = 0; line < PAGE_SIZE; ++line) { // check if table is empty
    word_t val;
    PMread (PAGE_SIZE * baseAdd + line, &val);
    if (val != 0) { // table not empty
      table_is_empty = false;
      break;
    }
  }
  if (depth == TABLES_DEPTH) { // next level is a page
    uint64_t page_minus_p = abs ((int) pageToInsert - ((int) pagePath));
    curr_page_distance =
        NUM_PAGES - page_minus_p < page_minus_p ? NUM_PAGES - page_minus_p
                                                : page_minus_p;
    if (!*foundUnusedTable and curr_page_distance > *max_distance
        and baseAdd != *tableToKeep) { // update max distance
      *max_distance = curr_page_distance;
      *return_frame = baseAdd;
      *evict_page = (int) pagePath;
      *frame_parent = parentAdd;
    }
    return 0;
  }
  for (int line = 0; line < PAGE_SIZE; ++line) {
    word_t val;
    PMread (PAGE_SIZE * baseAdd + line, &val);
    if (val != 0) { // table is not empty
      *maxFrameIndex = *maxFrameIndex < (uint64_t)val ? (uint64_t)val : *maxFrameIndex;
      auto shift = (uint64_t) log2 (PAGE_SIZE);
      dfs (depth + 1, maxFrameIndex, val, pageToInsert,
           (pagePath << shift) + line, tableToKeep,
           PAGE_SIZE * baseAdd + line,
           max_distance, evict_page, return_frame, frame_parent, foundUnusedTable);
    }
  }
  if (table_is_empty and depth < TABLES_DEPTH
      and baseAdd != *tableToKeep) { // case of found empty table
    *return_frame = baseAdd;
    *frame_parent = parentAdd;
    *foundUnusedTable = 1;
    return 0;
  }
  if (!*foundUnusedTable
      and *maxFrameIndex + 1 < NUM_FRAMES) { // there is an unused frame
    return *maxFrameIndex + 1;
  }
  return 0;
}

/**
 * find an empty frame
 * @param baseAdd - address to start with
 * @param offset_i - offset of i level
 * @param maxFrameIndex - maximal index frame in use
 * @param pageToInsert - page we want to insert
 * @param tableToKeep - table that we have to keep
 * @return an empty frame
 */
uint64_t
createNewTable (uint64_t baseAdd, uint64_t offset_i, uint64_t *maxFrameIndex,
                uint64_t pageToInsert, uint64_t tableToKeep) {
  uint64_t max_distance = 0;
  uint64_t evict_page = 0;
  uint64_t return_frame = 0;
  uint64_t frame_parent = 0;
  uint64_t keep = tableToKeep;
  uint64_t unusedTable = 0;
  uint64_t frame = dfs (0, maxFrameIndex, 0, pageToInsert, 0,
                        &keep, 0, &max_distance, &evict_page,
                        &return_frame, &frame_parent,
                        &unusedTable);
  if (frame == 0) { // there is no empty frame
    if (unusedTable) { // case of empty table
      PMwrite (frame_parent, 0);
      return return_frame;
    }
    else { // case of frame to evict
      PMevict (return_frame, evict_page);
      PMwrite (frame_parent, 0);
      init_table (return_frame);
      return return_frame;
    }
  }
  else { // there is an empty frame
    init_table (frame);
    PMwrite (baseAdd + offset_i, (word_t) frame);
  }
  return frame;
}

/**
 * translate virtual address to physical address.
 * @param virtualAddress - address to translate
 * @return physical address
 */
uint64_t translateVirtualAddress (uint64_t virtualAddress) {
  uint64_t offset = virtualAddress % PAGE_SIZE;
  uint64_t base_add = 0;
  word_t frame = 0;
  word_t leaf = 0;
  for (auto depth = TABLES_DEPTH; depth > 0; depth--) {
    uint64_t offset_i = (virtualAddress >> (depth * OFFSET_WIDTH)) % PAGE_SIZE;
    PMread (base_add + offset_i, &frame);
    if (frame == 0) {  // if table is empty
      uint64_t maxFrameIndex = base_add / PAGE_SIZE;
      frame = (word_t) createNewTable (base_add, offset_i, &maxFrameIndex,
                                       (virtualAddress - offset) / PAGE_SIZE,
                                       maxFrameIndex);
      PMwrite (base_add + offset_i, frame);
    }
    base_add = frame * PAGE_SIZE;
  }
  PMread (base_add + offset, &leaf);
  if (leaf == 0) { // we are in leaf
    PMrestore (frame, virtualAddress >> OFFSET_WIDTH);
  }
  return base_add + offset;

}

/* Reads a word from the given virtual address
 * and puts its content in *value.
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMread (uint64_t virtualAddress, word_t *value) {
  if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
    return 0;
  }
  uint64_t p_add = translateVirtualAddress (virtualAddress);
  if (p_add == (uint64_t) -1) {
    return 0;
  }
  PMread (p_add, value);
  return 1;
}

/* Writes a word to the given virtual address.
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMwrite (uint64_t virtualAddress, word_t value) {
  if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
    return 0;
  }
  PMwrite (translateVirtualAddress (virtualAddress), value);
  return 1;
}