#include "core/key_signature.hpp"
#include "core/lesson_engine.hpp"
#include "core/lesson_score.hpp"
#include "core/musicxml_importer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <miniz.h>
#include <string>
#include <utility>
#include <vector>

using namespace selfano::core;

namespace {

[[nodiscard]] std::vector<std::uint8_t>
build_test_mxl_zip(const std::vector<std::pair<std::string, std::string>>& entries) {
  mz_zip_archive zip{};
  REQUIRE(mz_zip_writer_init_heap(&zip, 0, 0) == MZ_TRUE);
  for (const auto& [name, content] : entries) {
    REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(),
                                  MZ_BEST_SPEED) == MZ_TRUE);
  }
  void* ptr = nullptr;
  size_t len = 0;
  REQUIRE(mz_zip_writer_finalize_heap_archive(&zip, &ptr, &len) == MZ_TRUE);
  std::vector<std::uint8_t> out(static_cast<const std::uint8_t*>(ptr),
                                static_cast<const std::uint8_t*>(ptr) + len);
  mz_free(ptr);
  mz_zip_writer_end(&zip);
  return out;
}

constexpr const char* kGrandStaffMusicXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">
<score-partwise version="3.1">
  <work><work-title>Nocturne Sketch in G Minor</work-title></work>
  <identification><creator type="composer">Selfano Test</creator></identification>
  <part-list>
    <score-part id="P1"><part-name>Piano</part-name></score-part>
  </part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>2</divisions>
        <key><fifths>-2</fifths><mode>minor</mode></key>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <staves>2</staves>
        <clef number="1"><sign>G</sign><line>2</line></clef>
        <clef number="2"><sign>F</sign><line>4</line></clef>
      </attributes>
      <direction placement="above">
        <sound tempo="96"/>
      </direction>
      <harmony>
        <root><root-step>G</root-step></root>
        <kind>minor</kind>
      </harmony>
      <note>
        <grace/>
        <pitch><step>F</step><alter>1</alter><octave>4</octave></pitch>
        <voice>1</voice>
        <type>eighth</type>
        <staff>1</staff>
      </note>
      <note>
        <pitch><step>G</step><octave>4</octave></pitch>
        <duration>3</duration>
        <voice>1</voice>
        <type>quarter</type>
        <dot/>
        <staff>1</staff>
        <notations><technical><fingering>2</fingering></technical></notations>
      </note>
      <note>
        <pitch><step>A</step><octave>4</octave></pitch>
        <duration>1</duration>
        <voice>1</voice>
        <type>eighth</type>
        <staff>1</staff>
        <notations><technical><fingering>3</fingering></technical></notations>
      </note>
      <note>
        <pitch><step>B</step><alter>-1</alter><octave>4</octave></pitch>
        <duration>2</duration>
        <voice>1</voice>
        <type>quarter</type>
        <staff>1</staff>
        <notations><technical><fingering>4</fingering></technical></notations>
      </note>
      <note>
        <pitch><step>D</step><octave>5</octave></pitch>
        <duration>2</duration>
        <tie type="start"/>
        <voice>1</voice>
        <type>quarter</type>
        <staff>1</staff>
        <notations><technical><fingering>5</fingering></technical><tied type="start"/></notations>
      </note>
      <backup><duration>8</duration></backup>
      <note>
        <pitch><step>G</step><octave>2</octave></pitch>
        <duration>4</duration>
        <voice>5</voice>
        <type>half</type>
        <staff>2</staff>
        <notations><technical><fingering>5</fingering></technical></notations>
      </note>
      <note>
        <pitch><step>D</step><octave>3</octave></pitch>
        <duration>4</duration>
        <voice>5</voice>
        <type>half</type>
        <staff>2</staff>
        <notations><technical><fingering>2</fingering></technical></notations>
      </note>
      <note>
        <chord/>
        <pitch><step>B</step><alter>-1</alter><octave>3</octave></pitch>
        <duration>4</duration>
        <voice>5</voice>
        <type>half</type>
        <staff>2</staff>
        <notations><technical><fingering>1</fingering></technical></notations>
      </note>
    </measure>
    <measure number="2">
      <harmony>
        <root><root-step>D</root-step></root>
        <kind>dominant</kind>
      </harmony>
      <note>
        <pitch><step>D</step><octave>5</octave></pitch>
        <duration>4</duration>
        <tie type="stop"/>
        <voice>1</voice>
        <type>half</type>
        <staff>1</staff>
        <notations><tied type="stop"/></notations>
      </note>
      <note>
        <pitch><step>C</step><octave>5</octave></pitch>
        <duration>2</duration>
        <voice>1</voice>
        <type>quarter</type>
        <staff>1</staff>
      </note>
      <note>
        <rest/>
        <duration>2</duration>
        <voice>1</voice>
        <type>quarter</type>
        <staff>1</staff>
      </note>
      <backup><duration>8</duration></backup>
      <note>
        <pitch><step>D</step><octave>3</octave></pitch>
        <duration>8</duration>
        <voice>5</voice>
        <type>whole</type>
        <staff>2</staff>
      </note>
      <note>
        <chord/>
        <pitch><step>F</step><alter>1</alter><octave>3</octave></pitch>
        <duration>8</duration>
        <voice>5</voice>
        <type>whole</type>
        <staff>2</staff>
      </note>
    </measure>
  </part>
