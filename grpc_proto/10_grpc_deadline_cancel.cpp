#include "metrics.grpc.pb.h"
#include "metrics.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

// build: cmake -B build && cmake --build build --target 10_grpc_deadline_cancel

// ─────────────────────────────────────────────────────────────────────
// Deadlines and cancellation.
//
// Deadline:
//   Every RPC can carry an absolute deadline (wall-clock time).
//   Client: ctx.set_deadline(std::chrono::system_clock::now() + 2s)
//   If the server has not responded by then, the client receives
//   StatusCode::DEADLINE_EXCEEDED automatically.
//   The deadline is propagated to the server; the server can check
//   ctx->IsCancelled() (which returns true when deadline fires or
//   the client explicitly cancels).
//
// Cancellation:
//   Client calls ctx.TryCancel() to abort an in-flight RPC.
//   Server receives IsCancelled()==true on the next blocking call
//   (e.g. Read(), Write(), or the RPC handler itself).
//   After TryCancel() the client must still call Finish() to drain
//   the stream; it will receive StatusCode::CANCELLED.
//
// Best practices:
//   - Always set a deadline on client RPCs in production.
//   - On server, check IsCancelled() in loops to avoid wasted work.
//   - For streaming, check IsCancelled() before each Write().
// ─────────────────────────────────────────────────────────────────────

using namespace metrics;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::ClientContext;
using grpc::Status;
using namespace std::chrono_literals;

// ── Service ───────────────────────────────────────────────────────────
class MetricsServiceImpl final : public MetricsService::Service
{
public:
    // Slow unary — simulates a long-running computation
    Status GetMetric(ServerContext* ctx,
                     const MetricRequest* req,
                     MetricPoint* resp) override
    {
        std::cout << "[server] GetMetric: " << req->name() << "\n";

        // Simulate heavy work in chunks so we can honour cancellation
        for (int i = 0; i < 20; ++i) {
            if (ctx->IsCancelled()) {
                std::cout << "[server] cancelled after " << i << " steps\n";
                return Status::CANCELLED;
            }
            std::this_thread::sleep_for(50ms);
        }

        resp->set_name(req->name());
        resp->set_value(42.0);
        return Status::OK;
    }

    // Streaming — emits points with a delay between each
    Status StreamMetrics(ServerContext* ctx,
                         const MetricRequest* req,
                         ServerWriter<MetricPoint>* writer) override
    {
        for (int i = 0; ; ++i) {
            if (ctx->IsCancelled()) {
                std::cout << "[server] stream cancelled at i=" << i << "\n";
                return Status::CANCELLED;
            }

            MetricPoint pt;
            pt.set_name(req->name());
            pt.set_value(i * 1.0);
            pt.set_timestamp(1700000000LL + i);

            if (!writer->Write(pt)) break;
            std::this_thread::sleep_for(200ms); // slow producer
        }
        return Status::OK;
    }
};

// ── Demo 1: deadline exceeded on slow unary ───────────────────────────
void demo_deadline(MetricsService::Stub* stub)
{
    ClientContext ctx;
    // Deadline shorter than the server's simulated work (20 × 50ms = 1s)
    ctx.set_deadline(std::chrono::system_clock::now() + 300ms);

    MetricRequest req;
    MetricPoint   resp;
    req.set_name("slow_metric");

    Status s = stub->GetMetric(&ctx, req, &resp);

    std::cout << "[deadline demo] ok=" << std::boolalpha << s.ok()
              << " code=" << s.error_code();

    if (s.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED)
        std::cout << " → DEADLINE_EXCEEDED (expected)\n";
    else
        std::cout << "\n";
}

// ── Demo 2: client-initiated TryCancel on streaming RPC ──────────────
void demo_cancel(MetricsService::Stub* stub)
{
    ClientContext ctx;
    MetricRequest req;
    req.set_name("cancel_metric");

    auto reader = stub->StreamMetrics(&ctx, req);

    // Read a couple of points then cancel
    MetricPoint pt;
    int count = 0;
    while (reader->Read(&pt)) {
        std::cout << "[cancel demo] got pt " << count << " = " << pt.value() << "\n";
        ++count;
        if (count == 2) {
            std::cout << "[cancel demo] calling TryCancel()\n";
            ctx.TryCancel();
        }
    }

    Status s = reader->Finish();
    std::cout << "[cancel demo] final code=" << s.error_code();
    if (s.error_code() == grpc::StatusCode::CANCELLED)
        std::cout << " → CANCELLED (expected)\n";
    else
        std::cout << "\n";
}

int main()
{
    const std::string addr = "127.0.0.1:50057";

    MetricsServiceImpl svc;
    ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&svc);
    auto server = builder.BuildAndStart();
    std::cout << "[server] listening on " << addr << "\n";

    std::thread t([&addr] {
        std::this_thread::sleep_for(50ms);

        auto stub = MetricsService::NewStub(
            grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));

        demo_deadline(stub.get());
        demo_cancel(stub.get());
    });

    t.join();
    server->Shutdown();
}
