/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include "../../src/apps/engine/controlprotocol.h"

using namespace reone;

TEST(control_protocol, should_format_reply_as_single_json_line) {
    // given
    ControlReply reply {7, "say \"hi\"", false, {"line one", "tab\there"}};

    // when
    auto line = formatControlReply(reply, 1234, 20.5);

    // then
    EXPECT_EQ(std::string::npos, line.find('\n'));
    auto obj = boost::json::parse(line).as_object();
    EXPECT_EQ(7, obj.at("id").to_number<int64_t>());
    EXPECT_EQ("say \"hi\"", obj.at("cmd").as_string());
    EXPECT_FALSE(obj.at("ok").as_bool());
    auto &output = obj.at("output").as_array();
    ASSERT_EQ(2u, output.size());
    EXPECT_EQ("line one", output[0].as_string());
    EXPECT_EQ("tab\there", output[1].as_string());
    EXPECT_EQ(1234, obj.at("frame").to_number<int64_t>());
    EXPECT_DOUBLE_EQ(20.5, obj.at("time").to_number<double>());
}

TEST(control_protocol, should_format_empty_output_as_empty_array) {
    auto obj = boost::json::parse(formatControlReply(ControlReply {1, "quit", true, {}}, 0, 0.0)).as_object();
    EXPECT_TRUE(obj.at("ok").as_bool());
    EXPECT_TRUE(obj.at("output").as_array().empty());
}

TEST(control_protocol, should_split_lines_across_chunks) {
    // given
    ControlLineSplitter splitter;

    // when
    splitter.append("pi");
    auto none = splitter.next();
    splitter.append("ng\r\n\n  pause 5 \nqu");
    auto first = splitter.next();
    auto second = splitter.next();
    auto third = splitter.next();
    splitter.append("it\n");
    auto fourth = splitter.next();

    // then
    EXPECT_FALSE(none);
    EXPECT_EQ("ping", first.value_or(""));
    EXPECT_EQ("pause 5", second.value_or(""));
    EXPECT_FALSE(third);
    EXPECT_EQ("quit", fourth.value_or(""));
    EXPECT_FALSE(splitter.next());
}

TEST(control_protocol, should_discard_overlong_partial_line) {
    ControlLineSplitter splitter;
    splitter.append(std::string(ControlLineSplitter::kMaxLineLength + 1, 'x'));
    EXPECT_FALSE(splitter.next());
    splitter.append("ping\n");
    EXPECT_EQ("ping", splitter.next().value_or(""));
}

TEST(control_protocol, should_queue_commands_in_order_with_increasing_ids) {
    // given
    ControlQueue queue;
    EXPECT_TRUE(queue.empty());

    // when
    auto id1 = queue.push("ping");
    auto id2 = queue.push("pause 10");
    auto first = queue.pop();
    auto second = queue.pop();

    // then
    EXPECT_EQ(1u, id1);
    EXPECT_EQ(2u, id2);
    ASSERT_TRUE(first);
    EXPECT_EQ(1u, first->id);
    EXPECT_EQ("ping", first->command);
    ASSERT_TRUE(second);
    EXPECT_EQ(2u, second->id);
    EXPECT_EQ("pause 10", second->command);
    EXPECT_FALSE(queue.pop());
    EXPECT_TRUE(queue.empty());
}

TEST(control_protocol, should_accept_pushes_from_another_thread) {
    ControlQueue queue;
    std::thread producer([&queue]() {
        for (int i = 0; i < 1000; ++i) {
            queue.push("cmd " + std::to_string(i));
        }
    });
    producer.join();
    for (int i = 0; i < 1000; ++i) {
        auto request = queue.pop();
        ASSERT_TRUE(request);
        EXPECT_EQ("cmd " + std::to_string(i), request->command);
    }
}