</score-partwise>
)xml";

[[nodiscard]] std::string make_long_musicxml(int bar_count) {
  std::string xml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <movement-title>Long 32-Bar Study</movement-title>
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">)xml";
  for (int b = 1; b <= bar_count; ++b) {
    xml += "<measure number=\"" + std::to_string(b) + "\">";
    if (b == 1) {
      xml += "<attributes><divisions>1</divisions><key><fifths>0</fifths><mode>major</mode></"
             "key><time><beats>4</beats><beat-type>4</beat-type></time></attributes>";
    }
    xml += "<note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><type>"
           "whole</type></note>";
    xml += "</measure>";
  }
  xml += "</part></score-partwise>";
  return xml;
}

} // namespace

TEST_CASE("MusicXmlImporter parses Grand Staff MusicXML with backup, chord, ties, harmony, and "
          "fingerings",
          "[musicxml_importer]") {
  const auto res = import_musicxml_string(kGrandStaffMusicXml);
  REQUIRE(res.has_value());

  const auto& score = res->score;
  REQUIRE(score.meta.is_imported);
  REQUIRE(score.meta.tier == 0);
  REQUIRE(score.meta.id.starts_with("imp_"));
  REQUIRE(score.meta.title == "Nocturne Sketch in G Minor");
  REQUIRE(score.meta.composer_source == "Selfano Test");
  REQUIRE(score.meta.key.fifths == -2);
  REQUIRE(score.meta.key.mode == Mode::Minor);
  REQUIRE(score.meta.time_signature == TimeSignature{4, 4});
  REQUIRE_THAT(score.meta.target_bpm, Catch::Matchers::WithinAbs(96.0, 0.1));
  REQUIRE(score.measures.size() == 2U);
  REQUIRE(validate_lesson_score(score).has_value());

  // Grace note F#4 imported before dotted quarter G4(2), 8th A4(3), quarter Bb4(4), quarter D5(5)
  const auto& m1 = score.measures[0];
  REQUIRE(m1.treble_events.size() == 5U);
  REQUIRE(m1.treble_events[0].is_grace);
  REQUIRE(m1.treble_events[0].notes[0].midi == 66); // F#4
  REQUIRE(m1.treble_events[1].duration.base == NoteBaseDuration::Quarter);
  REQUIRE(m1.treble_events[1].duration.dotted);
  REQUIRE(m1.treble_events[1].notes[0].finger == 2U);
  REQUIRE(m1.treble_events[4].notes[0].tie_start);

  // Bar 1 bass: half G2, half D3+Bb3 chord
  REQUIRE(m1.bass_events.size() == 2U);
  REQUIRE(m1.bass_events[1].notes.size() == 2U);

  // Harmony converted cleanly and ChordSkeleton mode works
  REQUIRE(m1.harmony.size() == 1U);
  REQUIRE(m1.harmony[0].chord_symbol == "Gm");
  REQUIRE(score.measures[1].harmony.size() == 1U);
  REQUIRE(score.measures[1].harmony[0].chord_symbol == "D7");

  // Bar 2 tie_stop preserved and LessonEngine excludes it from fresh MIDI attacks
  REQUIRE(score.measures[1].treble_events[0].notes[0].tie_stop);
  LessonEngine engine;
  engine.load_score(score, LessonConfig{.practice_mode = LessonPracticeMode::WaitForNote,
                                        .hand_mode = LessonHandMode::RightHand});
  REQUIRE(engine.targets().size() ==
          6U); // G4, grace A4, A4, Bb4, D5 (bar 1), C5 (bar 2) - tied D5 skipped
}

