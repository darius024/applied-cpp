#include "metrics.grpc.pb.h"
#include "metrics.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

// build: cmake -B build && cmake --build build --target 07_grpc_bidi_stream

// ─────────────────────────────────────────────────────────────────────
// gRPC bidirectional streaming: both sides send independent streams.
//
// Proto:  rpc Monitor (stream MetricRequest) returns (stream MetricPoint)
//
// Server: receives ServerReaderWriter<Response, Request>*.
//   stream->Read(&req)   — receive from client
//   stream->Write(resp)  — send to client
//   Both directions are independent; ordering within each is preserved.
//   The method returns Status when done writing; this closes the stream.
//
// Client: stub call returns ClientReaderWriter<Request, Response>*.
//   Same Read/Write API, but roles are flipped.
//   writer->WritesDone() after sending all requests.
//   reader->Finish() blocks for final status.
//
// Typical patterns:
//   - sequential: read one, write one (ping-pong)
//   - split threads: one thread writes, another reads concurrently
//     → allows pipelining without head-of-line blocking
//
// Use cases: real-time dashboards subscribing to changing metric sets,
//   bidirectional control channels, game state sync.
// ─────────────────────────────────────────────────────────────────────

using namespace metrics;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;

// ── Service ───────────────────────────────────────────────────────────
class MetricsServiceImpl final : public MetricsService::Service
{
public:
    // Pattern: for each request received, emit `limit` metric points
    Status Monitor(ServerContext* ctx,
                   ServerReaderWriter<MetricPoint, MetricRequest>* stream) override
    {
        MetricRequest req;
        while (stream->Read(&req)) {
            if (ctx->IsCancelled()) break;

            std::cout << "[server] monitoring: " << req.name()
                      << " ×" << req.limit() << "\n";

            int n = req.limit() > 0 ? req.limit() : 3;
            for (int i = 0; i < n; ++i) {
                MetricPoint pt;
                pt.set_name(req.name());
                pt.set_value(10.0 * (i + 1));
                pt.set_timestamp(1700000000LL + i);

                if (!stream->Write(pt)) return Status::OK;
            }
        }
        return Status::OK;
    }
};

// ── Client ────────────────────────────────────────────────────────────
void run_client(const std::string& addr)
{
    auto stub = MetricsService::NewStub(
        grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));

    ClientContext ctx;
    auto stream = stub->Monitor(&ctx);

    // Use a separate reader thread so writes don't block on server responses
    std::thread reader([&stream] {
        MetricPoint pt;
        while (stream->Read(&pt)) {
            std::cout << "[client] " << pt.name()
                      << " = " << pt.value() << "\n";
        }
    });

    // Writer: subscribe to two different metrics
    for (auto& [name, limit] :
         std::vector<std::pair<std::string, int>>{{"cpu_usage", 3}, {"mem_usage", 2}})
    {
        MetricRequest req;
        req.set_name(name);
        req.set_limit(limit);
        if (!stream->Write(req)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stream->WritesDone();
    reader.join();

    Status s = stream->Finish();
    std::cout << "[client] bidi done, ok=" << std::boolalpha << s.ok() << "\n";
}

int main()
{
    const std::string addr = "127.0.0.1:50054";

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
