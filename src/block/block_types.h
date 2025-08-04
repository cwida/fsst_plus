#pragma once
#include <ranges>
#include <vector>
#include "../config.h"

struct BlockWritingMetadata {
    /*
     * Signifies the number of different prefixes/suffixes inside the block.
     * Starts from 0 and goes up to the block_granularity value. For suffix it ends up being equal to the number of strings in the block.
     * For prefix it's generally less.
     */
    size_t number_of_prefixes = 0;
    size_t number_of_suffixes = 0;

    size_t prefix_area_start_index = UINT64_MAX; // initialize unset, set when inserting the first prefix
    size_t suffix_area_start_index = 0; // global index

    std::vector<uint16_t> prefix_offsets_from_first_prefix; // from start of first prefix, how many bytes until we reach prefix i
    std::vector<uint16_t> suffix_offsets_from_first_suffix; // same, but for suffix

    std::vector<uint8_t> suffix_encoded_prefix_lengths; // the length of the prefix for suffix i

    std::vector<uint8_t> suffix_prefix_index; // the index of the prefix for suffix i

    size_t prefix_area_size = 0;
    uint16_t suffix_area_size = 0;
    
    explicit BlockWritingMetadata(const size_t block_granularity) :
        prefix_offsets_from_first_prefix(block_granularity),
        suffix_offsets_from_first_suffix(block_granularity),
        suffix_encoded_prefix_lengths(block_granularity),
        suffix_prefix_index(block_granularity) {}
};

struct BlockSizingMetadata {
    size_t prefix_last_index_added = UINT64_MAX;
    size_t block_size = 0;
};