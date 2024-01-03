// Copyright (C) 2024 Haochen Jiang
//
// Authors: Haochen Jiang
// Date: 2024/1/2

/**
 * @file mini_batch.h
 *
 * @brief Implements the MiniBatch class.
 *
 * The MiniBatch class encapsulates a collection of data items, each of which can be of various types. It provides
 * functionality to manipulate these data items, and each MiniBatch has an associated name for identification.
 */

#pragma once

#include <vector>
#include <string>
#include "data_container.h"

/**
 * @brief The MiniBatch class stores a collection of data items and a name.
 *
 * MiniBatch is primarily used to store and manipulate a sequence of data items. Each data item can be of any type 
 * defined in the DataContainer variant. The class provides methods to add, retrieve, and manage these items.
 */
class MiniBatch {
public:
    /**
     * @brief Default constructor.
     */
    MiniBatch() = default;

    /**
     * @brief Constructs a MiniBatch with a list of data items.
     *
     * @param data A vector of data items to initialize the MiniBatch.
     */
    MiniBatch(const std::vector<DataContainer>& data)
        : batchData(data) {}

    /**
     * @brief Constructs a MiniBatch with a name and a list of data items.
     *
     * @param name The name of the MiniBatch.
     * @param data A vector of data items to initialize the MiniBatch.
     */
    MiniBatch(const std::string& name, const std::vector<DataContainer>& data)
        : batchName(name), batchData(data) {}

    /**
     * @brief Adds a data item to the MiniBatch.
     *
     * @param data The data item to add.
     */
    void addData(const DataContainer& data) {
        batchData.push_back(data);
    }

    /**
     * @brief Retrieves a data item at a specified index.
     *
     * @param index The index of the data item.
     * @return The data item at the specified index.
     */
    const DataContainer& getData(size_t index) const {
        return batchData.at(index);
    }

    std::vector<DataContainer>& getData() {
        return batchData;
    }
    
    const std::vector<DataContainer>& getData() const {
        return batchData;
    }

    /**
     * @brief Returns the number of data items in the MiniBatch.
     *
     * @return The size of the MiniBatch.
     */
    size_t size() const {
        return batchData.size();
    }

    /**
     * @brief Clears all data items from the MiniBatch.
     */
    void clear() {
        batchData.clear();
    }

    /**
     * @brief Retrieves the name of the MiniBatch.
     *
     * @return The name of the MiniBatch.
     */
    const std::string& getName() const {
        return batchName;
    }

    /**
     * @brief Sets the name of the MiniBatch.
     *
     * @param name The new name for the MiniBatch.
     */
    void setName(const std::string& name) {
        batchName = name;
    }

    // Additional methods can be added as needed.

private:
    std::vector<DataContainer> batchData; ///< Stores the data items of the MiniBatch.
    std::string batchName; ///< The name of the MiniBatch.
};
