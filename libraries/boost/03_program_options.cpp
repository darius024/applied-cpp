#include <boost/program_options.hpp>
#include <iostream>
#include <string>
#include <thread>

// compile: g++ -std=c++17 03_program_options.cpp -o 03_program_options -lboost_program_options
// run:     ./03_program_options --host localhost --port 9090 --threads 4 --verbose
// help:    ./03_program_options --help

// ─────────────────────────────────────────────────────────────────────
// boost::program_options: declarative CLI argument parsing.
//
// Typical flow:
//   1. Describe options with add_options().
//   2. parse_command_line() — tokenises argc/argv into a variables_map.
//   3. store() + notify() — validate and populate the map.
//      notify() throws required_option if a required arg is missing.
//   4. Access via vm["key"].as<T>().
// ─────────────────────────────────────────────────────────────────────

namespace po = boost::program_options;

int main(int argc, char* argv[])
{
    const int hw_threads = static_cast<int>(std::thread::hardware_concurrency());

    po::options_description desc("Options");
    desc.add_options()
        ("help,h",    "print this message and exit")
        ("host",      po::value<std::string>()->required(),            "server hostname (required)")
        ("port,p",    po::value<int>()->default_value(8080),           "listen port")
        ("threads,t", po::value<int>()->default_value(hw_threads),     "worker thread count")
        ("verbose,v", po::bool_switch()->default_value(false),         "verbose output");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 0;
        }

        po::notify(vm); // throws po::required_option if --host is missing
    }
    catch (const po::required_option& e) {
        std::cerr << "error: " << e.what() << "\n\n" << desc << "\n";
        return 1;
    }
    catch (const po::error& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    // ── Access parsed values ──────────────────────────────────────────
    std::cout << "host:    " << vm["host"].as<std::string>() << "\n";
    std::cout << "port:    " << vm["port"].as<int>()         << "\n";
    std::cout << "threads: " << vm["threads"].as<int>()      << "\n";
    std::cout << "verbose: " << std::boolalpha
                             << vm["verbose"].as<bool>()     << "\n";

    // ── Checking presence vs using default ────────────────────────────
    // vm.count("key") is 0 if the user didn't supply the flag,
    // even if a default was set — the default IS stored in the map though.
    if (!vm["port"].defaulted())
        std::cout << "(port was explicitly set by the user)\n";
    else
        std::cout << "(port is using the default)\n";
}
