#ifndef STAGE_FILE_H
#define STAGE_FILE_H

// A stage file (greek, surrey or italian) parsed into memory slots. The phone
// and, later, the box USB page both use this, so they cannot disagree.

#include <string>
#include <vector>
#include "rally_state.h"

struct StageFileStage {
    int stage = 0;                 // 1-99, 0 when the number was unreadable
    int mode = -1;                 // 0 greek, 1 surrey, 2 italian; -1 unknown
    RallyState::MemorySlot slot;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool loadable() const { return errors.empty() && stage >= 1 && stage <= 99 && mode >= 0; }
};

struct StageFile {
    std::vector<StageFileStage> stages;
    std::vector<std::string> errors;   // the whole file: too big, empty, unreadable
};

StageFile parseStageFile(const std::string& text);

// An italian file only matches an italian box; greek and surrey match either other type.
bool stageFileFitsMode(int mode, int rally_type);

// "" when the file may load. Otherwise why not.
std::string uploadRefusal(const RallyState& s, const StageFile& f);

// Loadable stages whose memory already holds a stage, ascending.
std::vector<int> uploadOverwrites(const RallyState& s, const StageFile& f);

// "stage 8 is already programmed — replace?" and the two- and three-stage forms.
std::string overwriteQuestion(const std::vector<int>& stages);

// Writes every loadable stage. The caller has already checked uploadRefusal.
// If the stage on show is one of them, `segments` is replaced too.
std::vector<int> applyStageFile(RallyState& s, const StageFile& f);

// The `upload` object the phone draws. `loaded` is the stages just written.
std::string uploadStateJson(const std::string& id, const RallyState& s, const StageFile& f,
                            const std::vector<int>& loaded);

std::string jsonEscape(const std::string& s);

// JSON string value, unescaped. False on a bad escape or a value over 64 KB.
bool jsonUnescape(const std::string& raw, std::string* out);
bool jsonFindEscapedString(const char* json, const char* key, std::string* out);

#ifndef RALLY_NO_GTK
struct AppData;
std::vector<int> stageUploadCommit(AppData* data, const StageFile& f);
#endif

#endif
