#include <variant>
#include <vector>
#include <string>

// 定义支持的数据类型
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