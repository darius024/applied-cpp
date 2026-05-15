#include "metrics.grpc.pb.h"
#include "metrics.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

// build: cmake -B build && cmake --build build --target 04_grpc_unary

// ─────────────────────────────────────────────────────────────────────
// gRPC unary RPC: one request → one response.
//
// Server side:
//   Subclass the generated Service base and override each RPC method.
//   The method receives a ServerContext*, the request message, and a
//   response message pointer. Populate the response and return Status.
//
// Client side:
//   Create a Channel (connection) to the server address.
//   Instantiate a Stub from the channel — cheap, can be shared.
//   Call the method on the stub with a ClientContext and messages.
//   Check status.ok() before using the response.
//
// ServerBuilder: fluent API to configure the server before starting.
//   AddListeningPort, RegisterService, BuildAndStart.
// ─────────────────────────────────────────────────────────────────────

using namespace metrics;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

// ── Service implementation ────────────────────────────────────────────
class MetricsServiceImpl final : public MetricsService::Service
{
public:
    // Override only the unary RPC for this file
    Status GetMetric(ServerContext* /*ctx*/,
                     const MetricRequest* req,
                     MetricPoint* resp) override
    {
        std::cout << "[server] GetMetric: name=" << req->name() << "\n";

        // Simulate a lookup — in production: query a time-series store
        if (req->name().empty())
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "name required");

        resp->set_name(req->name());
        resp->set_value(42.7);
        resp->set_timestamp(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        (*resp->mutable_labels())["host"] = "node-1";

        return Status::OK;
    }
};

// ── Server ────────────────────────────────────────────────────────────
std::unique_ptr<Server> start_server(const std::string& addr,
                                     MetricsServiceImpl& svc)
{
    ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&svc);
    auto server = builder.BuildAndStart();
    std::cout << "[server] listening on " << addr << "\n";
    return server;
}

// ── Client ────────────────────────────────────────────────────────────
void run_client(const std::string& addr)
{
    // Channel is a long-lived connection — reuse the same one for all calls
    auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
    auto stub    = MetricsService::NewStub(channel);

    // ── Successful call ───────────────────────────────────────────────
    {
        ClientContext ctx;
        MetricRequest req;
        MetricPoint   resp;
        req.set_name("cpu_usage");

        Status s = stub->GetMetric(&ctx, req, &resp);
        if (s.ok()) {
            std::cout << "[client] " << resp.name()
                      << " = " << resp.value()
                      << " ts=" << resp.timestamp() << "\n";
        } else {
            std::cerr << "[client] error: " << s.error_message() << "\n";
        }
    }

    // ── Error case: empty name ────────────────────────────────────────
    {
        ClientContext ctx;
        MetricRequest req; // name intentionally left empty
        MetricPoint   resp;

        Status s = stub->GetMetric(&ctx, req, &resp);
        std::cout << "[client] empty name → code="
                  << s.error_code()
                  << " msg=" << s.error_message() << "\n";
    }
}

int main()
{
    const std::string addr = "127.0.0.1:50051";

    MetricsServiceImpl svc;
    auto server = start_server(addr, svc);

    // Run client in a thread while server is live
    std::thread t([&addr] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        run_client(addr);
    });
    t.join();

    server->Shutdown();
}
