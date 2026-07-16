#include <catch2/catch_test_macros.hpp>
#include <entt/signal/dispatcher.hpp>

#include <string>
#include <vector>

namespace
{
struct ConnectedEventProbe
{
};

struct CellEventProbe
{
};

struct Listener
{
    entt::dispatcher& Dispatcher;
    std::vector<std::string>& Calls;

    void OnFirst(const ConnectedEventProbe&)
    {
        Calls.emplace_back("connected.first.begin");
        Dispatcher.trigger(CellEventProbe{});
        Calls.emplace_back("connected.first.done");
    }

    void OnCell(const CellEventProbe&) { Calls.emplace_back("cell"); }

    void OnSecond(const ConnectedEventProbe&) { Calls.emplace_back("connected.second"); }
};
} // namespace

TEST_CASE("pinned EnTT dispatcher preserves registration and nested event order")
{
    entt::dispatcher dispatcher;
    std::vector<std::string> calls;
    Listener listener{dispatcher, calls};

    dispatcher.sink<ConnectedEventProbe>().connect<&Listener::OnFirst>(&listener);
    dispatcher.sink<CellEventProbe>().connect<&Listener::OnCell>(&listener);
    dispatcher.sink<ConnectedEventProbe>().connect<&Listener::OnSecond>(&listener);

    dispatcher.trigger(ConnectedEventProbe{});

    const std::vector<std::string> expected{
        "connected.first.begin",
        "cell",
        "connected.first.done",
        "connected.second",
    };
    REQUIRE(calls == expected);
}
