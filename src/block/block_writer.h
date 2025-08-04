#pragma once
#include "duckdb.hpp"
#include <iostream>
#include <ranges>
#include "basic_fsst.h"

inline void WriteBlockHeader(const BlockWritingMetadata &wm, uint8_t *&current_data_ptr) {
    // A 1) Write the number of strings as an uint_8
    Store<uint8_t>(wm.number_of_suffixes, current_data_ptr);
    current_data_ptr += sizeof(uint8_t);

    // A 2) Write the suffix_data_area_offsets[]
    for (size_t i = 0; i < wm.number_of_suffixes; i++) {
        const uint16_t offset_array_size_to_go = (wm.number_of_suffixes - i) * sizeof(uint16_t) - sizeof(uint16_t); // Count itself with - sizeof(uint16_t). So the offsetting starts at the value's end.
        uint16_t suffix_data_area_offset = wm.prefix_area_size + offset_array_size_to_go + wm.suffix_offsets_from_first_suffix[i];
        Store<uint16_t>(suffix_data_area_offset, current_data_ptr);
        current_data_ptr += sizeof(uint16_t);
    }
}

inline void WritePrefixArea(const Prefixes &prefixes, const BlockWritingMetadata &wm,
                              const size_t prefix_area_start_index, 
                              uint8_t *&current_data_ptr // A reference to a pointer. When updated, the original pointer is updated as well.
                              ) {
    // std::cout << "prefix_compression_result.encoded_string_lengths.size(): " << prefix_compression_result.encoded_string_lengths.size() << '\n';
    // std::cout << "wm.prefix_n_in_block: " << wm.prefix_n_in_block << '\n';

    for (size_t i = 0; i < wm.number_of_prefixes; i++) {
        // std::cout << "iteration: " << i << '\n';

        const size_t prefix_index = prefix_area_start_index + i;
        // std::cout << "prefix_index: " << prefix_index << '\n';
        
        // Add bounds checking before accessing the vector
        if (prefix_index >= prefixes.lengths.size()) {
            std::cerr << "⛔️ Prefix index out of bounds: " << prefix_index << " >= " << prefixes.lengths.size() << "\n";
            throw std::logic_error("Prefix index out of bounds.");
        }
        
        const size_t prefix_length = prefixes.lengths[prefix_index];

        // std::cout << "Write Prefix " << i << " Length=" << prefix_length << '\n';
        
        const unsigned char *prefix_start = prefixes.string_ptrs[prefix_index];

        // std::cout << "Current data ptr: " << static_cast<void*>(current_data_ptr) << '\n';  // Cast to void* to print address
        // std::cout << "Will write up to: " << static_cast<void*>(current_data_ptr + prefix_length) << '\n';  

        memcpy(current_data_ptr, prefix_start, prefix_length);
        current_data_ptr += prefix_length;
    }
}

inline void WriteSuffixArea(const Suffixes &suffixes, const BlockWritingMetadata &wm,
                              const size_t &suffix_area_start_index, uint8_t *&current_data_ptr) {
    for (size_t i = 0; i < wm.number_of_suffixes; i++) {
        const size_t suffix_index = suffix_area_start_index + i;
        
        // Add bounds check
        if (suffix_index >= suffixes.string_ptrs.size()) {
            std::cerr << "⚠️ Invalid suffix index: " << suffix_index << "\n";
            throw std::logic_error("Invalid suffix index. Terminating.");
        }
        
        uint8_t prefix_index = wm.suffix_prefix_index[i];
        uint8_t suffix_prefix_length = wm.suffix_encoded_prefix_lengths[i];
        const bool suffix_has_prefix = suffix_prefix_length != 0;

        // write the length of the prefix, can be zero
        Store<uint8_t>(suffix_prefix_length, current_data_ptr);
        current_data_ptr += sizeof(uint8_t);

        // if there is a prefix, calculate offset and store it
        if (suffix_has_prefix) {
            size_t prefix_offset_from_first_prefix = wm.prefix_offsets_from_first_prefix[prefix_index];
            size_t suffix_offset_from_first_suffix = wm.suffix_offsets_from_first_suffix[i]; // should it index by suffix_index or by i?
            uint16_t prefix_jumpback_offset = (wm.prefix_area_size - prefix_offset_from_first_prefix) +
                                              suffix_offset_from_first_suffix;

            Store<uint16_t>(prefix_jumpback_offset, current_data_ptr);
            current_data_ptr += sizeof(uint16_t);
        }

        // write the suffix
        const size_t suffix_length = suffixes.lengths[suffix_index];
        const unsigned char *suffix_start = suffixes.string_ptrs[suffix_index];
        memcpy(current_data_ptr, suffix_start, suffix_length);
        current_data_ptr += suffix_length;
    }
}


inline uint8_t * WriteBlock(uint8_t *block_start,
                        const CleavedResult &cleaved_result, const BlockWritingMetadata &wm) {

    uint8_t *current_data_ptr = block_start;
    // A) WRITE THE HEADER
    WriteBlockHeader(wm, current_data_ptr);

    // B) WRITE THE PREFIX AREA
    WritePrefixArea(cleaved_result.prefixes, wm, wm.prefix_area_start_index, current_data_ptr);

    // C) WRITE SUFFIX AREAˆ
    WriteSuffixArea(cleaved_result.suffixes, wm, wm.suffix_area_start_index, current_data_ptr);

    return current_data_ptr;
}
