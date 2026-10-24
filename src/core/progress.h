#pragma once

#include <stdexcept>
#include <string>

namespace pdftweak {

// Receives the progress of a run. An operation starts a stage with a label and a
// count of units, usually pages, then reports the units it has finished. Between units it asks
// whether the run was cancelled.
class Progress {
public:
    virtual ~Progress() = default;
    virtual void stage(const std::string& utf8Label, long long units) = 0;
    virtual void advance(long long units = 1) = 0;
    virtual bool cancelled() const = 0;
};

// Thrown at the next page or stage boundary after Progress::cancelled() turns true.
struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("Cancelled") {}
};

// Reports nothing and never cancels, for the command line.
class NullProgress final : public Progress {
public:
    void stage(const std::string&, long long) override {}
    void advance(long long) override {}
    bool cancelled() const override { return false; }
};

} // namespace pdftweak
