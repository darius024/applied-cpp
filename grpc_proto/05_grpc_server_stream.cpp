#include "metrics.grpc.pb.h"
#include "metrics.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

// build: cmake -B build && cmake --build build --target 05_grpc_server_stream

// ─────────────────────────────────────────────────────────────────────
// gRPC server-side streaming: one request → stream of responses.
//
// Proto:  rpc StreamMetrics (MetricRequest) returns (stream MetricPoint)
//
// Server: receives a ServerWriter<Response>* instead of a response ptr.
//   writer->Write(msg) sends one message downstream.
//   When the method returns Status::OK the stream is closed cleanly.
//
// Client: receives a ClientReader<Response>* from the stub call.
//   reader->Read(&msg) returns true while messages are available,
//   false when the server has closed the stream.
//   Finish() retrieves the final Status after Read returns false.
//
// Use cases: live metric feeds, log tailing, large result pagination,
//   real-time notifications.
// ─────────────────────────────────────────────────────────────────────

using namespace metrics;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::ClientContext;
using grpc::Status;

// ── Service ───────────────────────────────────────────────────────────
class MetricsServiceImpl final : public MetricsService::Service
{
public:
    Status StreamMetrics(ServerContext* ctx,
                         const MetricRequest* req,
                         ServerWriter<MetricPoint>* writer) override
    {
        std::cout << "[server] StreamMetrics: " << req->name()
                  << " limit=" << req->limit() << "\n";

        int limit = req->limit() > 0 ? req->limit() : 10;

        for (int i = 0; i < limit; ++i) {
            // Check if client cancelled — avoids wasted work
            if (ctx->IsCancelled()) {
                std::cout << "[server] client cancelled\n";
                return Status::CANCELLED;
            }

            MetricPoint pt;
            pt.set_name(req->name());
            pt.set_value(50.0 + i * 0.5);          // simulated reading
            pt.set_timestamp(1700000000LL + i);
            (*pt.mutable_labels())["seq"] = std::to_string(i);

            if (!writer->Write(pt)) break; // client disconnected mid-stream

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
    MetricRequest req;
    req.set_name("mem_usage");
    req.set_limit(5);

    auto reader = stub->StreamMetrics(&ctx, req);

    MetricPoint pt;
    int count = 0;
    while (reader->Read(&pt)) {
        std::cout << "[client] " << pt.name()
                  << " = " << pt.value()
                  << " ts=" << pt.timestamp() << "\n";
        ++count;
    }

    Status s = reader->Finish();
    std::cout << "[client] stream done: " << count
              << " points, ok=" << std::boolalpha << s.ok() << "\n";
}

int main()
{
    const std::string addr = "127.0.0.1:50052";

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
