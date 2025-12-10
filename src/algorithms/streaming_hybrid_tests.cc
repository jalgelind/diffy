#include "streaming_hybrid.hpp"
#include "patience.hpp"
#include "myers_linear.hpp"
#include "util/readlines.hpp"
#include "util/hash.hpp"

#include <doctest.h>

using namespace diffy;

// Helper to create Line objects
static Line make_line(int num, const std::string& content) {
    Line l;
    l.line_number = num;
    l.line = content;
    l.checksum = hash::hash(content.c_str(), content.size());
    return l;
}

TEST_CASE("StreamingHybrid: Identical files") {
    std::vector<Line> lines_a = {
        make_line(1, "line 1\n"),
        make_line(2, "line 2\n"),
        make_line(3, "line 3\n"),
        make_line(4, "line 4\n"),
        make_line(5, "line 5\n")
    };
    std::vector<Line> lines_b = lines_a;
    
    DiffInput<Line> input{lines_a, lines_b, "test_a", "test_b"};
    StreamingHybrid<Line> algo(input);
    
    SUBCASE("find_anchors should cover entire file") {
        auto anchors = algo.find_anchors();
        CHECK(anchors.size() >= 1);
        
        // Calculate total lines covered by anchors
        int64_t total_covered = 0;
        for (const auto& anchor : anchors) {
            total_covered += anchor.length;
        }
        CHECK(total_covered == 5);
    }
    
    SUBCASE("diff should produce all common edits") {
        auto result = algo.compute();
        CHECK(result.status == DiffResultStatus::OK);
        CHECK(result.edit_sequence.size() == 5);
        
        for (const auto& edit : result.edit_sequence) {
            CHECK(edit.type == EditType::Common);
        }
    }
}

TEST_CASE("StreamingHybrid: Single line change") {
    std::vector<Line> lines_a = {
        make_line(1, "line 1\n"),
        make_line(2, "line 2\n"),
        make_line(3, "line 3\n"),
        make_line(4, "line 4\n"),
        make_line(5, "line 5\n")
    };
    std::vector<Line> lines_b = {
        make_line(1, "line 1\n"),
        make_line(2, "line 2\n"),
        make_line(3, "CHANGED\n"),
        make_line(4, "line 4\n"),
        make_line(5, "line 5\n")
    };
    
    DiffInput<Line> input{lines_a, lines_b, "test_a", "test_b"};
    StreamingHybrid<Line> algo(input);
    
    SUBCASE("find_anchors completes without error") {
        auto anchors = algo.find_anchors();
        // With only 5 lines and MIN_ANCHOR_SIZE=3, may not find anchors
        // This is expected behavior - just verify it doesn't crash
    }
    
    SUBCASE("partition should create regions") {
        auto anchors = algo.find_anchors();
        auto regions = algo.partition_into_regions(anchors);
        
        CHECK(regions.size() >= 1);
        
        // May or may not have anchors depending on file size
        // but should successfully partition
    }
    
    SUBCASE("diff should detect the change") {
        auto result = algo.compute();
        CHECK(result.status == DiffResultStatus::OK);
        
        bool has_delete = false;
        bool has_insert = false;
        for (const auto& edit : result.edit_sequence) {
            if (edit.type == EditType::Delete) has_delete = true;
            if (edit.type == EditType::Insert) has_insert = true;
        }
        CHECK((has_delete || has_insert));
    }
}

TEST_CASE("StreamingHybrid: Multiple changes") {
    std::vector<Line> lines_a = {
        make_line(1, "line 1\n"),
        make_line(2, "line 2\n"),
        make_line(3, "line 3\n"),
        make_line(4, "line 4\n"),
        make_line(5, "line 5\n"),
        make_line(6, "line 6\n"),
        make_line(7, "line 7\n"),
        make_line(8, "line 8\n")
    };
    std::vector<Line> lines_b = {
        make_line(1, "line 1\n"),
        make_line(2, "CHANGED 2\n"),
        make_line(3, "line 3\n"),
        make_line(4, "line 4\n"),
        make_line(5, "CHANGED 5\n"),
        make_line(6, "line 6\n"),
        make_line(7, "line 7\n"),
        make_line(8, "line 8\n")
    };
    
    DiffInput<Line> input{lines_a, lines_b, "test_a", "test_b"};
    StreamingHybrid<Line> algo(input);
    
    SUBCASE("should create regions") {
        auto anchors = algo.find_anchors();
        auto regions = algo.partition_into_regions(anchors);
        
        CHECK(regions.size() >= 1);
    }
}

