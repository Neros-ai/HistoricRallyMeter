#ifndef TEST_STAGE_FILE_H
#define TEST_STAGE_FILE_H

#include "test_framework.h"
#include "../stage_file.h"

class TestStageFile {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Stage file");

        suite->addTest("greek example loads", []() {
            const char* text =
                "stage,8\nmode,greek\nunits,m\nfrom,0\nstart,10:42\n"
                "box,to,kph,auto\n"
                "103,520,36,\n104,1310,,\n105,2050,,\n106,2450,49,y\n"
                "107,3100,,\n108,3900,43,y\n109,5210,,\n";
            const StageFile f = parseStageFile(text);
            if (f.stages.size() != 1 || !f.stages[0].loadable()) return false;
            const auto& s = f.stages[0];
            double end = s.slot.first_from_m;
            for (const Segment& seg : s.slot.segments) end += seg.distance_m;
            return s.stage == 8 && s.mode == 0 && s.slot.segments.size() == 7
                && s.slot.first_number == 103 && s.slot.start_time_s == 38520
                && !s.slot.segments[1].changesSpeed && s.slot.segments[1].target_speed_kph == 36
                && s.slot.segments[3].target_speed_kph == 49 && s.slot.segments[3].autoNext
                && end == 5210;
        });

        suite->addTest("comma inside a number is an error", []() {
            const char* text =
                "stage,1\nmode,italian\nunits,km\nduration,3:00:00\n"
                "type,ref,to,kph,time\nnavi,,17,23,,\n";
            const StageFile f = parseStageFile(text);
            return f.stages.size() == 1 && !f.stages[0].loadable()
                && !f.stages[0].errors.empty()
                && f.stages[0].errors[0].find("6 fields") != std::string::npos;
        });

        suite->addTest("semicolon file reads a comma decimal", []() {
            const char* text =
                "stage;1\nmode;italian\nunits;km\nduration;0:31\n"
                "type;ref;to;kph;time\npc;1;0,56;;0:31\n";
            const StageFile f = parseStageFile(text);
            if (f.stages.size() != 1 || !f.stages[0].loadable()) return false;
            return f.stages[0].slot.segments.size() == 1
                && f.stages[0].slot.segments[0].distance_m == 560
                && f.stages[0].slot.segments[0].pc_duration_s == 31;
        });

        suite->addTest("italian file does not fit a greek box", []() {
            RallyState s;
            s.rally_type = 0;
            const StageFile f = parseStageFile("stage,1\nmode,italian\nunits,m\ntype,to\nnavi,10\n");
            const std::string why = uploadRefusal(s, f);
            return stageFileFitsMode(2, 0) == false && stageFileFitsMode(1, 0)
                && stageFileFitsMode(0, 2) == false && why.find("italian") != std::string::npos;
        });

        suite->addTest("overwrite question names the stages", []() {
            return overwriteQuestion({8}) == "stage 8 is already programmed — replace?"
                && overwriteQuestion({8, 9}) == "stages 8 and 9 are already programmed — replace?"
                && overwriteQuestion({8, 9, 12}) == "stages 8, 9 and 12 are already programmed — replace?";
        });

        suite->addTest("quotes in the file text unescape", []() {
            std::string out;
            const bool ok = jsonUnescape("# \\\"SS8\\\"\\r\\nstage,8", &out);
            return ok && out == "# \"SS8\"\r\nstage,8";
        });

        // The check shows the box's own text, distances in the file's own units (RB-WEB-30).
        suite->addTest("check distances read as the file writes them", []() {
            return uploadDistanceText(17230, true) == "17.23" && uploadDistanceText(17235, true) == "17.235"
                && uploadDistanceText(380, true) == "0.38" && uploadDistanceText(0, true) == "0.00"
                && uploadDistanceText(1310, false) == "1,310" && uploadDistanceText(520, false) == "520";
        });

        suite->addTest("check rows and summary are the box's text", []() {
            const char* text =
                "stage,11\nmode,italian\nunits,km\nfrom,0\nstart,8:30\nduration,3:00:00\n"
                "type,ref,to,kph,time\nnavi,,0.38,,\npc,1,0.56,,0:31\nco,2,96.23,,\n";
            const StageFile f = parseStageFile(text);
            RallyState s;
            const std::string j = uploadStateJson("x", s, f, {});
            return j.find("\"km\":true") != std::string::npos
                && j.find("\"start_text\":\"08:30:00\"") != std::string::npos
                && j.find("\"duration_text\":\"3:00:00\"") != std::string::npos
                && j.find("\"avg_text\":\"Average 32.08\"") != std::string::npos
                && j.find("\"to\":\"96.23\"") != std::string::npos
                && j.find("\"dur\":\"0:31\"") != std::string::npos
                && j.find("\"tot\":\"3:00:00\"") != std::string::npos
                && j.find("\"kph\":\"20.90\"") != std::string::npos;
        });

        suite->addTest("greek check in metres groups thousands and shows distance at start", []() {
            const char* text =
                "stage,8\nmode,greek\nunits,m\nfrom,0\nstart,10:42\nstart distance,6000\n"
                "box,to,kph,auto\n103,520,36,\n104,1310,,\n";
            const StageFile f = parseStageFile(text);
            RallyState s;
            const std::string j = uploadStateJson("x", s, f, {});
            return j.find("\"km\":false") != std::string::npos
                && j.find("\"to\":\"1,310\"") != std::string::npos
                && j.find("\"start_text\":\"10:42:00\"") != std::string::npos
                && j.find("\"num\":\"103\"") != std::string::npos
                && j.find("\"start_dist_text\":\"6,000 m\"") != std::string::npos;
        });

        return suite;
    }
};

#endif
