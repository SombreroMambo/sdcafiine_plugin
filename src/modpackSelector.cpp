#include "modpackSelector.h"
#include "globals.h"
#include "utils/input.h"
#include "version.h"
#include <content_redirection/redirection.h>
#include <coreinit/screen.h>
#include <coreinit/thread.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fs/DirList.h>
#include <malloc.h>
#include <map>
#include <memory/mappedmemory.h>
#include <string>
#include <utils/logger.h>
#include <wups/storage.h>

#include <algorithm>
#include <sys/stat.h>
#include <vector>

#define TEXT_SEL(x, text1, text2) ((x) ? (text1) : (text2))

bool ConfirmModpackFiles(const std::string &basePath, const std::string &modpackName);

uint8_t *screenBuffer_0 = nullptr;
uint8_t *screenBuffer_1 = nullptr;

bool ScreenInit() {
    if (screenBuffer_0 != nullptr) {
        // allocated
        return true;
    }
    // Init screen and screen buffers
    OSScreenInit();
    uint32_t screen_buf0_size = OSScreenGetBufferSizeEx(SCREEN_TV);
    uint32_t screen_buf1_size = OSScreenGetBufferSizeEx(SCREEN_DRC);
    screenBuffer_0            = (uint8_t *) MEMAllocFromMappedMemoryForGX2Ex(screen_buf0_size, 0x100);
    screenBuffer_1            = (uint8_t *) MEMAllocFromMappedMemoryForGX2Ex(screen_buf1_size, 0x100);
    if (screenBuffer_0 == nullptr || screenBuffer_1 == nullptr) {
        if (screenBuffer_0) {
            MEMFreeToMappedMemory(screenBuffer_0);
            screenBuffer_0 = nullptr;
        }
        if (screenBuffer_1) {
            MEMFreeToMappedMemory(screenBuffer_1);
            screenBuffer_1 = nullptr;
        }
        DEBUG_FUNCTION_LINE_ERR("Failed to allocate screenBuffer");
        OSFatal("SDCafiine plugin: Failed to allocate screenBuffer.");
        return false;
    }
    OSScreenSetBufferEx(SCREEN_TV, (void *) screenBuffer_0);
    OSScreenSetBufferEx(SCREEN_DRC, (void *) (screenBuffer_1));

    OSScreenEnableEx(SCREEN_TV, 1);
    OSScreenEnableEx(SCREEN_DRC, 1);

    // Clear screens
    OSScreenClearBufferEx(SCREEN_TV, 0);
    OSScreenClearBufferEx(SCREEN_DRC, 0);

    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
    return true;
}

bool ScreenDeInit() {
    OSScreenClearBufferEx(SCREEN_TV, 0);
    OSScreenClearBufferEx(SCREEN_DRC, 0);

    // Flip buffers
    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
    if (screenBuffer_0) {
        MEMFreeToMappedMemory(screenBuffer_0);
        screenBuffer_0 = nullptr;
    }
    if (screenBuffer_1) {
        MEMFreeToMappedMemory(screenBuffer_1);
        screenBuffer_1 = nullptr;
    }
    return true;
}

