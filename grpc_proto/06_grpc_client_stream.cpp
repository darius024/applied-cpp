#include "metrics.grpc.pb.h"
#include "metrics.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

// build: cmake -B build && cmake --build build --target 06_grpc_client_stream

// ─────────────────────────────────────────────────────────────────────
// gRPC client-side streaming: stream of requests → one response.
//
// Proto:  rpc BatchWrite (stream MetricPoint) returns (BatchResult)
//
// Server: receives a ServerReader<Request>* and a response pointer.
//   reader->Read(&msg) consumes one message; returns false when the
//   client has closed the write side.
//   Populate the response and return Status — this closes the RPC.
//
// Client: stub call returns a ClientWriter<Request>* and the response
//   is passed by pointer alongside a ClientContext.
//   writer->Write(msg) sends a message.
//   writer->WritesDone() signals no more messages are coming.
//   writer->Finish() blocks until the server returns its response + Status.
//
// Use cases: bulk inserts, log/metric ingestion, file uploads.
// ─────────────────────────────────────────────────────────────────────

using namespace metrics;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ClientContext;
using grpc::Status;

// ── Service ───────────────────────────────────────────────────────────
class MetricsServiceImpl final : public MetricsService::Service
{
public:
    Status BatchWrite(ServerContext* /*ctx*/,
                      ServerReader<MetricPoint>* reader,
                      BatchResult* result) override
    {
        int accepted = 0;
        int rejected = 0;
        MetricPoint pt;

        while (reader->Read(&pt)) {
            // Simple validation: reject points with empty names
            if (pt.name().empty()) {
                ++rejected;
            } else {
                std::cout << "[server] received: " << pt.name()
                          << " = " << pt.value() << "\n";
                ++accepted;
            }
        }

        result->set_accepted(accepted);
        result->set_rejected(rejected);
        result->set_message("ingested " + std::to_string(accepted) + " points");

        return Status::OK;
    }
};

// ── Client ────────────────────────────────────────────────────────────
void run_client(const std::string& addr)
{
    auto stub = MetricsService::NewStub(
        grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));

    ClientContext ctx;
    BatchResult result;

    // ClientWriter owns the stream; WritesDone + Finish close it
    auto writer = stub->BatchWrite(&ctx, &result);

    // Send a batch of metric points
    std::vector<std::pair<std::string, double>> points = {
        {"cpu_usage", 72.1},
        {"mem_usage", 4096.0},
        {"disk_io",   1234.5},
        {"",          0.0},    // bad point → rejected by server
        {"net_rx",    999.9},
    };

    for (auto& [name, val] : points) {
        MetricPoint pt;
        pt.set_name(name);
        pt.set_value(val);
        pt.set_timestamp(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

        if (!writer->Write(pt)) {
            std::cerr << "[client] server closed stream early\n";
            break;
        }
    }

    writer->WritesDone();                  // signal end of upload
    Status s = writer->Finish();           // receive response + status

    if (s.ok()) {
        std::cout << "[client] accepted=" << result.accepted()
                  << " rejected=" << result.rejected()
                  << " msg=" << result.message() << "\n";
    } else {
        std::cerr << "[client] RPC failed: " << s.error_message() << "\n";
    }
}

int main()
{
    const std::string addr = "127.0.0.1:50053";

    MetricsServiceImpl svc;
    ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&svc);
    auto server = builder.BuildAndStart();
    std::cout << "[server] listening on " << addr << "\n";

    std::thread t([&addr] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        run_client(addr);
    });
    t.join();

    server->Shutdown();
}
