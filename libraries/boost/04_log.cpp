#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <iostream>

// compile: g++ -std=c++17 04_log.cpp -o 04_log \
//          -lboost_log -lboost_log_setup -lboost_thread -lboost_system \
//          -pthread -DBOOST_LOG_DYN_LINK

// ─────────────────────────────────────────────────────────────────────
// boost::log: severity-based, multi-sink logging.
//
// HPC relevance: filtering is evaluated at the SOURCE before any
// string formatting — a filtered-out log entry costs almost nothing.
// Async front-ends can decouple the hot path from I/O entirely.
//
// Severity levels (ascending): trace debug info warning error fatal.
// ─────────────────────────────────────────────────────────────────────

namespace logging  = boost::log;
namespace trivial  = boost::log::trivial;
namespace expr     = boost::log::expressions;
namespace keywords = boost::log::keywords;

void init_logging(trivial::severity_level min_level)
{
    // Add standard attributes: TimeStamp, LineID, ThreadID, ProcessID.
    logging::add_common_attributes();

    // Console sink: only severity + message.
    logging::add_console_log(
        std::clog,
        keywords::format = (
            expr::stream
                << "[" << trivial::severity << "] "
                << expr::smessage
        )
    );

    // File sink: rotates at 10 MB, keeps the last 5 files.
    logging::add_file_log(
        keywords::file_name        = "app_%N.log",
        keywords::rotation_size    = 10 * 1024 * 1024,
        keywords::max_files        = 5,
        keywords::auto_flush       = true,
        keywords::format = (
            expr::stream
                << "[" << trivial::severity << "] "
                << expr::smessage
        )
    );

    // Global severity filter — entries below min_level are dropped
    // BEFORE any formatting or sink dispatch (near-zero cost).
    logging::core::get()->set_filter(trivial::severity >= min_level);
}

int main()
{
    init_logging(trivial::info); // trace and debug will be dropped

    BOOST_LOG_TRIVIAL(trace)   << "trace — filtered out (no output)";
    BOOST_LOG_TRIVIAL(debug)   << "debug — filtered out (no output)";
    BOOST_LOG_TRIVIAL(info)    << "server started on :8080";
    BOOST_LOG_TRIVIAL(warning) << "connection pool at 80% capacity";
    BOOST_LOG_TRIVIAL(error)   << "failed to open config.toml";
    BOOST_LOG_TRIVIAL(fatal)   << "out of memory — terminating";

    // ── Runtime filter change ─────────────────────────────────────────
    // Promote to debug during a diagnostic window — no recompile needed.
    logging::core::get()->set_filter(trivial::severity >= trivial::debug);
    BOOST_LOG_TRIVIAL(debug)   << "debug now visible";
    BOOST_LOG_TRIVIAL(trace)   << "trace still filtered";
}