TEST_CASE("MusicXmlImporter unpacks compressed .mxl archives via META-INF/container.xml",
          "[musicxml_importer]") {
  constexpr const char* kContainerXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0">
  <rootfiles>
    <rootfile full-path="scores/nocturne.musicxml" media-type="application/vnd.recordare.musicxml+xml"/>
  </rootfiles>
</container>)xml";

  const auto zip_bytes = build_test_mxl_zip({
      {"META-INF/container.xml", kContainerXml},
      {"scores/nocturne.musicxml", kGrandStaffMusicXml},
  });
  REQUIRE(is_mxl_zip_bytes(zip_bytes));

  const auto res = import_mxl_bytes(zip_bytes);
  REQUIRE(res.has_value());
  REQUIRE(res->score.meta.title == "Nocturne Sketch in G Minor");
  REQUIRE(res->score.measures.size() == 2U);
  REQUIRE(validate_lesson_score(res->score).has_value());
}

TEST_CASE("MusicXmlImporter slices 32-bar single-staff score into four 8-bar phrases",
          "[musicxml_importer]") {
  const std::string xml = make_long_musicxml(32);
  const auto res = import_musicxml_string(xml);
  REQUIRE(res.has_value());
  REQUIRE(res->score.measures.size() == 32U);
  REQUIRE(res->score.phrases.size() == 4U);
  REQUIRE(res->score.phrases[0].start_bar == 0U);
  REQUIRE(res->score.phrases[0].end_bar == 7U);
  REQUIRE(res->score.phrases[3].start_bar == 24U);
  REQUIRE(res->score.phrases[3].end_bar == 31U);
  REQUIRE(validate_lesson_score(res->score).has_value());
}

TEST_CASE("MusicXmlImporter rejects malformed XML, corrupt ZIPs, and oversized payloads",
          "[musicxml_importer]") {
  REQUIRE_FALSE(import_musicxml_string("<not-musicxml>").has_value());
  REQUIRE_FALSE(import_musicxml_string("<<<broken").has_value());

  const std::vector<std::uint8_t> corrupt_zip{'P', 'K', 3, 4, 0xFF, 0x00, 0x13};
  REQUIRE_FALSE(import_mxl_bytes(corrupt_zip).has_value());

  const auto no_xml_zip = build_test_mxl_zip({{"readme.txt", "hello"}});
  REQUIRE_FALSE(import_mxl_bytes(no_xml_zip).has_value());

  // Oversized > 16 MiB compressed guard
  std::vector<std::uint8_t> huge_mxl(kMaxMxlCompressedBytes + 1U, 0U);
  REQUIRE_FALSE(import_mxl_bytes(huge_mxl).has_value());

  // Missing file
  REQUIRE_FALSE(import_musicxml_file("/tmp/nonexistent_selfano_file_12345.musicxml").has_value());
}

TEST_CASE("MusicXmlImporter preserves implicit anacrusis pickup measures", "[musicxml_importer]") {
  constexpr const char* kPickupXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <work><work-title>Pickup Waltz</work-title></work>
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="0" implicit="yes">
      <attributes>
        <divisions>2</divisions>
        <key><fifths>0</fifths><mode>minor</mode></key>
        <time><beats>3</beats><beat-type>4</beat-type></time>
        <staves>2</staves>
      </attributes>
      <note>
        <pitch><step>E</step><octave>4</octave></pitch>
        <duration>2</duration><voice>1</voice><type>quarter</type><staff>1</staff>
      </note>
      <backup><duration>2</duration></backup>
      <note>
        <rest/><duration>2</duration><voice>5</voice><type>quarter</type><staff>2</staff>
      </note>
    </measure>
    <measure number="1">
      <note>
        <pitch><step>A</step><octave>4</octave></pitch>
        <duration>6</duration><voice>1</voice><type>half</type><dot/><staff>1</staff>
      </note>
      <backup><duration>6</duration></backup>
      <note>
        <pitch><step>A</step><octave>2</octave></pitch>
        <duration>6</duration><voice>5</voice><type>half</type><dot/><staff>2</staff>
      </note>
    </measure>
  </part>
</score-partwise>)xml";

  const auto res = import_musicxml_string(kPickupXml);
  REQUIRE(res.has_value());
  REQUIRE(res->score.measures.size() == 2U);
  REQUIRE(res->score.measures[0].bar_number == 0U);
  REQUIRE(res->score.measures[0].pickup_ticks == 480);
  REQUIRE(res->score.measures[1].bar_number == 1U);
  REQUIRE_FALSE(res->score.measures[1].pickup_ticks.has_value());
  REQUIRE(validate_lesson_score(res->score).has_value());
}

