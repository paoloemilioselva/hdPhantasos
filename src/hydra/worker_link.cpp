#include "worker_link.h"

#include "trace.h"

#include "pxr/base/tf/getenv.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <signal.h>
#  include <spawn.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

PXR_NAMESPACE_OPEN_SCOPE

namespace fs = std::filesystem;

namespace {

// The shared file's layout, as worker/phantasos_worker/protocol.py has it.
constexpr std::size_t kHeader = 4096;
constexpr std::size_t kRegion = std::size_t(3840) * 2160 * 3;
constexpr std::size_t kInputOffset = kHeader;
constexpr std::size_t kOutputOffset = kHeader + kRegion;
constexpr std::size_t kFileSize = kHeader + 2 * kRegion;

// ---------------------------------------------------------------------------
// The few JSON reads the protocol needs: top-level string and number fields
// of a one-line object the worker wrote with json.dumps. A small scanner that
// walks the object rather than searching the text, so a prompt containing
// `"id":` cannot be mistaken for a field.
// ---------------------------------------------------------------------------

class JsonFields {
  public:
    explicit JsonFields(const std::string& text) : _text(text) { _Parse(); }

    bool String(const char* key, std::string* out) const
    {
        for (const auto& [name, value, isString] : _fields) {
            if (name == key && isString) {
                *out = value;
                return true;
            }
        }
        return false;
    }

    bool Number(const char* key, double* out) const
    {
        for (const auto& [name, value, isString] : _fields) {
            if (name == key && !isString) {
                char* end = nullptr;
                *out = std::strtod(value.c_str(), &end);
                return end != value.c_str();
            }
        }
        return false;
    }

  private:
    struct Field {
        std::string name, value;
        bool isString;
    };

    void _Skip() { while (_at < _text.size() && std::isspace(static_cast<unsigned char>(_text[_at]))) ++_at; }

    bool _ReadString(std::string* out)
    {
        if (_at >= _text.size() || _text[_at] != '"') return false;
        ++_at;
        while (_at < _text.size() && _text[_at] != '"') {
            char c = _text[_at++];
            if (c == '\\' && _at < _text.size()) {
                const char e = _text[_at++];
                switch (e) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'u': {
                        // Enough for the prompts the worker sends: code
                        // points below 0x800 as UTF-8, the rest as '?'.
                        unsigned code = 0;
                        for (int i = 0; i < 4 && _at < _text.size(); ++i) {
                            code = code * 16 + static_cast<unsigned>(std::stoi(std::string(1, _text[_at++]), nullptr, 16));
                        }
                        if (code < 0x80) {
                            out->push_back(static_cast<char>(code));
                        } else if (code < 0x800) {
                            out->push_back(static_cast<char>(0xC0 | (code >> 6)));
                            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            out->push_back('?');
                        }
                        continue;
                    }
                    default: c = e;
                }
            }
            out->push_back(c);
        }
        if (_at >= _text.size()) return false;
        ++_at;  // closing quote
        return true;
    }

    /// Skips a value of any kind, returning its raw text for scalars.
    bool _SkipValue(std::string* raw, bool* isString)
    {
        _Skip();
        if (_at >= _text.size()) return false;
        const char c = _text[_at];
        if (c == '"') {
            *isString = true;
            return _ReadString(raw);
        }
        *isString = false;
        if (c == '{' || c == '[') {
            int depth = 0;
            bool inString = false;
            for (; _at < _text.size(); ++_at) {
                const char d = _text[_at];
                if (inString) {
                    if (d == '\\') ++_at;
                    else if (d == '"') inString = false;
                } else if (d == '"') {
                    inString = true;
                } else if (d == '{' || d == '[') {
                    ++depth;
                } else if (d == '}' || d == ']') {
                    if (--depth == 0) {
                        ++_at;
                        return true;
                    }
                }
            }
            return false;
        }
        const std::size_t start = _at;
        while (_at < _text.size() && _text[_at] != ',' && _text[_at] != '}') ++_at;
        *raw = _text.substr(start, _at - start);
        return true;
    }

