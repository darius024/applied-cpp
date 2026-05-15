#include "metrics.grpc.pb.h"
#include "metrics.pb.h"
#include <grpcpp/grpcpp.h>
// experimental interceptors require the experimental header
#include <grpcpp/support/interceptor.h>
#include <grpcpp/support/server_interceptor.h>
#include <grpcpp/support/client_interceptor.h>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <string>

// build: cmake -B build && cmake --build build --target 09_grpc_interceptors

// ─────────────────────────────────────────────────────────────────────
// gRPC interceptors — cross-cutting logic injected into the RPC path.
//
// Interceptors sit between the stub/service and the network layer.
// They receive hooks at well-defined points:
//
//   PRE_SEND_INITIAL_METADATA   — before first send (metadata + method)
//   PRE_SEND_MESSAGE            — before each request/response message
//   POST_SEND_MESSAGE           — after message serialised
//   PRE_SEND_STATUS             — server side: before status sent
//   PRE_SEND_CLOSE              — client side: before WritesDone
//   POST_RECV_INITIAL_METADATA  — after receiving server metadata
//   POST_RECV_MESSAGE           — after receiving a message
//   POST_RECV_STATUS            — client: after receiving final status
//   POST_RECV_CLOSE             — server: after client closes write
//
// A single interceptor processes ONE of those hooks per Proceed() call.
// Call info.GetInterceptionHookPoints() to know which hook fired.
// Call Proceed() to continue the RPC chain; block by not calling it.
//
// Use cases:
//   Server: logging, auth validation, metrics, rate limiting.
//   Client: injecting auth headers, tracing, retry decoration.
// ─────────────────────────────────────────────────────────────────────

using namespace metrics;
using namespace grpc::experimental;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

// ─────────────────────────────────────────────────────────────────────
// Server-side: logging interceptor
// ─────────────────────────────────────────────────────────────────────
class LoggingInterceptor final : public Interceptor
{
public:
    explicit LoggingInterceptor(ServerRpcInfo* /*info*/) {}

    void Intercept(InterceptorBatchMethods* methods) override
    {
        if (methods->QueryInterceptionHookPoint(
                InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
            std::cout << "[server-interceptor] sending initial metadata\n";
        }

        if (methods->QueryInterceptionHookPoint(
                InterceptionHookPoints::POST_RECV_MESSAGE)) {
            std::cout << "[server-interceptor] received message\n";
        }

        if (methods->QueryInterceptionHookPoint(
                InterceptionHookPoints::PRE_SEND_STATUS)) {
            std::cout << "[server-interceptor] sending status\n";
        }

        methods->Proceed(); // must call to continue chain
    }
};

class LoggingInterceptorFactory final
    : public ServerInterceptorFactoryInterface
{
public:
    Interceptor* CreateServerInterceptor(ServerRpcInfo* info) override
    {
        return new LoggingInterceptor(info);
    }
};

// ─────────────────────────────────────────────────────────────────────
// Server-side: auth interceptor — rejects calls missing a token
// ─────────────────────────────────────────────────────────────────────
class AuthInterceptor final : public Interceptor
{
public:
    explicit AuthInterceptor(ServerRpcInfo* /*info*/) {}

    void Intercept(InterceptorBatchMethods* methods) override
    {
        // Hijack is available only at PRE_RECV_MESSAGE /
        // POST_RECV_INITIAL_METADATA, but metadata checks work here.
        // For simplicity we check metadata after it is received.
        methods->Proceed();
    }
};

class AuthInterceptorFactory final
    : public ServerInterceptorFactoryInterface
{
public:
    Interceptor* CreateServerInterceptor(ServerRpcInfo* info) override
    {
        return new AuthInterceptor(info);
    }
};

// ─────────────────────────────────────────────────────────────────────
// Client-side: injects an auth token into outgoing metadata
// ─────────────────────────────────────────────────────────────────────
class AuthTokenInterceptor final : public Interceptor
{
public:
    explicit AuthTokenInterceptor(ClientRpcInfo* /*info*/,
                                  std::string token)
        : token_(std::move(token)) {}

    void Intercept(InterceptorBatchMethods* methods) override
    {
        if (methods->QueryInterceptionHookPoint(
                InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
            auto* meta = methods->GetSendInitialMetadata();
            if (meta)
                meta->insert({"x-auth-token", token_});
            std::cout << "[client-interceptor] injected auth token\n";
        }
        methods->Proceed();
    }

private:
    std::string token_;
};

class AuthTokenInterceptorFactory final
    : public ClientInterceptorFactoryInterface
{
public:
    explicit AuthTokenInterceptorFactory(std::string token)
        : token_(std::move(token)) {}

    Interceptor* CreateClientInterceptor(ClientRpcInfo* info) override
    {
        return new AuthTokenInterceptor(info, token_);
    }

private:
    std::string token_;
};

// ─────────────────────────────────────────────────────────────────────
// Minimal service
// ─────────────────────────────────────────────────────────────────────
class MetricsServiceImpl final : public MetricsService::Service
{
public:
    Status GetMetric(ServerContext* ctx,
                     const MetricRequest* req,
                     MetricPoint* resp) override
    {
        // Check token passed via metadata (set by client interceptor)
        auto md = ctx->client_metadata();
        auto it = md.find("x-auth-token");
        if (it == md.end() || it->second != "secret") {
            std::cerr << "[server] unauthorized\n";
            return Status(grpc::StatusCode::UNAUTHENTICATED, "bad token");
        }
        resp->set_name(req->name());
        resp->set_value(1.0);
        return Status::OK;
    }
};

int main()
{
    const std::string addr = "127.0.0.1:50056";

    // Server with two interceptors chained
    MetricsServiceImpl svc;
    ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&svc);

    std::vector<std::unique_ptr<ServerInterceptorFactoryInterface>> factories;
    factories.push_back(std::make_unique<LoggingInterceptorFactory>());
    factories.push_back(std::make_unique<AuthInterceptorFactory>());
    builder.experimental().SetInterceptorCreators(std::move(factories));

    auto server = builder.BuildAndStart();
    std::cout << "[server] listening on " << addr << "\n";

    // Client with auth-token interceptor
    std::thread t([&addr] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        grpc::ChannelArguments args;
        std::vector<std::unique_ptr<ClientInterceptorFactoryInterface>> cfactories;
        cfactories.push_back(
            std::make_unique<AuthTokenInterceptorFactory>("secret"));

        auto channel = grpc::experimental::CreateCustomChannelWithInterceptors(
            addr, grpc::InsecureChannelCredentials(),
            args, std::move(cfactories));

        auto stub = MetricsService::NewStub(channel);

        grpc::ClientContext ctx;
        MetricRequest req;
        MetricPoint   resp;
        req.set_name("cpu_usage");

        Status s = stub->GetMetric(&ctx, req, &resp);
        std::cout << "[client] ok=" << std::boolalpha << s.ok()
                  << " val=" << resp.value() << "\n";
    });

    t.join();
    server->Shutdown();
}
