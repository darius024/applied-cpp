#include "metrics.grpc.pb.h"
#include "metrics.pb.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/alarm.h>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

// build: cmake -B build && cmake --build build --target 08_grpc_async

// ─────────────────────────────────────────────────────────────────────
// gRPC async server using CompletionQueue (CQ).
//
// Why async?
//   Sync server: 1 thread per in-flight RPC (blocks on I/O).
//   Async server: N threads drain a CQ; no thread is blocked.
//   A CQ event fires when a tag becomes "ready" (e.g. a new call
//   arrived, a response has been sent, etc.).
//
// Core API:
//   AsyncService  — generated alongside the sync Service; call
//     Request<Method>(ctx, req, responder, cq, notification_cq, tag)
//     to register interest in one incoming call.
//   CompletionQueue::Next(&tag, &ok) — blocks until an event arrives.
//     tag is whatever void* you passed to the RequestXxx call.
//     ok == false means the channel is shutting down.
//
// CallData state machine (one per in-flight RPC):
//   CREATE  — register interest in one new call
//   PROCESS — we have a request; compute response; send it
//   FINISH  — response sent; re-register to accept another call
//
// The pattern:
//   tag = this (CallData*), cast back via static_cast<CallData*>(tag)
//   Proceed() drives the state transition.
//   After FINISH, a new CallData is constructed to replace this one —
//   this is the standard gRPC async example pattern.
// ─────────────────────────────────────────────────────────────────────

using namespace metrics;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCompletionQueue;
using grpc::ServerAsyncResponseWriter;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;

// ── CallData: owns one async unary RPC lifecycle ─────────────────────
class CallData
{
public:
    CallData(MetricsService::AsyncService* svc, ServerCompletionQueue* cq)
        : svc_(svc), cq_(cq), responder_(&ctx_), state_(CREATE)
    {
        Proceed();
    }

    void Proceed()
    {
        if (state_ == CREATE) {
            state_ = PROCESS;
            // Register: "tell me when a GetMetric call arrives"
            // tag = this, so CQ delivers this pointer back to us
            svc_->RequestGetMetric(&ctx_, &req_, &responder_, cq_, cq_, this);

        } else if (state_ == PROCESS) {
            // A new call arrived — spawn a fresh CallData to keep listening
            new CallData(svc_, cq_);

            // Handle the request
            MetricPoint resp;
            resp.set_name(req_.name());
            resp.set_value(99.9);
            resp.set_timestamp(1700000042LL);

            std::cout << "[async-server] GetMetric: " << req_.name() << "\n";

            state_ = FINISH;
            responder_.Finish(resp, Status::OK, this);

        } else {
            // FINISH — CQ fired after Finish(); safe to delete
            delete this;
        }
    }

private:
    enum State { CREATE, PROCESS, FINISH };

    MetricsService::AsyncService*       svc_;
    ServerCompletionQueue*              cq_;
    ServerContext                       ctx_;
    MetricRequest                       req_;
    ServerAsyncResponseWriter<MetricPoint> responder_;
    State                               state_;
};

// ── Server thread pool ────────────────────────────────────────────────
void drain_queue(ServerCompletionQueue* cq)
{
    void* tag;
    bool  ok;
    while (cq->Next(&tag, &ok)) {
        // ok==false → server shutting down; tag may be invalid
        if (ok)
            static_cast<CallData*>(tag)->Proceed();
    }
}

int main()
{
    const std::string addr = "127.0.0.1:50055";

    MetricsService::AsyncService svc;
    ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&svc);

    auto cq     = builder.AddCompletionQueue();
    auto server = builder.BuildAndStart();
    std::cout << "[server] async listening on " << addr << "\n";

    // Seed one CallData — it will self-replicate after each handled call
    new CallData(&svc, cq.get());

    // Two worker threads draining the same CQ
    std::thread w1(drain_queue, cq.get());
    std::thread w2(drain_queue, cq.get());

    // Client in another thread
    std::thread client([&addr] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        auto stub = MetricsService::NewStub(
            grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));

        // Fire a few requests concurrently using async client API
        const int N = 4;
        std::vector<ClientContext>         ctxs(N);
        std::vector<MetricPoint>           resps(N);
        std::vector<grpc::Status>          statuses(N);
        CompletionQueue                    client_cq;
        std::vector<std::unique_ptr<grpc::ClientAsyncResponseReader<MetricPoint>>> readers;

        for (int i = 0; i < N; ++i) {
            MetricRequest req;
            req.set_name("cpu_" + std::to_string(i));
            readers.push_back(stub->AsyncGetMetric(&ctxs[i], req, &client_cq));
            readers.back()->Finish(&resps[i], &statuses[i],
                                   reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        }

        for (int done = 0; done < N; ++done) {
            void* tag; bool ok;
            client_cq.Next(&tag, &ok);
            int idx = static_cast<int>(reinterpret_cast<intptr_t>(tag));
            std::cout << "[client] resp[" << idx << "]: "
                      << resps[idx].name() << " = " << resps[idx].value() << "\n";
        }
    });

    client.join();

    server->Shutdown();
    cq->Shutdown();     // causes drain_queue threads to see Next()==false
    w1.join();
    w2.join();
}
