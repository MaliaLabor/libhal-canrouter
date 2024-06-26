// Copyright 2024 Khalil Estell
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <libhal-canrouter/can_router.hpp>

#include <algorithm>

#include <libhal-util/can.hpp>
#include <libhal/error.hpp>
#include <libhal/functional.hpp>

#include <boost/ut.hpp>

namespace hal {
namespace {
class mock_can : public hal::can
{
public:
  settings m_settings{};
  message_t m_message{};
  std::function<handler> m_handler{};
  bool m_return_error_status{ false };
  size_t m_on_receive_call_count = 0;
  bool m_noop_set = false;

private:
  void driver_configure(const settings& p_settings) override
  {
    m_settings = p_settings;
    if (m_return_error_status) {
      return hal::safe_throw(hal::operation_not_supported(this));
    }
  };

  void driver_bus_on() override
  {
  }

  void driver_send(const message_t& p_message) override
  {
    m_message = p_message;
    if (m_return_error_status) {
      return hal::safe_throw(hal::unknown(this));
    }
  };

  void driver_on_receive(hal::callback<handler> p_handler) override
  {
    m_on_receive_call_count++;
    m_handler = p_handler;
  };
};
}  // namespace

void can_router_test()
{
  using namespace boost::ut;

  "operator==(can::settings)"_test = []() {
    can::settings a{};
    can::settings b{};

    expect(a == b);
  };

  "operator!=(can::settings)"_test = []() {
    can::settings a{ .baud_rate = 100.0_kHz };
    can::settings b{ .baud_rate = 1200.0_kHz };

    expect(a != b);
  };

  "can_router::bus()"_test = []() {
    // Setup
    static constexpr can::message_t expected{
      .id = 0x111,
      .payload = { 0xAA, 0xBB, 0xCC },
      .length = 3,
    };
    mock_can mock;
    can_router router(mock);

    // Exercise
    router.bus().send(expected);

    // Verify
    expect(expected == mock.m_message);
  };

  "can_router::bus() failed"_test = []() {
    // Setup
    static constexpr can::message_t expected{
      .id = 0x111,
      .payload = { 0xAA, 0xBB, 0xCC },
      .length = 3,
    };
    mock_can mock;
    can_router router(mock);
    mock.m_return_error_status = true;

    // Exercise
    expect(throws<hal::unknown>([&]() { router.bus().send(expected); }));

    // Verify
    expect(expected == mock.m_message);
  };

  "can_router::add_message_callback(id)"_test = []() {
    // Setup
    static constexpr can::id_t id = 0x15;
    mock_can mock;
    can_router router(mock);

    // Exercise
    auto callback_item = router.add_message_callback(id);

    // Verify
    expect(that % id == callback_item.get().id);
    expect(that % 1 == router.handlers().size());

    const auto& iterator = std::find_if(
      router.handlers().begin(),
      router.handlers().cend(),
      [](const can_router::route& p_route) { return p_route.id == id; });
    expect(iterator->id == callback_item.get().id);
  };

  "can_router::add_message_callback(id, callback)"_test = []() {
    // Setup
    static constexpr can::id_t id = 0x15;
    static constexpr can::message_t expected{
      .id = 0x111,
      .payload = { 0xAA, 0xBB, 0xCC },
      .length = 3,
    };
    mock_can mock;
    can_router router(mock);
    int counter = 0;
    can::message_t actual{};

    // Exercise
    auto callback_item = router.add_message_callback(
      id, [&counter, &actual](const can::message_t& p_message) {
        counter++;
        actual = p_message;
      });

    const auto& iterator = std::find_if(
      router.handlers().begin(),
      router.handlers().cend(),
      [](const can_router::route& p_route) { return p_route.id == id; });

    iterator->handler(expected);

    // Verify
    expect(that % id == callback_item.get().id);
    expect(that % 1 == router.handlers().size());
    expect(iterator->id == callback_item.get().id);
    expect(that % 1 == counter);
    expect(expected == actual);
  };

  "can_router::add_message_callback(id, callback)"_test = []() {
    // Setup
    mock_can mock;
    can_router router(mock);
    int counter1 = 0;
    int counter2 = 0;
    int counter3 = 0;
    static constexpr can::message_t expected1{
      .id = 0x100,
      .payload = { 0xAA, 0xBB },
      .length = 2,
    };
    static constexpr can::message_t expected2{
      .id = 0x120,
      .payload = { 0xCC, 0xDD },
      .length = 2,
    };
    static constexpr can::message_t expected3{
      .id = 0x123,
      .payload = { 0xEE, 0xFF },
      .length = 2,
    };

    can::message_t actual1{};
    can::message_t actual2{};
    can::message_t actual3{};

    auto message_handler1 = router.add_message_callback(
      expected1.id, [&counter1, &actual1](const can::message_t& p_message) {
        counter1++;
        actual1 = p_message;
      });

    auto message_handler2 = router.add_message_callback(
      expected2.id, [&counter2, &actual2](const can::message_t& p_message) {
        counter2++;
        actual2 = p_message;
      });

    auto message_handler3 = router.add_message_callback(
      expected3.id, [&counter3, &actual3](const can::message_t& p_message) {
        counter3++;
        actual3 = p_message;
      });

    // Exercise
    router(expected1);

    // Verify
    expect(that % 3 == router.handlers().size());
    expect(that % 1 == counter1);
    expect(expected1 == actual1);
    expect(that % 0 == counter2);
    expect(expected2 != actual2);
    expect(that % 0 == counter2);
    expect(expected3 != actual3);

    router(expected2);

    expect(that % 1 == counter1);
    expect(expected1 == actual1);
    expect(that % 1 == counter2);
    expect(expected2 == actual2);
    expect(that % 0 == counter3);
    expect(expected3 != actual3);

    router(expected3);

    expect(that % 1 == counter1);
    expect(expected1 == actual1);
    expect(that % 1 == counter2);
    expect(expected2 == actual2);
    expect(that % 1 == counter2);
    expect(expected3 == actual3);

    router(expected2);

    expect(that % 1 == counter1);
    expect(expected1 == actual1);
    expect(that % 2 == counter2);
    expect(expected2 == actual2);
    expect(that % 1 == counter3);
    expect(expected3 == actual3);
  };

  "can_router::~can_router()"_test = []() {
    // Setup
    mock_can mock;
    {
      can_router router(mock);
      // m_on_receive_call_count = 1 on construction
      expect(that % 1 == mock.m_on_receive_call_count);
      // Exercise: end of scope calls destructor
    }

    // Verify
    expect(that % 2 == mock.m_on_receive_call_count);
  };

  "can_router::can_router(&&) move constructor"_test = []() {
    // Setup
    mock_can mock;

    {
      can_router original_router(mock);
      expect(that % 1 == mock.m_on_receive_call_count);

      auto new_router = std::move(original_router);
      expect(that % 2 == mock.m_on_receive_call_count);
    }

    // Exercise: end of scope calls destructor
    // Verify
    expect(that % 3 == mock.m_on_receive_call_count);
  };

  "can_router::add_message_callback(id, callback) with move"_test = []() {
    // Setup
    mock_can mock;
    can_router to_be_moved_router(mock);
    auto router = std::move(to_be_moved_router);
    int counter1 = 0;
    int counter2 = 0;
    int counter3 = 0;
    static constexpr can::message_t expected1{
      .id = 0x100,
      .payload = { 0xAA, 0xBB },
      .length = 2,
    };
    static constexpr can::message_t expected2{
      .id = 0x120,
      .payload = { 0xCC, 0xDD },
      .length = 2,
    };
    static constexpr can::message_t expected3{
      .id = 0x123,
      .payload = { 0xEE, 0xFF },
      .length = 2,
    };

    can::message_t actual1{};
    can::message_t actual2{};
    can::message_t actual3{};

    auto message_handler1 = router.add_message_callback(
      expected1.id, [&counter1, &actual1](const can::message_t& p_message) {
        counter1++;
        actual1 = p_message;
      });

    auto message_handler2 = router.add_message_callback(
      expected2.id, [&counter2, &actual2](const can::message_t& p_message) {
        counter2++;
        actual2 = p_message;
      });

    auto message_handler3 = router.add_message_callback(
      expected3.id, [&counter3, &actual3](const can::message_t& p_message) {
        counter3++;
        actual3 = p_message;
      });

    // Exercise
    router(expected1);

    // Verify
    expect(that % 3 == router.handlers().size());
    expect(that % 1 == counter1);
    expect(expected1 == actual1);
    expect(that % 0 == counter2);
    expect(expected2 != actual2);
    expect(that % 0 == counter2);
    expect(expected3 != actual3);

    router(expected2);

    expect(that % 1 == counter1);
    expect(expected1 == actual1);
    expect(that % 1 == counter2);
    expect(expected2 == actual2);
    expect(that % 0 == counter3);
    expect(expected3 != actual3);

    router(expected3);

    expect(that % 1 == counter1);
    expect(expected1 == actual1);
    expect(that % 1 == counter2);
    expect(expected2 == actual2);
    expect(that % 1 == counter2);
    expect(expected3 == actual3);

    router(expected2);

    expect(that % 1 == counter1);
    expect(expected1 == actual1);
    expect(that % 2 == counter2);
    expect(expected2 == actual2);
    expect(that % 1 == counter3);
    expect(expected3 == actual3);
  };
};
}  // namespace hal