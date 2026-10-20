#pragma once

// The delegate's side of the model worker (worker/phantasos_worker).
//
// The worker is a separate Python process with its own venv, started on first
// use and kept running. Messages are JSON lines on its stdin and stdout;
// pixels go through a memory-mapped file (worker/phantasos_worker/protocol.py
// has the layout; keep the two in step). Its stderr goes to a log file in the
// cache, <cache>/logs/worker.log.
//
// Requests are newest-wins: at most one frame is with the worker, and a newer
// request replaces one still waiting, so a moving camera never builds a queue.
// All process I/O happens on one thread owned by this object; the render pass
// only submits and polls.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "pxr/pxr.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdPhantasosWorkerLink {
  public:
    struct Request {
        std::uint64_t key = 0;
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgb;  // 8-bit sRGB, top row first
        std::string manifest;           // JSON object, one line
        std::string settings;           // JSON object, one line
    };

    struct Result {
        std::uint64_t key = 0;
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgb;  // 8-bit sRGB, top row first
        std::string error;              // empty on success
        std::string prompt;
        double milliseconds = 0;
    };

    struct Options {
        std::string cache;           // venv, model weights, logs, shared file
        std::string workerDirectory; // where run_worker.py and setup_worker.py are
        std::string python;          // overrides <cache>/venv's Python when set
        bool allowDownload = false;
    };

    explicit HdPhantasosWorkerLink(Options options);
    ~HdPhantasosWorkerLink();

    HdPhantasosWorkerLink(const HdPhantasosWorkerLink&) = delete;
    HdPhantasosWorkerLink& operator=(const HdPhantasosWorkerLink&) = delete;

    /// Queue a frame, replacing any that has not been sent yet.
    void Submit(Request request);

    /// The result for `key` once it is there, taken out of the link.
    std::optional<Result> Take(std::uint64_t key);

    /// What the worker is doing, for render stats: "starting", "loading
    /// FLUX.2 Klein 4B", "ready", or why it failed.
    std::string Status() const;

    /// The cache directory the worker uses when nothing overrides it:
    /// %LOCALAPPDATA%\hdPhantasos, or ~/.cache/hdPhantasos.
    static std::string DefaultCache();

  private:
    class Process;

    void _Run();
    bool _Start(std::string* error);
    bool _Exchange(const Request& request, Result* result);
    void _SetStatus(const std::string& status);

    Options _options;
    std::unique_ptr<Process> _process;

    mutable std::mutex _mutex;
    std::condition_variable _wake;
    std::optional<Request> _pending;
    std::optional<Result> _result;
    std::string _status = "idle";
    std::string _startError;  // a failed start is not retried
    bool _stopping = false;
    std::thread _thread;
};

/// `text` as a JSON string literal, quotes included.
std::string HdPhantasosJsonQuote(const std::string& text);

PXR_NAMESPACE_CLOSE_SCOPE
