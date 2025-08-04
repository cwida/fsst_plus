//
// Created by Yan Lanna Alexandre on 07/03/2025.
//
#pragma once
#include <vector>
#include <iostream>
#include <cassert>
#include "cleaving_types.h"

inline bool TextMatches(const unsigned char *result, const unsigned char *original, const size_t &size) {
    for (int i = 0; i < size; ++i) {
        if (result[i] != original[i]) {
            std::cout<<"MISMATCH: "<<"result["<<i<<"]: "<<result[i] << "original["<<i<<"]: "<< original[i]<< "\n";
            for (int i = 0; i < size; ++i) {
                std::cout << result[i];
            }
            std::cout<<"\n";
            for (int i = 0; i < size; ++i) {
                std::cout << original[i];
            }
            std::cout<<"\n";

            return false;
        }
    }
    return true;
}

inline size_t FindSimilarityChunkCorrespondingToIndex(const size_t &target_index,
                                                           const std::vector<SimilarityChunk> &similarity_chunks) {
    assert(!similarity_chunks.empty());
    assert(target_index >= similarity_chunks[0].start_index);

    // Binary search to find the chunk where target_index falls between start_index of current chunk
    // and start_index of next chunk (or is within the last chunk)
    size_t l = 0, r = similarity_chunks.size() - 1;
    while (l <= r) {
        const size_t m = l + (r - l) / 2;

        // If this is the last chunk or if target is in current chunk's range
        if (m == similarity_chunks.size() - 1 ||
            (similarity_chunks[m].start_index <= target_index &&
             target_index < similarity_chunks[m + 1].start_index)) {
            return m;
             }

        // If target is in a later chunk
        if (similarity_chunks[m].start_index <= target_index) {
            l = m + 1;
        }
        // If target is in an earlier chunk
        else {
            r = m - 1;
        }
    }

    // Default to the last chunk if not found
    return similarity_chunks.size() - 1;
}


inline size_t CalculateInputSize(const StringCollection &input) {
    size_t total_string_size = 0;
    for (size_t string_length: input.lengths) {
        total_string_size += string_length;
    }
    return total_string_size;
}