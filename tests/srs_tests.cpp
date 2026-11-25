#include "core/srs_deck.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace selfano::core;

TEST_CASE("review_card applies 3-button SM-2 scheduling and enforces 1.3 ease floor", "[srs]") {
  const std::int64_t day0 = 20000;
  SrsCard card{
      .id = "gos_intro",
      .piece_name = "Gate of Steiner",
      .section_name = "Intro",
      .chord_roadmap_hint = "Dm -> Bb -> F -> C",
  };

  SECTION("Good progression: 1d -> 6d -> scaled by ease factor") {
    REQUIRE(preview_next_interval_days(card, SrsRating::Good, day0) == 1);
    card = review_card(card, SrsRating::Good, day0);
    REQUIRE(card.repetitions == 1);
    REQUIRE(card.interval_days == 1);
    REQUIRE(card.due_epoch_day == day0 + 1);
    REQUIRE(card.ease_factor >= 2.5);

    REQUIRE(preview_next_interval_days(card, SrsRating::Good, day0 + 1) == 6);
    card = review_card(card, SrsRating::Good, day0 + 1);
    REQUIRE(card.repetitions == 2);
    REQUIRE(card.interval_days == 6);
    REQUIRE(card.due_epoch_day == day0 + 7);

    const int expected_3 = preview_next_interval_days(card, SrsRating::Good, day0 + 7);
    REQUIRE(expected_3 >= 14);
    card = review_card(card, SrsRating::Good, day0 + 7);
    REQUIRE(card.repetitions == 3);
    REQUIRE(card.interval_days == expected_3);
    REQUIRE(card.due_epoch_day == day0 + 7 + expected_3);
  }

  SECTION("Hard progression grows conservatively and trims ease factor") {
    card = review_card(card, SrsRating::Hard, day0);
    REQUIRE(card.repetitions == 1);
    REQUIRE(card.interval_days == 1);
    REQUIRE(card.ease_factor < 2.5);

    card = review_card(card, SrsRating::Hard, day0 + 1);
    REQUIRE(card.repetitions == 2);
    REQUIRE(card.interval_days == 3);

    card = review_card(card, SrsRating::Hard, day0 + 4);
    REQUIRE(card.repetitions == 3);
    REQUIRE(card.interval_days >= 4);
  }

  SECTION("Again resets repetitions to 0, interval to 1, and clamps ease factor at 1.3") {
    card = review_card(card, SrsRating::Good, day0);
    card = review_card(card, SrsRating::Good, day0 + 1);
    REQUIRE(card.repetitions == 2);

    card = review_card(card, SrsRating::Again, day0 + 7);
    REQUIRE(card.repetitions == 0);
    REQUIRE(card.interval_days == 1);
    REQUIRE(card.due_epoch_day == day0 + 8);

    for (int i = 0; i < 20; ++i) {
      card = review_card(card, SrsRating::Again, day0 + 10 + i);
      REQUIRE(card.ease_factor >= kMinEaseFactor);
    }
    REQUIRE(card.ease_factor == kMinEaseFactor);
  }
}

TEST_CASE("SrsDeck supports CRUD, due queue ordering, and reviewing cards by ID", "[srs]") {
  const std::int64_t today = 20050;
  SrsDeck deck;

  const std::string id1 = deck.add_card(SrsCard{
      .id = {},
      .piece_name = "Gate of Steiner",
      .section_name = "Intro",
      .chord_roadmap_hint = "Dm -> Bb -> F -> C",
      .due_epoch_day = today,
  });
  const std::string id2 = deck.add_card(SrsCard{
      .id = {},
      .piece_name = "Jane Doe",
      .section_name = "Chorus",
      .chord_roadmap_hint = "Am -> F -> C -> G",
      .due_epoch_day = today - 2, // Overdue
  });
  const std::string id3 = deck.add_card(SrsCard{
      .id = {},
      .piece_name = "Clair de Lune",
      .section_name = "Opening",
      .chord_roadmap_hint = "Dbmaj7 -> Fm7",
      .due_epoch_day = today + 3, // Not yet due
  });

  REQUIRE_FALSE(id1.empty());
  REQUIRE(deck.cards().size() == 3);
  REQUIRE(deck.due_count(today) == 2);

  const auto due = deck.due_cards(today);
  REQUIRE(due.size() == 2);
  REQUIRE(due[0].id == id2); // Most overdue first
  REQUIRE(due[1].id == id1);

  // Edit existing card
  SrsCard edited = *deck.find_card(id1);
  edited.section_name = "Phrase A";
  REQUIRE(deck.update_card(edited));
  REQUIRE(deck.find_card(id1)->section_name == "Phrase A");

  // Review due card id2 as Good -> moves out of today's due queue
  const auto reviewed = deck.review_card_by_id(id2, SrsRating::Good, today);
  REQUIRE(reviewed.has_value());
  if (reviewed.has_value()) {
    REQUIRE(reviewed->due_epoch_day == today + 1);
  }
  REQUIRE(deck.due_count(today) == 1);
  REQUIRE(deck.session_reviews() == 1);

  // Remove card id3
  REQUIRE(deck.remove_card(id3));
  REQUIRE(deck.cards().size() == 2);
  REQUIRE_FALSE(deck.remove_card("nonexistent"));
}
