#pragma once
#include "duckdb.hpp"
#include <iostream>
#include <ranges>
#include "basic_fsst.h"
#include "generic_utils.h"
#include <block_types.h>

inline bool TryAddPrefix(BlockSizingMetadata &sm,
                         BlockWritingMetadata &wm,
                         const Prefixes &prefixes,
                         const size_t prefix_index) {
    const size_t prefix_size = prefixes.lengths[prefix_index];
    if (sm.block_size + prefix_size >= config::block_byte_capacity) {
        return false;
    }
    wm.prefix_offsets_from_first_prefix[wm.number_of_prefixes] = wm.prefix_area_size;
    wm.number_of_prefixes += 1;
    wm.prefix_area_size += prefix_size;
    sm.block_size += prefix_size;
    sm.prefix_last_index_added = prefix_index;
    return true;
}

inline size_t CalculateSuffixPlusHeaderSize(const Suffixes &suffixes,
                                  const std::vector<SimilarityChunk> &similarity_chunks,
                                  const size_t suffix_index) {
    const size_t suffix_encoded_length = suffixes.lengths[suffix_index];
    constexpr size_t prefix_length_byte = sizeof(uint8_t);
    const bool suffix_has_prefix = (similarity_chunks[FindSimilarityChunkCorrespondingToIndex(
                                suffix_index, similarity_chunks
                              )].prefix_length != 0);
    const size_t jumpback_size = suffix_has_prefix ? sizeof(uint16_t) : 0;
    return suffix_encoded_length + prefix_length_byte + jumpback_size;
}

inline bool CanFitInBlock(const BlockSizingMetadata &bsm,
                          const size_t additional_size) {
    // We also need space for one uint16_t header offset
    constexpr size_t block_header_suffix_offset = sizeof(uint16_t);
    return (bsm.block_size + additional_size + block_header_suffix_offset
            < config::block_byte_capacity);
}

/*
 * This function's purpose is to figure out how many prefixes and suffixes can be grouped into a single block,
 * without exceeding the block’s byte capacity
 */
inline size_t CalculateBlockSizeAndPopulateWritingMetadata(const std::vector<SimilarityChunk> &similarity_chunks,
                                 const CleavedResult &cleaved_result,
                                 BlockWritingMetadata &wm,
                                 const size_t suffix_area_start_index) {
    BlockSizingMetadata sm;
    // Start with the space for num_strings
    sm.block_size += sizeof(uint8_t);

    // Try to fit as many suffixes as possible, up to 128
    size_t strings_to_go = cleaved_result.suffixes.lengths.size() - suffix_area_start_index;
    while (wm.number_of_suffixes < std::min(strings_to_go, config::block_granularity)) {
        const size_t suffix_index = suffix_area_start_index + wm.number_of_suffixes; // starts at 0
        const size_t prefix_index =
            FindSimilarityChunkCorrespondingToIndex(suffix_index, similarity_chunks);

        // If new prefix is needed, try to add it
        if (prefix_index != sm.prefix_last_index_added) {
            if (!TryAddPrefix(sm, wm, cleaved_result.prefixes, prefix_index)) {
                throw std::logic_error("FSSTPLUS Compression not possible, strings too long");
            } else {
                if (wm.prefix_area_start_index == UINT64_MAX) {
                    wm.prefix_area_start_index = prefix_index;
                }
            }
        }

        // Calculate suffix size
        size_t suffix_size = CalculateSuffixPlusHeaderSize(
            cleaved_result.suffixes, similarity_chunks, suffix_index
        );
        // Check capacity
        if (!CanFitInBlock(sm, suffix_size)) {
            throw std::logic_error("FSSTPLUS Compression not possible, strings too long");
        }

        // We can fit the suffix plus its offset in the block header
        constexpr size_t block_header_suffix_offset_size = sizeof(uint16_t);
        sm.block_size += suffix_size + block_header_suffix_offset_size;

        // Update suffix metadata
        wm.suffix_offsets_from_first_suffix[wm.number_of_suffixes] = wm.suffix_area_size;
        wm.suffix_encoded_prefix_lengths[wm.number_of_suffixes] =
            cleaved_result.prefixes.lengths[prefix_index];
        wm.suffix_prefix_index[wm.number_of_suffixes] = wm.number_of_prefixes - 1; // -1 because we increased it earlier
        wm.suffix_area_size += suffix_size;
        wm.number_of_suffixes += 1;
    }

    // std::cout << "🟪 BLOCK SIZING RESULTS |"
    // << " N Strings: " << std::setw(3) << wm.number_of_suffixes
    // << " N Prefixes: " << std::setw(3) << wm.number_of_prefixes
    // << " wm.prefix_area_size: " << std::setw(3) << wm.prefix_area_size
    // << " prefix_area_start_index: " << std::setw(6) <<  wm.prefix_area_start_index
    // <<" 🟪 \n";

    return sm.block_size;
}
