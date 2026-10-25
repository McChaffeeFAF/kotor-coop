/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include "reone/game/coopjoinqueue.h"

using namespace reone::game;

namespace {

// The host's prompt: up for the queue's next request when nothing else is up,
// as CoopSession::hostPromptJoin does with the confirm pop-up.
struct Popup {
    uint64_t serial {0};
    bool visible {false};
    uint32_t about {0};

    void show(uint32_t client) {
        ++serial;
        visible = true;
        about = client;
    }
    void hide() {
        ++serial;
        visible = false;
        about = 0;
    }
};

void promptStep(CoopJoinQueue &queue, Popup &popup) {
    queue.checkPrompt(popup.visible, popup.serial);
    if (popup.visible) {
        return;
    }
    if (uint32_t next = queue.next()) {
        popup.show(next);
        queue.shown(next, popup.serial);
    }
}

} // namespace

TEST(CoopJoinQueue, requests_on_the_same_tick_are_asked_one_at_a_time_in_order) {
    CoopJoinQueue queue;
    Popup popup;
    EXPECT_TRUE(queue.request(3, 1.0));
    EXPECT_TRUE(queue.request(1, 1.0));
    EXPECT_TRUE(queue.request(2, 1.0));
    EXPECT_FALSE(queue.request(1, 2.0)) << "asking again keeps the place";
    EXPECT_EQ((std::vector<uint32_t> {3, 1, 2}), queue.clients());

    promptStep(queue, popup);
    EXPECT_EQ(3u, popup.about);
    EXPECT_EQ(3u, queue.prompted());
    EXPECT_EQ(0u, queue.next()) << "one prompt at a time";
    promptStep(queue, popup);
    EXPECT_EQ(3u, popup.about) << "the prompt up stays up";

    // The host presses Accept: the pop-up hides itself, then the answer.
    popup.hide();
    EXPECT_TRUE(queue.remove(3));
    promptStep(queue, popup);
    EXPECT_EQ(1u, popup.about);
    popup.hide();
    EXPECT_TRUE(queue.remove(1));
    promptStep(queue, popup);
    EXPECT_EQ(2u, popup.about);
    popup.hide();
    EXPECT_TRUE(queue.remove(2));
    promptStep(queue, popup);
    EXPECT_FALSE(popup.visible);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(0u, queue.lost());
}

TEST(CoopJoinQueue, a_prompt_taken_down_unanswered_is_asked_again_first) {
    CoopJoinQueue queue;
    Popup popup;
    queue.request(1, 0.0);
    queue.request(2, 0.0);
    promptStep(queue, popup);
    ASSERT_EQ(1u, popup.about);

    // A conversation starts: the screen change hides the pop-up.
    popup.hide();
    EXPECT_EQ(1u, queue.checkPrompt(popup.visible, popup.serial));
    EXPECT_EQ(1u, queue.lost());
    EXPECT_EQ(0u, queue.prompted());
    EXPECT_EQ(1u, queue.next()) << "the lost prompt keeps its turn";
    EXPECT_EQ(0u, queue.checkPrompt(popup.visible, popup.serial)) << "counted once";

    promptStep(queue, popup);
    EXPECT_EQ(1u, popup.about);
}

TEST(CoopJoinQueue, another_pop_up_in_its_place_counts_as_taken_down) {
    CoopJoinQueue queue;
    Popup popup;
    queue.request(5, 0.0);
    promptStep(queue, popup);
    ASSERT_EQ(5u, queue.prompted());

    // A tutorial message box replaces it: visible, but another serial.
    popup.show(0);
    EXPECT_EQ(5u, queue.checkPrompt(popup.visible, popup.serial));
    promptStep(queue, popup);
    EXPECT_EQ(0u, popup.about) << "nothing goes over the other pop-up";
    EXPECT_EQ(0u, queue.prompted());
    popup.hide();
    promptStep(queue, popup);
    EXPECT_EQ(5u, popup.about);
    EXPECT_EQ(5u, queue.prompted());
}

TEST(CoopJoinQueue, an_answer_elsewhere_is_for_its_own_client_only) {
    CoopJoinQueue queue;
    Popup popup;
    queue.request(1, 0.0);
    queue.request(2, 0.0);
    promptStep(queue, popup);
    ASSERT_EQ(1u, popup.about);

    // coopapprove 2: not the prompt that is up, which stays about client 1.
    EXPECT_FALSE(queue.remove(2));
    EXPECT_EQ(1u, queue.prompted());
    promptStep(queue, popup);
    EXPECT_EQ(1u, popup.about);
    EXPECT_EQ((std::vector<uint32_t> {1}), queue.clients());

    // coopapprove 1 while its prompt is up: the caller takes the pop-up down.
    EXPECT_TRUE(queue.remove(1));
    EXPECT_EQ(0u, queue.prompted());
    popup.hide();
    EXPECT_EQ(0u, queue.checkPrompt(popup.visible, popup.serial)) << "an answered prompt is not lost";
    EXPECT_EQ(0u, queue.lost());
}

