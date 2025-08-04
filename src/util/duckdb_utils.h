//
// Created by Yan Lanna Alexandre on 07/03/2025.
//
#pragma once
#include "duckdb.hpp"


using namespace duckdb;

inline void ExtractStringsFromDataChunk(const unique_ptr<DataChunk> &data_chunk,
                                            std::vector<std::string> &data, std::vector<size_t> &lengths,
                                            std::vector<const unsigned char *> &string_ptrs) {
    auto &vector = data_chunk->data[0];
    const auto vector_data = FlatVector::GetData<string_t>(vector);
    auto &validity = FlatVector::Validity(vector);  // Get validity mask

    // Populate lenIn and strIn
    for (size_t i = 0; i < data_chunk->size(); i++) {
        if (!validity.RowIsValid(i)) {
            // Handle NULL case
            data.push_back("");
            const std::string &stored_str = data.back();
            lengths.push_back(0);
            string_ptrs.push_back(reinterpret_cast<const unsigned char *>(stored_str.c_str()));

        } else {
            std::string str = vector_data[i].GetString();

            data.push_back(str);
            const std::string &stored_str = data.back();
            // Creates a reference to the string that was just added to the vector.
            lengths.push_back(stored_str.size());
            string_ptrs.push_back(reinterpret_cast<const unsigned char *>(stored_str.c_str()));
            // c_str() returns a pointer to the internal character array, which is a temporary array owned by the string object.
        }
    }
}