void HandleMultiModPacks(uint64_t titleID) {
    screenBuffer_0 = nullptr;
    screenBuffer_1 = nullptr;
    char TitleIDString[17];
    snprintf(TitleIDString, 17, "%016llX", titleID);

    std::map<std::string, std::string> modTitlePath;

    std::map<std::string, std::string> mounting_points;

    const std::string modTitleIDPath    = std::string("fs:/vol/external01/wiiu/sdcafiine/").append(TitleIDString);
    const std::string modTitleIDPathOld = std::string("fs:/vol/external01/sdcafiine/").append(TitleIDString);
    DirList modTitleDirList(modTitleIDPath, nullptr, DirList::Dirs);

    modTitleDirList.SortList();

    for (int index = 0; index < modTitleDirList.GetFilecount(); index++) {
        std::string curFile = modTitleDirList.GetFilename(index);

        if (curFile == "." || curFile == "..") {
            continue;
        }

        const std::string &packageName = curFile;
        modTitlePath[packageName]      = (modTitleIDPath + "/").append(curFile);
        DEBUG_FUNCTION_LINE_VERBOSE("Found %s  %s", packageName.c_str(), modTitlePath[packageName].c_str());
    }

    if (modTitlePath.empty()) {
        DIR *dir = opendir(modTitleIDPathOld.c_str());
        if (dir) {
            if (!ScreenInit()) {
                OSFatal("SDCafiine plugin: Please migrate sd:/sdcafiine to sd:/wiiu/sdcafiine.");
            }
            OSScreenClearBufferEx(SCREEN_TV, 0);
            OSScreenClearBufferEx(SCREEN_DRC, 0);
            console_print_pos(-2, -1, "SDCafiine plugin " VERSION_FULL);
            console_print_pos(-2, 2, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            console_print_pos(-2, 3, "!!!         OLD DIRECTORY STRUCTURE DETECTED.           !!!");
            console_print_pos(-2, 4, "!!! Please migrate sd:/sdcafiine to sd:/wiiu/sdcafiine. !!!");
            console_print_pos(-2, 5, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            console_print_pos(-2, 7, "Loading game without mods");
            closedir(dir);

            OSScreenFlipBuffersEx(SCREEN_TV);
            OSScreenFlipBuffersEx(SCREEN_DRC);

            OSSleepTicks(OSMillisecondsToTicks(10000));
            ScreenDeInit();
        }
        return;
    } else if (modTitlePath.size() == 1 && gAutoApplySingleModpack && gSkipPrepareIfSingleModpack) {
        // Skip the "Preparing" countdown, but still show the file list + confirmation.
        KPADInit();
        WPADEnableURCC(true);
        if (!ScreenInit()) {
            KPADShutdown();
            return;
        }
        if (ConfirmModpackFiles(modTitlePath.begin()->second, modTitlePath.begin()->first)) {
            ReplaceContent(modTitlePath.begin()->second, modTitlePath.begin()->first);
        }
        ScreenDeInit();
        KPADShutdown();
        return;
    }

    int selected   = 0;
    int initScreen = 1;
    int x_offset   = -2;

    uint32_t buttonsTriggered;

    VPADStatus vpad_data{};
    VPADReadError vpad_error;
    KPADStatus kpad_data{};
    KPADError kpad_error;

    bool displayAutoSkipOption = modTitlePath.size() == 1;

    int wantToExit = 0;
    int page       = 0;
    int per_page   = displayAutoSkipOption ? 11 : 12;
    int max_pages  = (modTitlePath.size() / per_page) + (modTitlePath.size() % per_page != 0 ? 1 : 0);

    int curState = 0;
    if (gAutoApplySingleModpack && modTitlePath.size() == 1) {
        curState = 1;
    }

    int durationInFrames = 60;
    int frameCounter     = 0;
    KPADInit();
    WPADEnableURCC(true);

    if (!ScreenInit()) {
        return;
    }

    while (true) {
        buttonsTriggered = 0;

        VPADRead(VPAD_CHAN_0, &vpad_data, 1, &vpad_error);
        if (vpad_error == VPAD_READ_SUCCESS) {
            buttonsTriggered = vpad_data.trigger;
        }

        for (int i = 0; i < 4; i++) {
            memset(&kpad_data, 0, sizeof(kpad_data));
            if (KPADReadEx((KPADChan) i, &kpad_data, 1, &kpad_error) > 0) {
                if (kpad_error == KPAD_ERROR_OK && kpad_data.extensionType != 0xFF) {
                    if (kpad_data.extensionType == WPAD_EXT_CORE || kpad_data.extensionType == WPAD_EXT_NUNCHUK) {
                        buttonsTriggered |= remapWiiMoteButtons(kpad_data.trigger);
                    } else {
                        buttonsTriggered |= remapClassicButtons(kpad_data.classic.trigger);
                    }
                }
            }
        }

        if (curState == 1) {
            if (buttonsTriggered & VPAD_BUTTON_MINUS) {
                curState = 0;
                continue;
            }

            if (initScreen) {
                OSScreenClearBufferEx(SCREEN_TV, 0);
                OSScreenClearBufferEx(SCREEN_DRC, 0);
                console_print_pos(x_offset, -1, "SDCafiine plugin " VERSION_FULL);
                console_print_pos(x_offset, 1, "Preparing modpack \"%s\"...", modTitlePath.begin()->first.c_str());
                console_print_pos(x_offset, 3, "Press MINUS to open menu");
                // Flip buffers
                OSScreenFlipBuffersEx(SCREEN_TV);
                OSScreenFlipBuffersEx(SCREEN_DRC);
            }

            if (frameCounter >= durationInFrames) {
                if (ConfirmModpackFiles(modTitlePath.begin()->second, modTitlePath.begin()->first)) {
                    ReplaceContent(modTitlePath.begin()->second, modTitlePath.begin()->first);
                }
                break;
            }

            frameCounter++;
        } else {
            if (buttonsTriggered & VPAD_BUTTON_A) {
                wantToExit = 1;
                initScreen = 1;
            } else if (modTitlePath.size() == 1 && (buttonsTriggered & VPAD_BUTTON_MINUS)) {
                OSScreenClearBufferEx(SCREEN_TV, 0);
                OSScreenClearBufferEx(SCREEN_DRC, 0);

                console_print_pos(x_offset, -1, "SDCafiine plugin " VERSION_FULL);
                console_print_pos(x_offset, 1, "Save settings...");

                // Flip buffers
                OSScreenFlipBuffersEx(SCREEN_TV);
                OSScreenFlipBuffersEx(SCREEN_DRC);

                gAutoApplySingleModpack = !gAutoApplySingleModpack;
                // If the value has changed, we store it in the storage.
                if (WUPSStorageAPI::Store(AUTO_APPLY_SINGLE_MODPACK_STRING, gAutoApplySingleModpack) == WUPS_STORAGE_ERROR_SUCCESS) {
                    if (WUPSStorageAPI::SaveStorage() != WUPS_STORAGE_ERROR_SUCCESS) {
                        DEBUG_FUNCTION_LINE_ERR("Failed to close storage");
                    }
                } else {
                    DEBUG_FUNCTION_LINE_WARN("Failed to save to storage");
                }

                initScreen = 1;
            } else if (buttonsTriggered & VPAD_BUTTON_B) {
                break;
            } else if (buttonsTriggered & VPAD_BUTTON_DOWN) {
                selected++;
                initScreen = 1;
            } else if (buttonsTriggered & VPAD_BUTTON_UP) {
                selected--;
                initScreen = 1;
            } else if (buttonsTriggered & VPAD_BUTTON_L) {
                selected -= per_page;
                initScreen = 1;
            } else if (buttonsTriggered & VPAD_BUTTON_R) {
                selected += per_page;
                initScreen = 1;
            }
            if (selected < 0) {
                selected = 0;
            }
            if (selected >= (int) modTitlePath.size()) {
                selected = modTitlePath.size() - 1;
            }
            page = selected / per_page;

            if (initScreen) {
                OSScreenClearBufferEx(SCREEN_TV, 0);
                OSScreenClearBufferEx(SCREEN_DRC, 0);
                console_print_pos(x_offset, -1, "SDCafiine plugin " VERSION_FULL);
                console_print_pos(x_offset, 1, "Press A to launch a modpack");
                console_print_pos(x_offset, 2, "Press B to launch without a modpack");
                if (modTitlePath.size() == 1) {
                    if (gAutoApplySingleModpack) {
                        console_print_pos(x_offset, 4, "Press MINUS to disable autostart for a single modpack");
                    } else {
                        console_print_pos(x_offset, 4, "Press MINUS to enable autostart for a single modpack");
                    }
                }
                int y_offset = displayAutoSkipOption ? 6 : 4;
                int cur_     = 0;

                for (auto &it : modTitlePath) {
                    std::string key   = it.first;
                    std::string value = it.second;

                    if (wantToExit && cur_ == selected) {
                        if (ConfirmModpackFiles(value, key)) {
                            ReplaceContent(value, key);
                        }
                        break;
                    }

                    if (cur_ >= (page * per_page) && cur_ < ((page + 1) * per_page)) {
                        console_print_pos(x_offset, y_offset++, "%s %s", TEXT_SEL((selected == cur_), "--->", "    "), key.c_str());
                    }
                    cur_++;
                }

                if (wantToExit) { //just in case.
                    break;
                }

                if (max_pages > 0) {
                    console_print_pos(x_offset, 17, "Page %02d/%02d. Press L/R to change page.", page + 1, max_pages);
                }

                // Flip buffers
                OSScreenFlipBuffersEx(SCREEN_TV);
                OSScreenFlipBuffersEx(SCREEN_DRC);

                initScreen = 0;
            }
        }
    }

    ScreenDeInit();

    KPADShutdown();
}

bool ReplaceContentInternal(const std::string &basePath, const std::string &subdir, CRLayerHandle *layerHandle);

bool ReplaceContent(const std::string &basePath, const std::string &modpack) {
    bool contentRes = ReplaceContentInternal(basePath, "content", &gContentLayerHandle);
    bool aocRes     = ReplaceContentInternal(basePath, "aoc", &gAocLayerHandle);

    if (!contentRes && !aocRes) {
        auto screenWasAllocated = screenBuffer_0 != nullptr;

        if (!ScreenInit()) {
            OSFatal("SDCafiine plugin: Failed to apply the modpack.");
        }
        uint32_t sleepTime = 3000;
        DEBUG_FUNCTION_LINE_ERR("Failed to apply the modpack. Starting without mods.");
        OSScreenClearBufferEx(SCREEN_TV, 0);
        OSScreenClearBufferEx(SCREEN_DRC, 0);
        console_print_pos(-2, -1, "SDCafiine plugin " VERSION VERSION_EXTRA);
        console_print_pos(-2, 1, "Failed to apply the modpack. Starting without mods...");
        bool folderExists = false;
        struct stat st {};
        std::string contentPath = (std::string(modpack) + "/content");
        if (stat(contentPath.c_str(), &st) >= 0) {
            folderExists = true;
        } else {
            console_print_pos(-2, 3, "No /vol/content replacement found (%s)", contentPath.c_str());
        }

        std::string aocPath = (std::string(modpack) + "/aoc");
        if (stat(aocPath.c_str(), &st) >= 0) {
            folderExists = true;
        } else {
            console_print_pos(-2, 4, "No /vol/aoc replacement found (%s)", aocPath.c_str());
        }
        if (folderExists) {
            console_print_pos(-2, 6, "ContentRedirection_AddFSLayer failed");
        } else {
            sleepTime = 5000;
            console_print_pos(-2, 6, "You need at least one of the replacements!");

            DIR *dir = opendir(basePath.c_str());
            if (dir) {
                auto res = readdir(dir);
                if (res != nullptr) {
                    sleepTime = 7000;
                    console_print_pos(-2, 8, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
                    console_print_pos(-2, 9, "!!! OLD DIRECTORY STRUCTURE DETECTED.                          !!!");
                    console_print_pos(-2, 10, "!!! Please migrate to the new directory structure.             !!!");
                    console_print_pos(-2, 11, R"(!!! Move the files into the "content" directory of the modpack !!!)");
                    console_print_pos(-2, 12, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
                }
                closedir(dir);
            }
        }

        OSScreenFlipBuffersEx(SCREEN_TV);
        OSScreenFlipBuffersEx(SCREEN_DRC);

        OSSleepTicks(OSMillisecondsToTicks(sleepTime));
        if (!screenWasAllocated) {
            ScreenDeInit();
        }
        return false;
    }
    return true;
}

bool ReplaceContentInternal(const std::string &basePath, const std::string &subdir, CRLayerHandle *layerHandle) {
    std::string layerName = "SDCafiine /vol/" + subdir;
    std::string fullPath  = basePath + "/" + subdir;
    struct stat st {};
    if (stat(fullPath.c_str(), &st) < 0) {
        DEBUG_FUNCTION_LINE_WARN("Skip /vol/%s to %s redirection. Dir does not exist", subdir.c_str(), fullPath.c_str());
        return false;
    }

    auto res = ContentRedirection_AddFSLayer(layerHandle,
                                             layerName.c_str(),
                                             fullPath.c_str(),
                                             subdir == "aoc" ? FS_LAYER_TYPE_AOC_MERGE : FS_LAYER_TYPE_CONTENT_MERGE);
    if (res == CONTENT_REDIRECTION_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE("Redirect /vol/%s to %s", subdir.c_str(), fullPath.c_str());
    } else {
        DEBUG_FUNCTION_LINE_ERR("Failed to redirect /vol/%s to %s", subdir.c_str(), fullPath.c_str());
        return false;
    }
    return true;
}


// ---------------------------------------------------------------------------
// File list + "Is this correct?" confirmation screen
// ---------------------------------------------------------------------------

namespace {
constexpr size_t MAX_LISTED_FILES = 10000; // safety cap for huge modpacks
constexpr int LIST_TOP            = 4;     // first screen row of the file list
constexpr int LIST_ROWS           = 11;    // rows 4..14
constexpr int MAX_PATH_CHARS      = 74;

struct ModFileEntry {
    std::string volPath; // e.g. /vol/content/movie/intro.mp4
    bool hidden;         // true for .deleted_ markers
};

struct HeldState {
    uint32_t vpad    = 0;
    uint32_t kpad[4] = {0, 0, 0, 0};
};

void CollectModFiles(const std::string &dirPath, const std::string &volPath, std::vector<ModFileEntry> &out, size_t &notListed, int depth) {
    if (depth > 32) {
        return;
    }
    DIR *dir = opendir(dirPath.c_str());
    if (!dir) {
        return;
    }
    std::vector<std::string> subDirs;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        // Don't trust d_type on the Wii U SD devoptab: ask the file system directly.
        bool isDir = false;
        struct stat st {};
        std::string fullPath = dirPath + "/" + name;
        if (stat(fullPath.c_str(), &st) == 0) {
            isDir = S_ISDIR(st.st_mode);
        } else {
            isDir = (entry->d_type == DT_DIR);
        }
        if (isDir) {
            subDirs.emplace_back(name);
            continue;
        }
        if (out.size() >= MAX_LISTED_FILES) {
            notListed++;
            continue;
        }
        if (strncmp(name, ".deleted_", 9) == 0 && name[9] != '\0') {
            out.push_back({volPath + "/" + (name + 9), true});
        } else {
            out.push_back({volPath + "/" + name, false});
        }
    }
    closedir(dir);

    for (const auto &sub : subDirs) {
        CollectModFiles(dirPath + "/" + sub, volPath + "/" + sub, out, notListed, depth + 1);
    }
}

// Keeps the end of long paths (the file name) visible.
std::string FitPath(const std::string &path, size_t maxLen) {
    if (path.size() <= maxLen) {
        return path;
    }
    return "..." + path.substr(path.size() - (maxLen - 3));
}

// Returns the buttons currently held on GamePad, Wii Remote, Classic and Pro controllers.
// If a controller has no new sample, its last known state is kept, so no fake presses happen.
uint32_t ReadHeldButtons(HeldState &state) {
    VPADStatus vpad_data{};
    VPADReadError vpad_error;
    VPADRead(VPAD_CHAN_0, &vpad_data, 1, &vpad_error);
    if (vpad_error == VPAD_READ_SUCCESS) {
        state.vpad = vpad_data.hold;
    } else if (vpad_error == VPAD_READ_INVALID_CONTROLLER) {
        state.vpad = 0;
    }

    for (int i = 0; i < 4; i++) {
        KPADStatus kpad_data{};
        KPADError kpad_error;
        if (KPADReadEx((KPADChan) i, &kpad_data, 1, &kpad_error) > 0) {
            if (kpad_error == KPAD_ERROR_OK && kpad_data.extensionType != 0xFF) {
                if (kpad_data.extensionType == WPAD_EXT_CORE || kpad_data.extensionType == WPAD_EXT_NUNCHUK) {
                    state.kpad[i] = remapWiiMoteButtons(kpad_data.hold);
                } else {
                    state.kpad[i] = remapClassicButtons(kpad_data.classic.hold);
                }
            } else {
                state.kpad[i] = 0;
            }
        } else if (kpad_error == KPAD_ERROR_INVALID_CONTROLLER) {
            state.kpad[i] = 0;
        }
    }

    uint32_t held = state.vpad;
    for (unsigned int k : state.kpad) {
        held |= k;
    }
    return held;
}

void FlipScreens() {
    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
}
} // namespace

// Screen and KPAD must already be initialized by the caller.
// Returns true  = X pressed (start WITH the modpack)
//         false = Y pressed (start WITHOUT mods)
bool ConfirmModpackFiles(const std::string &basePath, const std::string &modpackName) {
    const int x = -2;

    // 1) The usual loading text while the SD card is scanned.
    OSScreenClearBufferEx(SCREEN_TV, 0);
    OSScreenClearBufferEx(SCREEN_DRC, 0);
    console_print_pos(x, -1, "SDCafiine plugin " VERSION_FULL);
    console_print_pos(x, 1, "Preparing modpack \"%s\"...", modpackName.c_str());
    console_print_pos(x, 3, "Reading modpack files from the SD card...");
    FlipScreens();

    std::vector<ModFileEntry> files;
    size_t notListed = 0;
    CollectModFiles(basePath + "/content", "/vol/content", files, notListed, 0);
    CollectModFiles(basePath + "/aoc", "/vol/aoc", files, notListed, 0);
    std::sort(files.begin(), files.end(), [](const ModFileEntry &a, const ModFileEntry &b) { return a.volPath < b.volPath; });

    size_t hiddenCount = 0;
    for (const auto &f : files) {
        if (f.hidden) {
            hiddenCount++;
        }
    }
    const int total  = (int) files.size();
    const int maxTop = total > LIST_ROWS ? total - LIST_ROWS : 0;

    // 2) File list + question.
    HeldState state;
    uint32_t prevHeld = ReadHeldButtons(state); // ignore buttons that are already held
    int top           = 0;
    int holdFrames    = 0;
    bool redraw       = true;
    bool result       = false;

    while (true) {
        uint32_t held    = ReadHeldButtons(state);
        uint32_t pressed = held & ~prevHeld;
        prevHeld         = held;

        if (pressed & VPAD_BUTTON_X) {
            result = true;
            break;
        }
        if (pressed & VPAD_BUTTON_Y) {
            result = false;
            break;
        }

        int move = 0;
        if (pressed & VPAD_BUTTON_DOWN) {
            move = 1;
        } else if (pressed & VPAD_BUTTON_UP) {
            move = -1;
        } else if (pressed & VPAD_BUTTON_R) {
            move = LIST_ROWS;
        } else if (pressed & VPAD_BUTTON_L) {
            move = -LIST_ROWS;
        }

        // Hold UP/DOWN to scroll faster.
        if (held & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN)) {
            holdFrames++;
            if (move == 0 && holdFrames > 25 && (holdFrames % 3) == 0) {
                move = (held & VPAD_BUTTON_DOWN) ? 1 : -1;
            }
        } else {
            holdFrames = 0;
        }

        if (move != 0) {
            int newTop = std::clamp(top + move, 0, maxTop);
            if (newTop != top) {
                top    = newTop;
                redraw = true;
            }
        }

        if (redraw) {
            OSScreenClearBufferEx(SCREEN_TV, 0);
            OSScreenClearBufferEx(SCREEN_DRC, 0);
            console_print_pos(x, -1, "SDCafiine plugin " VERSION_FULL);
            console_print_pos(x, 1, "Preparing modpack \"%s\"...", modpackName.c_str());
            console_print_pos(x, 2, "%d file(s) added/replaced, %d file(s) hidden%s:",
                              total - (int) hiddenCount, (int) hiddenCount, notListed ? " (list cut off)" : "");

            if (total == 0) {
                console_print_pos(x, LIST_TOP, "  (no files found in content/ or aoc/)");
            }
            for (int row = 0; row < LIST_ROWS && top + row < total; row++) {
                const auto &f = files[top + row];
                if (f.hidden) {
                    console_print_pos(x, LIST_TOP + row, "[hidden] %s", FitPath(f.volPath, MAX_PATH_CHARS - 9).c_str());
                } else {
                    console_print_pos(x, LIST_TOP + row, "  %s", FitPath(f.volPath, MAX_PATH_CHARS - 2).c_str());
                }
            }

            if (total > LIST_ROWS) {
                console_print_pos(x, 15, "Files %d-%d of %d   UP/DOWN scroll, L/R page",
                                  top + 1, std::min(top + LIST_ROWS, total), total);
            }
            if (notListed) {
                console_print_pos(x, 15, "Showing first %d, %d more not listed. UP/DOWN, L/R",
                                  total, (int) notListed);
            }
            console_print_pos(x, 16, "Is this correct?");
            console_print_pos(x, 17, "X = Yes (with mods)   Y = No (without mods)   [Wiimote: 2/1]");
            FlipScreens();
            redraw = false;
        }

        OSSleepTicks(OSMillisecondsToTicks(16));
    }

    OSScreenClearBufferEx(SCREEN_TV, 0);
    OSScreenClearBufferEx(SCREEN_DRC, 0);
    console_print_pos(x, -1, "SDCafiine plugin " VERSION_FULL);
    console_print_pos(x, 1, result ? "Applying modpack \"%s\"..." : "Starting without mods...", modpackName.c_str());
    FlipScreens();

    return result;
}

void console_print_pos(int x, int y, const char *format, ...) {
    char *tmp = nullptr;

    va_list va;
    va_start(va, format);
    if ((vasprintf(&tmp, format, va) >= 0) && tmp) {
        if (strlen(tmp) > 79) {
            tmp[79] = 0;
        }

        OSScreenPutFontEx(SCREEN_TV, x, y, tmp);
        OSScreenPutFontEx(SCREEN_DRC, x, y, tmp);
    }
    va_end(va);

    if (tmp) {
        free(tmp);
    }
}