    void _Parse()
    {
        _Skip();
        if (_at >= _text.size() || _text[_at] != '{') return;
        ++_at;
        while (true) {
            _Skip();
            Field field;
            if (!_ReadString(&field.name)) return;
            _Skip();
            if (_at >= _text.size() || _text[_at] != ':') return;
            ++_at;
            if (!_SkipValue(&field.value, &field.isString)) return;
            _fields.push_back(std::move(field));
            _Skip();
            if (_at < _text.size() && _text[_at] == ',') {
                ++_at;
                continue;
            }
            return;
        }
    }

    const std::string& _text;
    std::size_t _at = 0;
    std::vector<Field> _fields;
};

std::string JsonQuote(const std::string& text)
{
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c < 0x20) {
            out += ' ';
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

std::string FindOnPath(const std::string& name)
{
    const std::string path = TfGetenv("PATH");
    if (path.empty()) return {};
#if defined(_WIN32)
    const char separator = ';';
    const char* suffixes[] = {".exe", ""};
#else
    const char separator = ':';
    const char* suffixes[] = {""};
#endif
    std::string entries = path;
    std::size_t start = 0;
    while (start <= entries.size()) {
        const std::size_t end = entries.find(separator, start);
        const std::string directory = entries.substr(start, end == std::string::npos ? std::string::npos : end - start);
        for (const char* suffix : suffixes) {
            std::error_code error;
            const fs::path candidate = fs::path(directory) / (name + suffix);
            if (!directory.empty() && fs::is_regular_file(candidate, error)) return candidate.string();
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------
// A child process with its stdin and stdout piped to us and its stderr
// appended to a log file, and the shared frame file mapped into memory.
// ---------------------------------------------------------------------------

class HdPhantasosWorkerLink::Process {
  public:
    ~Process()
    {
        Kill();
        Unmap();
    }

    bool Start(const std::vector<std::string>& arguments, const std::string& logPath,
               std::string* error);
    bool WriteLine(const std::string& line);
    bool ReadLine(std::string* line);
    void CloseInput();
    /// The exit code, or -1 if it is still running after `milliseconds`.
    int Wait(int milliseconds);
    void Kill();

    bool Map(const std::string& path, std::string* error);
    void Unmap();
    std::uint8_t* Memory() const { return _memory; }

  private:
    std::string _buffer;
    std::uint8_t* _memory = nullptr;
    std::string _mappedPath;
#if defined(_WIN32)
    HANDLE _process = nullptr;
    HANDLE _input = nullptr;   // our end of the child's stdin
    HANDLE _output = nullptr;  // our end of the child's stdout
    HANDLE _file = INVALID_HANDLE_VALUE;
    HANDLE _mapping = nullptr;
#else
    pid_t _pid = -1;
    int _input = -1;
    int _output = -1;
    int _file = -1;
#endif
};

#if defined(_WIN32)

namespace {

std::wstring Wide(const std::string& text)
{
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

/// One argument quoted as CommandLineToArgvW reads it back.
std::wstring QuoteArgument(const std::wstring& argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(c);
            backslashes = 0;
            continue;
        } else {
            backslashes = 0;
        }
        out.push_back(c);
    }
    out.append(backslashes, L'\\');
    return out + L"\"";
}

std::string LastError()
{
    return "Windows error " + std::to_string(GetLastError());
}

}  // namespace

bool HdPhantasosWorkerLink::Process::Start(const std::vector<std::string>& arguments,
                                            const std::string& logPath, std::string* error)
{
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childInput = nullptr, childOutput = nullptr;
    if (!CreatePipe(&childInput, &_input, &inherit, 0) ||
        !CreatePipe(&_output, &childOutput, &inherit, 0)) {
        *error = "cannot create pipes: " + LastError();
        return false;
    }
    // Our ends stay ours: a child that inherited them would keep its own
    // stdin open, and never see the end of input.
    SetHandleInformation(_input, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(_output, HANDLE_FLAG_INHERIT, 0);
    HANDLE log = CreateFileW(Wide(logPath).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &inherit, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    std::wstring commandLine;
    for (const std::string& argument : arguments) {
        if (!commandLine.empty()) commandLine += L' ';
        commandLine += QuoteArgument(Wide(argument));
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childInput;
    startup.hStdOutput = childOutput;
    startup.hStdError = log != INVALID_HANDLE_VALUE ? log : GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION info{};
    // No console window: usdview is a GUI program, and a worker console
    // flashing up beside it would be noise.
    const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
    CloseHandle(childInput);
    CloseHandle(childOutput);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!started) {
        *error = "cannot start '" + arguments.front() + "': " + LastError();
        return false;
    }
    CloseHandle(info.hThread);
    _process = info.hProcess;
    return true;
}

bool HdPhantasosWorkerLink::Process::WriteLine(const std::string& line)
{
    const std::string data = line + "\n";
    std::size_t written = 0;
    while (written < data.size()) {
        DWORD count = 0;
        if (!WriteFile(_input, data.data() + written, static_cast<DWORD>(data.size() - written), &count, nullptr)) {
            return false;
        }
        written += count;
    }
    return true;
}

bool HdPhantasosWorkerLink::Process::ReadLine(std::string* line)
{
    while (true) {
        const std::size_t newline = _buffer.find('\n');
        if (newline != std::string::npos) {
            *line = _buffer.substr(0, newline);
            _buffer.erase(0, newline + 1);
            return true;
        }
        char chunk[4096];
        DWORD count = 0;
        if (!_output || !ReadFile(_output, chunk, sizeof chunk, &count, nullptr) || count == 0) return false;
        _buffer.append(chunk, count);
    }
}

void HdPhantasosWorkerLink::Process::CloseInput()
{
    if (_input) {
        CloseHandle(_input);
        _input = nullptr;
    }
}

int HdPhantasosWorkerLink::Process::Wait(int milliseconds)
{
    if (!_process) return 0;
    if (WaitForSingleObject(_process, static_cast<DWORD>(milliseconds)) != WAIT_OBJECT_0) return -1;
    DWORD code = 0;
    GetExitCodeProcess(_process, &code);
    return static_cast<int>(code);
}

void HdPhantasosWorkerLink::Process::Kill()
{
    CloseInput();
    if (_process) {
        if (WaitForSingleObject(_process, 0) != WAIT_OBJECT_0) TerminateProcess(_process, 1);
        WaitForSingleObject(_process, 5000);
        CloseHandle(_process);
        _process = nullptr;
    }
    if (_output) {
        CloseHandle(_output);
        _output = nullptr;
    }
}

bool HdPhantasosWorkerLink::Process::Map(const std::string& path, std::string* error)
{
    _file = CreateFileW(Wide(path).c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (_file == INVALID_HANDLE_VALUE) {
        *error = "cannot create '" + path + "': " + LastError();
        return false;
    }
    LARGE_INTEGER size;
    size.QuadPart = static_cast<LONGLONG>(kFileSize);
    if (!SetFilePointerEx(_file, size, nullptr, FILE_BEGIN) || !SetEndOfFile(_file)) {
        *error = "cannot size '" + path + "': " + LastError();
        return false;
    }
    _mapping = CreateFileMappingW(_file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (_mapping) _memory = static_cast<std::uint8_t*>(MapViewOfFile(_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (!_memory) {
        *error = "cannot map '" + path + "': " + LastError();
        return false;
    }
    _mappedPath = path;
    return true;
}

void HdPhantasosWorkerLink::Process::Unmap()
{
    if (_memory) UnmapViewOfFile(_memory);
    if (_mapping) CloseHandle(_mapping);
    if (_file != INVALID_HANDLE_VALUE) CloseHandle(_file);
    _memory = nullptr;
    _mapping = nullptr;
    _file = INVALID_HANDLE_VALUE;
    if (!_mappedPath.empty()) {
        std::error_code ignored;
        fs::remove(_mappedPath, ignored);
        _mappedPath.clear();
    }
}

#else  // POSIX

bool HdPhantasosWorkerLink::Process::Start(const std::vector<std::string>& arguments,
                                            const std::string& logPath, std::string* error)
{
    int toChild[2], fromChild[2];
    if (pipe(toChild) != 0 || pipe(fromChild) != 0) {
        *error = std::string("cannot create pipes: ") + std::strerror(errno);
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, toChild[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fromChild[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, logPath.c_str(),
                                     O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_addclose(&actions, toChild[1]);
    posix_spawn_file_actions_addclose(&actions, fromChild[0]);

    std::vector<char*> argv;
    for (const std::string& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    const int status = posix_spawnp(&_pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(toChild[0]);
    close(fromChild[1]);
    _input = toChild[1];
    _output = fromChild[0];
    fcntl(_input, F_SETFD, FD_CLOEXEC);
    fcntl(_output, F_SETFD, FD_CLOEXEC);
    if (status != 0) {
        *error = "cannot start '" + arguments.front() + "': " + std::strerror(status);
        _pid = -1;
        return false;
    }
    // A worker that died must not take the host down with SIGPIPE.
    signal(SIGPIPE, SIG_IGN);
    return true;
}

bool HdPhantasosWorkerLink::Process::WriteLine(const std::string& line)
{
    const std::string data = line + "\n";
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t count = write(_input, data.data() + written, data.size() - written);
        if (count <= 0) return false;
        written += static_cast<std::size_t>(count);
    }
    return true;
}

bool HdPhantasosWorkerLink::Process::ReadLine(std::string* line)
{
    while (true) {
        const std::size_t newline = _buffer.find('\n');
        if (newline != std::string::npos) {
            *line = _buffer.substr(0, newline);
            _buffer.erase(0, newline + 1);
            return true;
        }
        char chunk[4096];
        const ssize_t count = _output >= 0 ? read(_output, chunk, sizeof chunk) : -1;
        if (count <= 0) return false;
        _buffer.append(chunk, static_cast<std::size_t>(count));
    }
}

void HdPhantasosWorkerLink::Process::CloseInput()
{
    if (_input >= 0) {
        close(_input);
        _input = -1;
    }
}

int HdPhantasosWorkerLink::Process::Wait(int milliseconds)
{
    if (_pid < 0) return 0;
    for (int waited = 0;; waited += 50) {
        int status = 0;
        if (waitpid(_pid, &status, WNOHANG) == _pid) {
            _pid = -1;
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
        if (waited >= milliseconds) return -1;
        usleep(50 * 1000);
    }
}

void HdPhantasosWorkerLink::Process::Kill()
{
    CloseInput();
    if (_pid > 0) {
        kill(_pid, SIGTERM);
        if (Wait(3000) < 0 && _pid > 0) {
            kill(_pid, SIGKILL);
            Wait(3000);
        }
    }
    if (_output >= 0) {
        close(_output);
        _output = -1;
    }
}

bool HdPhantasosWorkerLink::Process::Map(const std::string& path, std::string* error)
{
    _file = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (_file < 0 || ftruncate(_file, static_cast<off_t>(kFileSize)) != 0) {
        *error = "cannot create '" + path + "': " + std::strerror(errno);
        return false;
    }
    void* memory = mmap(nullptr, kFileSize, PROT_READ | PROT_WRITE, MAP_SHARED, _file, 0);
    if (memory == MAP_FAILED) {
        *error = "cannot map '" + path + "': " + std::strerror(errno);
        return false;
    }
    _memory = static_cast<std::uint8_t*>(memory);
    _mappedPath = path;
    return true;
}

void HdPhantasosWorkerLink::Process::Unmap()
{
    if (_memory) munmap(_memory, kFileSize);
    if (_file >= 0) close(_file);
    _memory = nullptr;
    _file = -1;
    if (!_mappedPath.empty()) {
        unlink(_mappedPath.c_str());
        _mappedPath.clear();
    }
}

#endif

// ---------------------------------------------------------------------------
// The link
// ---------------------------------------------------------------------------

std::string HdPhantasosWorkerLink::DefaultCache()
{
    if (const std::string cache = TfGetenv("HDPHANTASOS_CACHE"); !cache.empty()) return cache;
#if defined(_WIN32)
    if (const std::string local = TfGetenv("LOCALAPPDATA"); !local.empty()) {
        return (fs::path(local) / "hdPhantasos").string();
    }
#endif
    const std::string home = TfGetenv("HOME");
    return (fs::path(home.empty() ? "." : home) / ".cache" / "hdPhantasos").string();
}

HdPhantasosWorkerLink::HdPhantasosWorkerLink(Options options) : _options(std::move(options))
{
    _thread = std::thread([this] { _Run(); });
}

HdPhantasosWorkerLink::~HdPhantasosWorkerLink()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        // Unblocks a thread waiting on the worker's answer: its pipe closes.
        if (_process) _process->Kill();
    }
    _wake.notify_all();
    if (_thread.joinable()) _thread.join();
}

void HdPhantasosWorkerLink::Submit(Request request)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _pending = std::move(request);
    }
    _wake.notify_all();
}

std::optional<HdPhantasosWorkerLink::Result> HdPhantasosWorkerLink::Take(std::uint64_t key)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_result || _result->key != key) return std::nullopt;
    std::optional<Result> result = std::move(_result);
    _result.reset();
    return result;
}

std::string HdPhantasosWorkerLink::Status() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _status;
}

void HdPhantasosWorkerLink::_SetStatus(const std::string& status)
{
    HdPhantasosTrace("worker: %s", status.c_str());
    std::lock_guard<std::mutex> lock(_mutex);
    _status = status;
}

void HdPhantasosWorkerLink::_Run()
{
    while (true) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _wake.wait(lock, [this] { return _stopping || _pending.has_value(); });
            if (_stopping) break;
            request = std::move(*_pending);
            _pending.reset();
        }

        Result result;
        result.key = request.key;
        bool running;
        std::string startError;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            running = _process != nullptr;
            startError = _startError;
        }
        if (!running && startError.empty()) {
            std::string error;
            if (!_Start(&error)) {
                std::lock_guard<std::mutex> lock(_mutex);
                _startError = error;
                _status = error;
                _process.reset();
            }
            std::lock_guard<std::mutex> lock(_mutex);
            startError = _startError;
        }
        if (!startError.empty()) {
            result.error = startError;
        } else if (!_Exchange(request, &result)) {
            // The worker died. Say so, and start a fresh one for the next
            // frame rather than giving up on the session.
            std::lock_guard<std::mutex> lock(_mutex);
            if (_stopping) break;
            if (result.error.empty()) {
                result.error = "the worker stopped; see " +
                               (fs::path(_options.cache) / "logs" / "worker.log").string();
            }
            _status = result.error;
            _process.reset();
        }
        std::lock_guard<std::mutex> lock(_mutex);
        _result = std::move(result);
    }

    std::unique_ptr<Process> process;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        process = std::move(_process);
    }
    if (process) {
        process->WriteLine("{\"op\": \"quit\"}");
        process->CloseInput();
        if (process->Wait(2000) < 0) process->Kill();
    }
}

bool HdPhantasosWorkerLink::_Start(std::string* error)
{
    const fs::path cache = _options.cache;
    std::error_code ignored;
    fs::create_directories(cache / "logs", ignored);
    fs::create_directories(cache / "run", ignored);
    const std::string log = (cache / "logs" / "worker.log").string();

    std::string python = _options.python;
    if (python.empty()) {
#if defined(_WIN32)
        python = (cache / "venv" / "Scripts" / "python.exe").string();
#else
        python = (cache / "venv" / "bin" / "python").string();
#endif
    }

    // --- First use: make the venv, if allowed to download what it needs.
    if (!fs::exists(python)) {
        if (!_options.allowDownload) {
            *error = "the model worker is not set up. Turn on 'Allow downloads' "
                     "(HDPHANTASOS_ALLOW_DOWNLOAD=1) to set it up, about 4 GB, or run "
                     "worker/setup_worker.py yourself";
            return false;
        }
        std::string uv;
        uv = TfGetenv("HDPHANTASOS_UV");
        if (uv.empty()) uv = FindOnPath("uv");
        if (uv.empty()) {
            *error = "setting up the model worker needs uv (https://docs.astral.sh/uv/) on PATH";
            return false;
        }
        _SetStatus("setting up the model worker: Python, PyTorch, libraries (first use, a few GB)");
        Process setup;
        const std::vector<std::string> command = {
            uv, "run", "--no-project", "--python", "3.12",
            (fs::path(_options.workerDirectory) / "setup_worker.py").string(), "--cache", cache.string()};
        if (!setup.Start(command, log, error)) return false;
        setup.CloseInput();
        std::string line;
        while (setup.ReadLine(&line)) {}
        const int code = setup.Wait(24 * 3600 * 1000);
        if (code != 0 || !fs::exists(python)) {
            *error = "setting up the model worker failed; see " + log;
            return false;
        }
    }

    // --- Start the worker and wait for it to say it is listening.
    _SetStatus("starting the model worker");
    auto process = std::make_unique<Process>();
    const std::string shared =
        (cache / "run" / ("frames-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".bin")).string();
    if (!process->Map(shared, error)) return false;
    std::vector<std::string> command = {
        python, "-u", (fs::path(_options.workerDirectory) / "run_worker.py").string(),
        "--cache", cache.string(), "--shared", shared};
    if (_options.allowDownload) command.push_back("--allow-download");
    if (!process->Start(command, log, error)) return false;

    std::string line;
    while (process->ReadLine(&line)) {
        std::string op;
        if (JsonFields(line).String("op", &op) && op == "ready") {
            _SetStatus("ready");
            std::lock_guard<std::mutex> lock(_mutex);
            _process = std::move(process);
            return true;
        }
    }
    *error = "the model worker did not start; see " + log;
    return false;
}

bool HdPhantasosWorkerLink::_Exchange(const Request& request, Result* result)
{
    Process* process;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        process = _process.get();
    }
    if (!process) return false;
    const std::size_t size = static_cast<std::size_t>(request.width) * request.height * 3;
    if (size > kRegion || request.rgb.size() < size) {
        result->error = "the frame is larger than the shared region (4K)";
        return true;
    }
    std::memcpy(process->Memory() + kInputOffset, request.rgb.data(), size);

    const std::string message =
        "{\"op\": \"frame\", \"id\": " + std::to_string(request.key) +
        ", \"width\": " + std::to_string(request.width) + ", \"height\": " + std::to_string(request.height) +
        ", \"settings\": " + (request.settings.empty() ? "{}" : request.settings) +
        ", \"manifest\": " + (request.manifest.empty() ? "{}" : request.manifest) + "}";
    if (!process->WriteLine(message)) return false;

    std::string line;
    while (process->ReadLine(&line)) {
        const JsonFields fields(line);
        std::string op;
        if (!fields.String("op", &op)) continue;
        if (op == "status") {
            std::string status;
            if (fields.String("message", &status)) _SetStatus(status);
            continue;
        }
        double id = -1;
        fields.Number("id", &id);
        if (static_cast<std::uint64_t>(id) != request.key) continue;
        if (op == "error") {
            fields.String("message", &result->error);
            if (result->error.empty()) result->error = "the worker failed";
            _SetStatus("error: " + result->error);
            return true;
        }
        if (op == "result") {
            double width = 0, height = 0;
            fields.Number("width", &width);
            fields.Number("height", &height);
            fields.Number("ms", &result->milliseconds);
            fields.String("prompt", &result->prompt);
            result->width = static_cast<int>(width);
            result->height = static_cast<int>(height);
            const std::size_t bytes = static_cast<std::size_t>(result->width) * result->height * 3;
            if (bytes == 0 || bytes > kRegion) {
                result->error = "the worker returned an empty or oversized picture";
                return true;
            }
            result->rgb.assign(process->Memory() + kOutputOffset, process->Memory() + kOutputOffset + bytes);
            _SetStatus("ready");
            return true;
        }
    }
    return false;
}

std::string HdPhantasosJsonQuote(const std::string& text)
{
    return JsonQuote(text);
}

PXR_NAMESPACE_CLOSE_SCOPE
