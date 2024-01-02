#include <vector>
#include <string>
#include "data_container.h"

class MiniBatch {
public:
    // 默认构造函数
    MiniBatch() = default;

    // 使用一系列数据初始化的构造函数
    MiniBatch(const std::vector<DataContainer>& data)
        : batchData(data) {}

    // 使用名称和数据初始化的构造函数
    MiniBatch(const std::string& name, const std::vector<DataContainer>& data)
        : batchName(name), batchData(data) {}

    // 添加一个数据项
    void addData(const DataContainer& data) {
        batchData.push_back(data);
    }

    // 获取特定索引的数据项
    const DataContainer& getData(size_t index) const {
        return batchData.at(index);
    }

    // 获取MiniBatch的大小
    size_t size() const {
        return batchData.size();
    }

    // 清空MiniBatch
    void clear() {
        batchData.clear();
    }

    // 获取MiniBatch的名称
    const std::string& getName() const {
        return batchName;
    }

    // 设置MiniBatch的名称
    void setName(const std::string& name) {
        batchName = name;
    }

    // 其他可能需要的方法，比如迭代器支持等

private:
    std::vector<DataContainer> batchData; // 存储一系列的数据容器
    std::string batchName; // MiniBatch的名称
};
