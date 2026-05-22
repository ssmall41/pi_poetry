#pragma once
#include <functional>
#include <string>

template<typename In, typename Out>
class StageWorker {
public:
    virtual ~StageWorker() = default;
    virtual void process(In pkg, const std::function<void(Out)>& emit) = 0;
    virtual std::string stage_name() const = 0;
};
