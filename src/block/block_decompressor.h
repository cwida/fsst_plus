#pragma once
#include <generic_utils.h>
#include <ranges>
#include "duckdb.hpp"
#include <iostream>
#include "basic_fsst.h"
#include "../config.h"
#include "../global.h"

inline void DecompressBlock(const uint8_t *block_start, const fsst_decoder_t &decoder,
const uint8_t *block_stop,
const std::vector<size_t> &lengths_original,
const std::vector<const unsigned char *> &string_ptrs_original, Metadata &metadata) {
    if (config::print_decompressed_corpus) {
        std::cout << " ------- Block " << metadata.global_index/config::block_granularity << "\n";
    }

    const size_t n_strings = Load<uint8_t>(block_start);
    const uint8_t *suffix_data_area_offsets_ptr = block_start + sizeof(uint8_t);

    constexpr size_t BUFFER_SIZE = 1000000;
    unsigned char *result = new unsigned char[BUFFER_SIZE];

    for (int i = 0; i < n_strings; i ++ ) {
        // Clear the result buffer to ensure no remnants from previous strings
        memset(result, 0, BUFFER_SIZE);
        
        const uint8_t *suffix_data_area_offset_ptr = suffix_data_area_offsets_ptr + i * sizeof(uint16_t);
        const uint16_t suffix_data_area_offset = Load<uint16_t>(suffix_data_area_offset_ptr);

        // Count itself with + sizeof(uint16_t). So the offsetting starts at the value's end.
        const uint8_t *suffix_data_area_start = suffix_data_area_offset_ptr + sizeof(uint16_t) + suffix_data_area_offset;
        const uint8_t prefix_length = Load<uint8_t>(suffix_data_area_start);

        uint16_t suffix_data_area_length;
        if (i < n_strings-1) {
            // By adding +sizeof(uint16_t) we get the next suffix_data_area_offset
            const uint16_t next_suffix_offset = Load<uint16_t>(suffix_data_area_offset_ptr + sizeof(uint16_t)); // next suffix offset
            // diff between this offset and next suffix offset
            suffix_data_area_length = next_suffix_offset + sizeof(uint16_t) - suffix_data_area_offset;
        } else {
            // last suffix, have to refer to block_stop to calc its length
             suffix_data_area_length = block_stop - suffix_data_area_start;
        }

        if (prefix_length == 0) {
            const uint8_t *encoded_suffix_ptr = suffix_data_area_start + sizeof(uint8_t);
            // suffix only
            const size_t decompressed_suffix_size = fsst_decompress(&decoder,
                            suffix_data_area_length - sizeof(uint8_t),
                            encoded_suffix_ptr, BUFFER_SIZE, result);
            if (config::print_decompressed_corpus) {
                std::cout << i << " decompressed: ";
                std::cout << result << "\n";
            }
            
            // Test if it's correct!
            if (decompressed_suffix_size != lengths_original[metadata.global_index] || !TextMatches(result, string_ptrs_original[metadata.global_index], decompressed_suffix_size)) {
                std::cerr << "‼️ ERROR: Decompression mismatch (suffix only) i: "<< i <<":\n"<<"result:   " << result << "\noriginal: " << string_ptrs_original[metadata.global_index] << "\n";
                throw std::runtime_error("Decompression mismatch (suffix only)");
            }
        } else { // Has prefix
            const uint8_t *jumpback_offset_ptr = suffix_data_area_start + sizeof(uint8_t);
            const uint16_t jumpback_offset = Load<uint16_t>(jumpback_offset_ptr);
            const uint8_t *encoded_suffix_ptr = jumpback_offset_ptr + sizeof(uint16_t);

            const uint8_t *encoded_prefix_ptr =
                    encoded_suffix_ptr - jumpback_offset - sizeof(uint8_t) - sizeof(uint16_t);


            // Step 1) Decompress prefix
            const size_t decompressed_prefix_size = fsst_decompress(&decoder, prefix_length,
                                                                    encoded_prefix_ptr,
                                                                    BUFFER_SIZE, result);


            // Step 2) Decompress suffix
            const size_t decompressed_suffix_size = fsst_decompress(&decoder,
                                                                    suffix_data_area_length - sizeof(uint8_t) -
                                                                    sizeof(uint16_t),
                                                                    encoded_suffix_ptr,
                                                                    BUFFER_SIZE, result + decompressed_prefix_size);
            if (config::print_decompressed_corpus) {
                std::cout << i << " decompressed: ";
                std::cout << result << "\n";
            }

            // Test if it's correct!
            size_t decompressed_size = decompressed_suffix_size + decompressed_prefix_size;
            if (decompressed_size != lengths_original[metadata.global_index] || !TextMatches(result, string_ptrs_original[metadata.global_index], decompressed_suffix_size + decompressed_prefix_size)) {
                std::cerr << "‼️ ERROR: Decompression mismatch:\n"<<"result:   " << result << "\noriginal: " << string_ptrs_original[metadata.global_index] << "\n";
                throw std::runtime_error("Decompression mismatch");
            }


        }
        metadata.global_index += 1;
    }
    
    // Free the result buffer to avoid memory leak
    delete[] result;
}
