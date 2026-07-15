#pragma once

#include <string>
#include <string_view>

namespace pkgbuild {

enum class EventKind {
    info,
    warning,
    command,
};

struct Event {
    EventKind kind;
    std::string message;
};

class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void emit(const Event& event) = 0;
};

class NullEventSink final : public EventSink {
public:
    void emit(const Event&) override {}
};

inline void emit(EventSink& sink, EventKind kind, std::string message)
{
    sink.emit(Event{kind, std::move(message)});
}

} // namespace pkgbuild