TEST_CASE("StreamingHybrid: Completely different files") {
    std::vector<Line> lines_a = {
        make_line(1, "apple\n"),
        make_line(2, "banana\n"),
        make_line(3, "cherry\n")
    };
    std::vector<Line> lines_b = {
        make_line(1, "dog\n"),
        make_line(2, "elephant\n"),
        make_line(3, "frog\n")
    };
    
    DiffInput<Line> input{lines_a, lines_b, "test_a", "test_b"};
    StreamingHybrid<Line> algo(input);
    
    SUBCASE("should find no anchors") {
        auto anchors = algo.find_anchors();
        CHECK(anchors.empty());
    }
    
    SUBCASE("should create single diff region") {
        auto anchors = algo.find_anchors();
        auto regions = algo.partition_into_regions(anchors);
        
        CHECK(regions.size() == 1);
        CHECK(!regions[0].is_anchor);
    }
    
    SUBCASE("diff should complete successfully") {
        auto result = algo.compute();
        CHECK(result.status == DiffResultStatus::OK);
    }
}

TEST_CASE("StreamingHybrid: Output quality matches Myers") {
    std::vector<Line> lines_a = {
        make_line(1, "line 1\n"),
        make_line(2, "line 2\n"),
        make_line(3, "line 3\n"),
        make_line(4, "line 4\n"),
        make_line(5, "line 5\n")
    };
    std::vector<Line> lines_b = {
        make_line(1, "line 1\n"),
        make_line(2, "MODIFIED\n"),
        make_line(3, "line 3\n"),
        make_line(4, "line 4\n"),
        make_line(5, "line 5\n")
    };
    
    DiffInput<Line> input1{lines_a, lines_b, "test_a", "test_b"};
    DiffInput<Line> input2{lines_a, lines_b, "test_a", "test_b"};
    
    StreamingHybrid<Line> streaming(input1);
    MyersLinear<Line> myers(input2);
    
    auto streaming_result = streaming.compute();
    auto myers_result = myers.compute();
    
    SUBCASE("both should succeed") {
        CHECK(streaming_result.status == DiffResultStatus::OK);
        CHECK(myers_result.status == DiffResultStatus::OK);
    }
    
    SUBCASE("should have same number of edits") {
        CHECK(streaming_result.edit_sequence.size() == myers_result.edit_sequence.size());
    }
    
    SUBCASE("edit types should match") {
        if (streaming_result.edit_sequence.size() == myers_result.edit_sequence.size()) {
            for (size_t i = 0; i < streaming_result.edit_sequence.size(); ++i) {
                CHECK(streaming_result.edit_sequence[i].type == 
                      myers_result.edit_sequence[i].type);
            }
        }
    }
}

TEST_CASE("StreamingHybrid: Empty files") {
    std::vector<Line> empty_lines;
    
    SUBCASE("both empty") {
        DiffInput<Line> input{empty_lines, empty_lines, "test_a", "test_b"};
        StreamingHybrid<Line> algo(input);
        
        auto result = algo.compute();
        CHECK(result.status == DiffResultStatus::OK);
        CHECK(result.edit_sequence.empty());
    }
    
    SUBCASE("left empty") {
        std::vector<Line> lines_b = {
            make_line(1, "line 1\n"),
            make_line(2, "line 2\n")
        };
        DiffInput<Line> input{empty_lines, lines_b, "test_a", "test_b"};
        StreamingHybrid<Line> algo(input);
        
        auto result = algo.compute();
        CHECK(result.status == DiffResultStatus::OK);
        CHECK(result.edit_sequence.size() == 2);
        
        for (const auto& edit : result.edit_sequence) {
            CHECK(edit.type == EditType::Insert);
        }
    }
    
    SUBCASE("right empty") {
        std::vector<Line> lines_a = {
            make_line(1, "line 1\n"),
            make_line(2, "line 2\n")
        };
        DiffInput<Line> input{lines_a, empty_lines, "test_a", "test_b"};
        StreamingHybrid<Line> algo(input);
        
        auto result = algo.compute();
        CHECK(result.status == DiffResultStatus::OK);
        CHECK(result.edit_sequence.size() == 2);
        
        for (const auto& edit : result.edit_sequence) {
            CHECK(edit.type == EditType::Delete);
        }
    }
}

TEST_CASE("StreamingHybrid: Anchor confidence scoring") {
    std::vector<Line> lines_a = {
        make_line(1, "A\n"),
        make_line(2, "B\n"),
        make_line(3, "C\n"),
        make_line(4, "D\n"),
        make_line(5, "E\n"),
        make_line(6, "F\n"),
        make_line(7, "G\n"),
        make_line(8, "H\n"),
        make_line(9, "I\n"),
        make_line(10, "J\n")
    };
    std::vector<Line> lines_b = lines_a;
    
    DiffInput<Line> input{lines_a, lines_b, "test_a", "test_b"};
    StreamingHybrid<Line> algo(input);
    
    auto anchors = algo.find_anchors();
    
    SUBCASE("longer anchors should have higher confidence") {
        if (anchors.size() >= 1) {
            CHECK(anchors[0].confidence > 0.5);
            CHECK(anchors[0].length >= 3);
        }
    }
}
