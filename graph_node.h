#include <vector>
#include <functional>
#include <map>
#include <string>
#include "data_container.h"

enum class ComputeType { CPU, GPU };

class GraphNode {
public:
    // 默认构造函数
    GraphNode(ComputeType type) : computeType(type) {}

    // 允许直接设置处理函数的构造函数
    GraphNode(ComputeType type, std::function<void(std::map<std::string, DataContainer>&, std::map<std::string, DataContainer>&)> processFunc)
        : computeType(type) {
        if (type == ComputeType::CPU) {
            cpuProcess = processFunc;
        } else {
            gpuProcess = processFunc;
        }
    }

    // 设置CPU处理函数
    void setCPUProcess(std::function<void(std::map<std::string, DataContainer>&, std::map<std::string, DataContainer>&)> cpuFunc) {
        cpuProcess = cpuFunc;
    }

    // 设置GPU处理函数
    void setGPUProcess(std::function<void(std::map<std::string, DataContainer>&, std::map<std::string, DataContainer>&)> gpuFunc) {
        gpuProcess = gpuFunc;
    }

    // 执行处理函数，根据计算类型决定调用哪个
    void execute() {
        if (computeType == ComputeType::CPU && cpuProcess) {
            cpuProcess(inputs, outputs);
        } else if (computeType == ComputeType::GPU && gpuProcess) {
            gpuProcess(inputs, outputs);
        }
    }

    // 添加输入输出字段及其数据
    void addInput(const std::string& name, const DataContainer& value) {
        inputs[name] = value;
    }

    void addOutput(const std::string& name, const DataContainer& value) {
        outputs[name] = value;
    }

    // 设置输入数据
    void setInput(const std::string& name, const DataContainer& value) {
        inputs[name] = value;
    }

    // 设置输出数据
    void setOutput(const std::string& name, const DataContainer& value) {
        outputs[name] = value;
    }

    // 获取输入输出数据
    const DataContainer& getInput(const std::string& name) const {
        return inputs.at(name);
    }

    const DataContainer& getOutput(const std::string& name) {
        return outputs[name];
    }

    const std::map<std::string, DataContainer>& getInputs() const {
        return inputs;
    }

    const std::map<std::string, DataContainer>& getOutputs() const {
        return outputs;
    }

private:
    ComputeType computeType;
    std::map<std::string, DataContainer> inputs;
    std::map<std::string, DataContainer> outputs;
    std::function<void(std::map<std::string, DataContainer>&, std::map<std::string, DataContainer>&)> cpuProcess;
    std::function<void(std::map<std::string, DataContainer>&, std::map<std::string, DataContainer>&)> gpuProcess;
};
