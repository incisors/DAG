// Copyright (C) 2024 Haochen Jiang
//
// Authors: Haochen Jiang
// Date: 2024/1/2

/**
 * @file graph_node.h
 *
 * @brief Represents a node within a computational graph capable of processing data.
 *
 * This class encapsulates a single node in a computational graph, where each node can execute
 * a specific computational task. It can process data on either CPU or GPU, depending on its configuration.
 */

#pragma once

#include <vector>
#include <functional>
#include <map>
#include <string>
#include "data_container.h"

enum class ComputeType { CPU, GPU };

class GraphNode {
public:
    /**
     * @brief Default constructor for GraphNode, setting its compute type.
     * 
     * @param type The compute type of the node (CPU or GPU).
     */
    GraphNode(ComputeType type) : computeType(type) {}

    /**
     * @brief Constructor allowing direct setting of the processing function.
     * 
     * @param type The compute type of the node (CPU or GPU).
     * @param processFunc The processing function to be executed by this node.
     */
    GraphNode(ComputeType type, std::function<void(std::map<std::string, DataContainer>&, std::map<std::string, DataContainer>&)> processFunc)
        : computeType(type) {
        if (type == ComputeType::CPU) {
            cpuProcess = processFunc;
        } else {
            gpuProcess = processFunc;
        }
    }

    /**
     * @brief Sets the CPU processing function.
     * 
     * @param cpuFunc The function to be used for CPU processing.
     */
    void setCPUProcess(std::function<void(std::map<std::string, DataContainer>&, std::map<std::string, DataContainer>&)> cpuFunc) {
        cpuProcess = cpuFunc;
    }

    /**
     * @brief Sets the GPU processing function.
     * 
     * @param gpuFunc The function to be used for GPU processing.
     */
    void setGPUProcess(std::function<void(std::map<std::string, DataContainer>&, std::map<std::string, DataContainer>&)> gpuFunc) {
        gpuProcess = gpuFunc;
    }

    /**
     * @brief Executes the node's processing function based on its compute type.
     */
    void execute() {
        if (computeType == ComputeType::CPU && cpuProcess) {
            cpuProcess(inputs, outputs);
        } else if (computeType == ComputeType::GPU && gpuProcess) {
            gpuProcess(inputs, outputs);
        }
    }

    /**
     * @brief Adds an input field and its associated data to the node.
     * 
     * @param name The name of the input field.
     * @param value The data to be associated with the input field.
     */
    void addInput(const std::string& name, const DataContainer& value) {
        inputs[name] = value;
    }

    /**
     * @brief Adds an output field and its associated data to the node.
     * 
     * @param name The name of the output field.
     * @param value The data to be associated with the output field.
     */
    void addOutput(const std::string& name, const DataContainer& value) {
        outputs[name] = value;
    }

    /**
     * @brief Sets the data for a specific input field.
     * 
     * @param name The name of the input field.
     * @param value The data to be set for the input field.
     */
    void setInput(const std::string& name, const DataContainer& value) {
        inputs[name] = value;
    }

    /**
     * @brief Sets the data for a specific output field.
     * 
     * @param name The name of the output field.
     * @param value The data to be set for the output field.
     */
    void setOutput(const std::string& name, const DataContainer& value) {
        outputs[name] = value;
    }

    /**
     * @brief Retrieves the data from a specific input field.
     * 
     * @param name The name of the input field.
     * @return The data associated with the specified input field.
     */
    const DataContainer& getInput(const std::string& name) const {
        return inputs.at(name);
    }

    /**
     * @brief Retrieves the data from a specific output field.
     * 
     * @param name The name of the output field.
     * @return The data associated with the specified output field.
     */
    const DataContainer& getOutput(const std::string& name) {
        return outputs[name];
    }

    /**
     * @brief Gets all the input fields and their associated data.
     * 
     * @return A map of input field names to their associated data.
     */
    const std::map<std::string, DataContainer>& getInputs() const {
        return inputs;
    }

    /**
     * @brief Gets all the output fields and their associated data.
     * 
     * @return A map of output field names to their associated data.
     */
    const std::map<std::string, DataContainer>& getOutputs() const {
        return outputs;
    }

private:
    ComputeType computeType; ///< The compute type of the node (CPU or GPU).
    std::map<std::string, DataContainer> inputs; ///< Map of input field names to data.
    std::map<std::string, DataContainer> outputs; ///< Map of output field names to data.
    std::function<void(std::map<std::string, DataContainer>&, std::map<std::string, DataContainer>&)> cpuProcess; ///< The CPU processing function.
    std::function<void(std::map<std::string, DataContainer>&, std::map<std::string, DataContainer>&)> gpuProcess; ///< The GPU processing function.
};
