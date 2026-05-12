#pragma once
#include <string>

// Abstract base for a pipeline stage worker.
// In → Out value types (copyable/movable) keep workers independent
// and serializable — enabling future GPU or distributed implementations.
template<typename In, typename Out>
class StageWorker {
public:
    virtual ~StageWorker() = default;
    virtual Out process(In pkg) = 0;
    virtual std::string stage_name() const = 0;
};