TEST_CASE("MusicXmlImporter imports expressive tempo words, dynamics, hairpins, articulations, "
          "slurs, and pedals",
          "[musicxml_importer]") {
  constexpr const char* kExpressiveXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <work><work-title>Expressive Prelude</work-title></work>
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>1</divisions>
        <key><fifths>0</fifths><mode>major</mode></key>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <staves>2</staves>
      </attributes>
      <direction placement="above">
        <direction-type><words>Andante cantabile</words></direction-type>
        <direction-type><dynamics><mp/></dynamics></direction-type>
      </direction>
      <direction placement="above">
        <direction-type><wedge type="crescendo"/></direction-type>
      </direction>
      <direction placement="below">
        <direction-type><pedal type="start"/></direction-type>
      </direction>
      <note>
        <pitch><step>C</step><octave>5</octave></pitch>
        <duration>1</duration><voice>1</voice><type>quarter</type><staff>1</staff>
        <notations>
          <slur type="start" number="1"/>
          <articulations><tenuto/></articulations>
        </notations>
      </note>
      <note>
        <pitch><step>E</step><octave>5</octave></pitch>
        <duration>1</duration><voice>1</voice><type>quarter</type><staff>1</staff>
        <notations><articulations><staccato/></articulations></notations>
      </note>
      <note>
        <pitch><step>G</step><octave>5</octave></pitch>
        <duration>1</duration><voice>1</voice><type>quarter</type><staff>1</staff>
        <notations><articulations><accent/></articulations></notations>
      </note>
      <note>
        <pitch><step>C</step><octave>5</octave></pitch>
        <duration>1</duration><voice>1</voice><type>quarter</type><staff>1</staff>
        <notations>
          <slur type="stop" number="1"/>
          <arpeggiate/>
        </notations>
      </note>
      <note>
        <chord/>
        <pitch><step>E</step><octave>5</octave></pitch>
        <duration>1</duration><voice>1</voice><type>quarter</type><staff>1</staff>
      </note>
      <direction placement="above">
        <direction-type><wedge type="stop"/></direction-type>
      </direction>
      <direction placement="above">
        <direction-type><dynamics><f/></dynamics></direction-type>
      </direction>
      <direction placement="below">
        <direction-type><pedal type="stop"/></direction-type>
      </direction>
      <backup><duration>4</duration></backup>
      <note>
        <pitch><step>C</step><octave>3</octave></pitch>
        <duration>4</duration><voice>5</voice><type>whole</type><staff>2</staff>
      </note>
    </measure>
  </part>
</score-partwise>)xml";

  const auto res = import_musicxml_string(kExpressiveXml);
  REQUIRE(res.has_value());
  const auto& sc = res->score;
  REQUIRE(validate_lesson_score(sc).has_value());
  REQUIRE(sc.meta.tempo_marking == "Andante cantabile");
  REQUIRE(sc.measures.size() == 1U);
  REQUIRE(sc.measures[0].dynamics.size() == 2U);
  REQUIRE(sc.measures[0].dynamics[0].level == DynamicLevel::MP);
  REQUIRE(sc.measures[0].dynamics[1].level == DynamicLevel::F);
  REQUIRE(sc.measures[0].hairpins.size() == 1U);
  REQUIRE(sc.measures[0].hairpins[0].kind == HairpinKind::Crescendo);
  REQUIRE(sc.slurs.size() == 1U);
  REQUIRE(sc.slurs[0].treble);
  REQUIRE(sc.pedals.size() == 1U);

  const auto& tr = sc.measures[0].treble_events;
  REQUIRE(tr.size() == 4U);
  REQUIRE(has_articulation(tr[0].articulations, ArticulationFlag::Tenuto));
  REQUIRE(has_articulation(tr[1].articulations, ArticulationFlag::Staccato));
  REQUIRE(has_articulation(tr[2].articulations, ArticulationFlag::Accent));
  REQUIRE(has_articulation(tr[3].articulations, ArticulationFlag::Arpeggiate));
}

