// Copyright (C) 2024 Haochen Jiang
//
// Authors: Haochen Jiang
// Date: 2024/1/2

/** 
 * @file data_container.h
 *
 * @brief Defines a versatile data container for various data types.
 *
 * This file contains the definition of DataContainer, a flexible and type-safe container
 * capable of holding a variety of basic and complex data types. It is based on the C++17
 * std::variant feature for managing a set of typed values efficiently and safely.
 */
#pragma once

#include <variant>
#include <vector>
#include <string>

/**
 * @brief Defines a versatile data container.
 *
 * This type definition uses std::variant to encapsulate a variety of data types.
 * It is designed to be a flexible container for data of different types,
 * including basic data types (int, float, etc.), their long and unsigned variants,
 * strings, and vectors of these types.
 *
 * @note std::variant is a C++17 feature, ensuring type safety and efficient management
 *       of a set of typed values.
 */
using DataContainer = std::variant<
    int,
    long,
    long long,
    unsigned int,
    unsigned long,
    unsigned long long,
    float,
    double,
    long double,
    std::string,
    std::vector<int>,
    std::vector<long>,
    std::vector<float>,
    std::vector<double>,
    std::vector<std::string>
>;