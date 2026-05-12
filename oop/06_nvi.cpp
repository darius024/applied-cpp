#include <iostream>
#include <string>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────
// Non-Virtual Interface (NVI) pattern.
//
// Problem with making the public API virtual:
//   - Derived classes can override it and bypass invariants (pre/post-
//     conditions, logging, locking) that the base wants to enforce.
//   - The base class loses control of how the interface is called.
//
// NVI solution:
//   - Public functions are NON-virtual — the interface is stable.
//   - They delegate to PRIVATE virtual hooks — the extension point.
//   - The base enforces all invariants in the public function;
//     derived classes only customise the core behaviour.
// ─────────────────────────────────────────────────────────────────────

class DataExporter
{
public:
    virtual ~DataExporter() = default;

    // Public, non-virtual. The template for the operation.
    // Derived classes cannot bypass the pre/post-conditions here.
    void export_data(const std::string& data)
    {
        if (data.empty())
            throw std::invalid_argument("data must not be empty");

        std::cout << "[exporter] starting export\n";
        do_export(data);                  // call the private virtual hook
        std::cout << "[exporter] export complete\n";
    }

private:
    // Private virtual — derived classes override this to customise behaviour,
    // but they cannot call it directly; only the base's public method can.
    virtual void do_export(const std::string& data) = 0;
};

// ─────────────────────────────────────────────────────────────────────
// Concrete exporters only implement the hook, not the full protocol.
// ─────────────────────────────────────────────────────────────────────

class CsvExporter : public DataExporter
{
private:
    void do_export(const std::string& data) override
    {
        std::cout << "[csv] writing: " << data << "\n";
    }
};

class JsonExporter : public DataExporter
{
private:
    void do_export(const std::string& data) override
    {
        std::cout << "[json] writing: {\"data\":\"" << data << "\"}\n";
    }
};

// ─────────────────────────────────────────────────────────────────────
// NVI with a non-pure virtual hook + default behaviour.
// Derived classes can optionally override; the base provides a default.
// ─────────────────────────────────────────────────────────────────────

class Report
{
public:
    virtual ~Report() = default;

    void generate()
    {
        std::cout << "=== Report: " << title() << " ===\n";
        do_generate();
        std::cout << "=== end ===\n";
    }

private:
    virtual std::string title()       const { return "Generic Report"; }
    virtual void        do_generate()       { std::cout << "(empty)\n"; }
};

class SalesReport : public Report
{
private:
    std::string title()    const override { return "Sales Q2"; }
    void        do_generate()    override { std::cout << "revenue: $42,000\n"; }
};

int main()
{
    CsvExporter  csv;
    JsonExporter json;

    csv.export_data("name,age\nAlice,30");
    std::cout << "\n";
    json.export_data("Alice");
    std::cout << "\n";

    Report      generic;
    SalesReport sales;
    generic.generate();
    std::cout << "\n";
    sales.generate();

    // Pre-condition enforced by the base regardless of derived class:
    try {
        csv.export_data("");
    } catch (const std::invalid_argument& e) {
        std::cout << "\ncaught: " << e.what() << "\n";
    }
}