TEST_CASE("MusicXmlImporter imports 32nd notes, double dots, triplets, 2-against-3, quintuplets, "
          "and sextuplets",
          "[musicxml_importer][phase24]") {
  // divisions = 120 -> quarter = 120, 8th = 60, 8th triplet = 40, 16th = 30, 32nd = 15,
  // 16th sextuplet = 20, 16th quintuplet (use divisions = 600 -> quarter = 600, 16th quint = 120)
  constexpr const char* kRomanticXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <work><work-title>Romantic Tuplet &amp; 32nd Fixture</work-title></work>
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>120</divisions>
        <key><fifths>-2</fifths><mode>minor</mode></key>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <staves>2</staves>
      </attributes>
      <!-- Beat 1: 8th triplet (40 each = 160 ticks) -->
      <note>
        <pitch><step>G</step><octave>4</octave></pitch>
        <duration>40</duration><voice>1</voice><type>eighth</type><staff>1</staff>
        <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>
        <notations><tuplet type="start"/></notations>
      </note>
      <note>
        <pitch><step>A</step><octave>4</octave></pitch>
        <duration>40</duration><voice>1</voice><type>eighth</type><staff>1</staff>
        <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>
      </note>
      <note>
        <pitch><step>B</step><alter>-1</alter><octave>4</octave></pitch>
        <duration>40</duration><voice>1</voice><type>eighth</type><staff>1</staff>
        <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>
        <notations><tuplet type="stop"/></notations>
      </note>
      <!-- Beat 2-3: double-dotted quarter (210 div = 840 ticks) + 16th (30 div = 120 ticks) -->
      <note>
        <pitch><step>D</step><octave>5</octave></pitch>
        <duration>210</duration><voice>1</voice><type>quarter</type><dot/><dot/><staff>1</staff>
      </note>
      <note>
        <pitch><step>C</step><octave>5</octave></pitch>
        <duration>30</duration><voice>1</voice><type>16th</type><staff>1</staff>
      </note>
      <!-- Beat 4: four 32nds (15 div = 60 ticks each) + 8th (60 div = 240 ticks) -->
      <note><pitch><step>B</step><alter>-1</alter><octave>4</octave></pitch><duration>15</duration><voice>1</voice><type>32nd</type><staff>1</staff></note>
      <note><pitch><step>A</step><octave>4</octave></pitch><duration>15</duration><voice>1</voice><type>32nd</type><staff>1</staff></note>
      <note><pitch><step>G</step><octave>4</octave></pitch><duration>15</duration><voice>1</voice><type>32nd</type><staff>1</staff></note>
      <note><pitch><step>F</step><alter>1</alter><octave>4</octave></pitch><duration>15</duration><voice>1</voice><type>32nd</type><staff>1</staff></note>
      <note><pitch><step>G</step><octave>4</octave></pitch><duration>60</duration><voice>1</voice><type>eighth</type><staff>1</staff></note>
      <backup><duration>480</duration></backup>
      <!-- Bass: 2-against-3 (two 8ths at 0, 240) + sextuplet (six 16ths = 80 each) + half -->
      <note><pitch><step>G</step><octave>2</octave></pitch><duration>60</duration><voice>5</voice><type>eighth</type><staff>2</staff></note>
      <note><pitch><step>D</step><octave>3</octave></pitch><duration>60</duration><voice>5</voice><type>eighth</type><staff>2</staff></note>
      <note>
        <pitch><step>G</step><octave>3</octave></pitch><duration>20</duration><voice>5</voice><type>16th</type><staff>2</staff>
        <time-modification><actual-notes>6</actual-notes><normal-notes>4</normal-notes></time-modification>
        <notations><tuplet type="start"/></notations>
      </note>
      <note>
        <pitch><step>A</step><octave>3</octave></pitch><duration>20</duration><voice>5</voice><type>16th</type><staff>2</staff>
        <time-modification><actual-notes>6</actual-notes><normal-notes>4</normal-notes></time-modification>
      </note>
      <note>
        <pitch><step>B</step><alter>-1</alter><octave>3</octave></pitch><duration>20</duration><voice>5</voice><type>16th</type><staff>2</staff>
        <time-modification><actual-notes>6</actual-notes><normal-notes>4</normal-notes></time-modification>
      </note>
      <note>
        <pitch><step>C</step><octave>4</octave></pitch><duration>20</duration><voice>5</voice><type>16th</type><staff>2</staff>
        <time-modification><actual-notes>6</actual-notes><normal-notes>4</normal-notes></time-modification>
      </note>
      <note>
        <pitch><step>B</step><alter>-1</alter><octave>3</octave></pitch><duration>20</duration><voice>5</voice><type>16th</type><staff>2</staff>
        <time-modification><actual-notes>6</actual-notes><normal-notes>4</normal-notes></time-modification>
      </note>
      <note>
        <pitch><step>A</step><octave>3</octave></pitch><duration>20</duration><voice>5</voice><type>16th</type><staff>2</staff>
        <time-modification><actual-notes>6</actual-notes><normal-notes>4</normal-notes></time-modification>
        <notations><tuplet type="stop"/></notations>
      </note>
      <note><pitch><step>G</step><octave>2</octave></pitch><duration>240</duration><voice>5</voice><type>half</type><staff>2</staff></note>
    </measure>
    <measure number="2">
      <attributes><divisions>5</divisions></attributes>
      <!-- Beat 1: 16th quintuplet (1 div each = 96 ticks) + dotted half -->
      <note>
        <pitch><step>C</step><octave>5</octave></pitch><duration>1</duration><voice>1</voice><type>16th</type><staff>1</staff>
        <time-modification><actual-notes>5</actual-notes><normal-notes>4</normal-notes></time-modification>
        <notations><tuplet type="start"/></notations>
      </note>
      <note><pitch><step>D</step><octave>5</octave></pitch><duration>1</duration><voice>1</voice><type>16th</type><staff>1</staff><time-modification><actual-notes>5</actual-notes><normal-notes>4</normal-notes></time-modification></note>
      <note><pitch><step>E</step><alter>-1</alter><octave>5</octave></pitch><duration>1</duration><voice>1</voice><type>16th</type><staff>1</staff><time-modification><actual-notes>5</actual-notes><normal-notes>4</normal-notes></time-modification></note>
      <note><pitch><step>D</step><octave>5</octave></pitch><duration>1</duration><voice>1</voice><type>16th</type><staff>1</staff><time-modification><actual-notes>5</actual-notes><normal-notes>4</normal-notes></time-modification></note>
      <note>
        <pitch><step>C</step><octave>5</octave></pitch><duration>1</duration><voice>1</voice><type>16th</type><staff>1</staff>
        <time-modification><actual-notes>5</actual-notes><normal-notes>4</normal-notes></time-modification>
        <notations><tuplet type="stop"/></notations>
      </note>
      <note><pitch><step>B</step><alter>-1</alter><octave>4</octave></pitch><duration>15</duration><voice>1</voice><type>half</type><dot/><staff>1</staff></note>
      <backup><duration>20</duration></backup>
      <note><pitch><step>C</step><octave>3</octave></pitch><duration>20</duration><voice>5</voice><type>whole</type><staff>2</staff></note>
    </measure>
  </part>
</score-partwise>)xml";

  const auto res = import_musicxml_string(kRomanticXml);
  REQUIRE(res.has_value());
  const auto& sc = res->score;
  REQUIRE(validate_lesson_score(sc).has_value());
  REQUIRE(sc.measures.size() == 2U);

  const auto& m1_tr = sc.measures[0].treble_events;
  REQUIRE(m1_tr.size() == 10U);
  // Triplet at ticks 0, 160, 320
  REQUIRE(m1_tr[0].start_tick == 0);
  REQUIRE(m1_tr[0].event_ticks() == 160);
  REQUIRE(m1_tr[0].tuplet.has_value());
  REQUIRE(m1_tr[0].tuplet->actual == 3U);
  REQUIRE(m1_tr[1].start_tick == 160);
  REQUIRE(m1_tr[2].start_tick == 320);
  // Double-dotted quarter = 840 ticks
  REQUIRE(m1_tr[3].start_tick == 480);
  REQUIRE(m1_tr[3].duration.base == NoteBaseDuration::Quarter);
  REQUIRE(m1_tr[3].duration.effective_dots() == 2U);
  REQUIRE(m1_tr[3].event_ticks() == 840);
  // Four 32nds = 60 ticks each
  REQUIRE(m1_tr[5].duration.base == NoteBaseDuration::ThirtySecond);
  REQUIRE(m1_tr[5].event_ticks() == 60);
  REQUIRE(m1_tr[8].start_tick == 1620);

  // Bass 2-against-3 + sextuplet (80 ticks each)
  const auto& m1_bs = sc.measures[0].bass_events;
  REQUIRE(m1_bs.size() == 9U);
  REQUIRE(m1_bs[1].start_tick == 240);
  REQUIRE(m1_bs[2].start_tick == 480);
  REQUIRE(m1_bs[2].event_ticks() == 80);
  REQUIRE(m1_bs[2].tuplet.has_value());
  REQUIRE(m1_bs[2].tuplet->actual == 6U);

  // Measure 2: quintuplet (96 ticks each)
  const auto& m2_tr = sc.measures[1].treble_events;
  REQUIRE(m2_tr.size() == 6U);
  REQUIRE(m2_tr[0].event_ticks() == 96);
  REQUIRE(m2_tr[1].start_tick == 96);
  REQUIRE(m2_tr[0].tuplet.has_value());
  REQUIRE(m2_tr[0].tuplet->actual == 5U);
}