TEST(CoopJoinQueue, a_player_who_leaves_while_asked_takes_the_prompt_along) {
    CoopJoinQueue queue;
    Popup popup;
    queue.request(1, 0.0);
    queue.request(2, 0.0);
    promptStep(queue, popup);
    EXPECT_TRUE(queue.remove(1)); // disconnected: the session hides the pop-up
    popup.hide();
    promptStep(queue, popup);
    EXPECT_EQ(2u, popup.about);
    EXPECT_FALSE(queue.remove(9)) << "an unknown client changes nothing";
    EXPECT_EQ(2u, queue.prompted());
}

TEST(CoopJoinQueue, a_shown_call_for_a_request_not_queued_is_ignored) {
    CoopJoinQueue queue;
    queue.shown(4, 7);
    EXPECT_EQ(0u, queue.prompted());
    EXPECT_FALSE(queue.request(0, 0.0)) << "client 0 is nobody";
    EXPECT_TRUE(queue.empty());
}

TEST(CoopJoinQueue, overdue_requests_are_reported_once_per_interval) {
    CoopJoinQueue queue;
    queue.request(1, 0.0);
    queue.request(2, 30.0);
    EXPECT_TRUE(queue.overdue(59.0, 60.0).empty());
    EXPECT_EQ((std::vector<uint32_t> {1}), queue.overdue(60.0, 60.0));
    EXPECT_TRUE(queue.overdue(61.0, 60.0).empty()) << "reported already";
    EXPECT_EQ((std::vector<uint32_t> {2}), queue.overdue(90.0, 60.0));
    EXPECT_EQ((std::vector<uint32_t> {1}), queue.overdue(120.0, 60.0));
    EXPECT_DOUBLE_EQ(120.0, queue.waited(1, 120.0));
    EXPECT_DOUBLE_EQ(90.0, queue.waited(2, 120.0));
    EXPECT_DOUBLE_EQ(0.0, queue.waited(3, 120.0));
}

TEST(CoopJoinQueue, a_burst_of_requests_with_interruptions_loses_none_and_misattributes_none) {
    // Four players, three on one tick and one later; the prompt is taken
    // down twice; answers come from the prompt and from coopapprove.
    CoopJoinQueue queue;
    Popup popup;
    std::vector<uint32_t> answered;
    auto acceptPrompt = [&]() {
        uint32_t about = popup.about;
        popup.hide();
        ASSERT_EQ(about, queue.prompted()) << "the prompt names the request it answers";
        queue.remove(about);
        answered.push_back(about);
    };
    queue.request(1, 0.0);
    queue.request(2, 0.0);
    queue.request(3, 0.0);
    promptStep(queue, popup);
    popup.hide(); // the opening conversation
    promptStep(queue, popup);
    acceptPrompt(); // 1
    promptStep(queue, popup);
    queue.request(4, 5.0); // during a module load
    popup.hide();          // the load screen
    promptStep(queue, popup);
    EXPECT_EQ(2u, popup.about) << "the interrupted request is asked again before newer ones";
    queue.remove(3); // coopapprove 3
    acceptPrompt();  // 2
    promptStep(queue, popup);
    acceptPrompt(); // 4
    promptStep(queue, popup);
    EXPECT_EQ((std::vector<uint32_t> {1, 2, 4}), answered);
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(2u, queue.lost());
    EXPECT_FALSE(popup.visible);
}

TEST(CoopJoinQueue, same_tick_requests_queue_in_the_order_their_messages_arrived) {
    // The host visits its clients in connection-handle order, not arrival order.
    CoopJoinQueue queue;
    Popup popup;
    queue.request(1, 0.0, 10); // Alice's character arrived first
    queue.request(3, 0.0, 12); // Cara's third
    queue.request(2, 0.0, 11); // Bob's second
    EXPECT_EQ((std::vector<uint32_t> {1, 2, 3}), queue.clients());
    promptStep(queue, popup);
    ASSERT_EQ(1u, popup.about);

    // An older arrival that became a request later never goes ahead of the prompt up.
    queue.request(7, 1.0, 5);
    EXPECT_EQ((std::vector<uint32_t> {1, 7, 2, 3}), queue.clients());
    // Unknown order: the back.
    queue.request(9, 1.0);
    EXPECT_EQ((std::vector<uint32_t> {1, 7, 2, 3, 9}), queue.clients());
    // A later arrival goes after every earlier one; an unknown one already
    // queued counts as having come first.
    queue.request(4, 1.0, 13);
    EXPECT_EQ((std::vector<uint32_t> {1, 7, 2, 3, 9, 4}), queue.clients());
}