TEST_CASE("MusicXmlImporter imports grace notes, mid-piece clef changes, and 8va/8vb ottava",
          "[musicxml_importer][phase25]") {
  constexpr const char* kGraceClefOttavaXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <work><work-title>Romantic Ornaments &amp; Clefs</work-title></work>
  <part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>2</divisions>
        <key><fifths>0</fifths><mode>major</mode></key>
        <time><beats>4</beats><beat-type>4</beat-type></time>
        <staves>2</staves>
        <clef number="1"><sign>G</sign><line>2</line></clef>
        <clef number="2"><sign>F</sign><line>4</line></clef>
      </attributes>
      <direction placement="above">
        <direction-type><octave-shift type="down" size="8"/></direction-type>
        <staff>1</staff>
      </direction>
      <note>
        <grace slash="yes"/>
        <pitch><step>B</step><octave>5</octave></pitch>
        <voice>1</voice><type>eighth</type><staff>1</staff>
      </note>
      <note>
        <pitch><step>C</step><octave>6</octave></pitch>
        <duration>4</duration><voice>1</voice><type>half</type><staff>1</staff>
      </note>
      <note>
        <pitch><step>E</step><octave>6</octave></pitch>
        <duration>4</duration><voice>1</voice><type>half</type><staff>1</staff>
      </note>
      <direction placement="above">
        <direction-type><octave-shift type="stop" size="8"/></direction-type>
        <staff>1</staff>
      </direction>
      <backup><duration>8</duration></backup>
      <note>
        <pitch><step>C</step><octave>3</octave></pitch>
        <duration>4</duration><voice>5</voice><type>half</type><staff>2</staff>
      </note>
      <attributes>
        <clef number="2"><sign>G</sign><line>2</line></clef>
      </attributes>
      <note>
        <pitch><step>G</step><octave>4</octave></pitch>
        <duration>4</duration><voice>5</voice><type>half</type><staff>2</staff>
      </note>
    </measure>
  </part>
</score-partwise>)xml";

  const auto res = import_musicxml_string(kGraceClefOttavaXml);
  REQUIRE(res.has_value());
  const auto& sc = res->score;
  REQUIRE(validate_lesson_score(sc).has_value());
  REQUIRE(sc.measures.size() == 1U);

  // Grace note imported (not skipped) right before C6
  const auto& tr = sc.measures[0].treble_events;
  REQUIRE(tr.size() == 3U);
  CHECK(tr[0].is_grace);
  CHECK(tr[0].grace_slash);
  CHECK(tr[0].start_tick == 0);
  CHECK(tr[0].notes[0].midi == 83); // B5
  CHECK_FALSE(tr[1].is_grace);
  CHECK(tr[1].notes[0].midi == 84); // C6

  // Mid-bar LH treble clef change at tick 960
  REQUIRE(sc.measures[0].clef_changes.size() == 1U);
  CHECK(sc.measures[0].clef_changes[0].clef == Clef::Treble);
  CHECK_FALSE(sc.measures[0].clef_changes[0].treble_staff);
  CHECK(sc.measures[0].clef_changes[0].start_tick == 960);

  // 8va ottava span (MusicXML down -> octave_shift = +1)
  REQUIRE(sc.ottavas.size() == 1U);
  CHECK(sc.ottavas[0].treble);
  CHECK(sc.ottavas[0].octave_shift == 1);
  CHECK(sc.ottavas[0].start_tick == 0);
  CHECK(sc.ottavas[0].end_tick == 1920);
}
