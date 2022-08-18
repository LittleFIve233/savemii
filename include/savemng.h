#pragma once

#define __STDC_WANT_LIB_EXT2__ 1

#include "draw.h"
#include "json.h"
#include "log_freetype.h"
#include <coreinit/filesystem.h>
#include <coreinit/mcp.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <iosuhax.h>
#include <iosuhax_devoptab.h>
#include <padscore/kpad.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vpad/input.h>

#define PATH_SIZE 0x400
#define FS_ALIGN(x)  ((x + 0x3F) & ~(0x3F))

#define SD		"/vol/app_sd/"
#define MLC		"/vol/storage_mlc01/"

extern VPADStatus vpad_status;
extern VPADReadError vpad_error;
extern KPADStatus kpad_status;

extern FSClient *__wut_devoptab_fs_client;

typedef struct {
    uint32_t highID;
    uint32_t lowID;
    uint16_t listID;
    char shortName[256];
    char longName[512];
    char productCode[5];
    bool saveInit;
    bool isTitleOnUSB;
    bool isTitleDupe;
    uint16_t dupeID;
    uint8_t *iconBuf;
} Title;

typedef struct {
    uint32_t highID;
    uint32_t lowID;
    uint8_t dev;
    bool found;
} Saves;

typedef struct {
    char persistentID[9];
    uint32_t pID;
    char miiName[50];
    uint8_t slot;
} Account;

typedef enum {
    ST_YES_NO = 1,
    ST_CONFIRM_CANCEL = 2,
    ST_MULTILINE = 16,
    ST_WARNING = 32,
    ST_ERROR = 64
} Style;

extern Account *wiiuacc;
extern Account *sdacc;
extern uint8_t wiiuaccn, sdaccn;

FSCmdBlock *getCmdBlk();
void console_print_pos(int x, int y, const char *format, ...);
bool promptConfirm(Style st, std::string question);
void promptError(const char *message, ...);
void getAccountsWiiU();
void getAccountsSD(Title *title, uint8_t slot);
bool hasAccountSave(Title *title, bool inSD, bool iine, uint32_t user, uint8_t slot, int version);
int getLoadiineGameSaveDir(char *out, const char *productCode, const char *longName, const uint32_t highID, const uint32_t lowID);
int getLoadiineSaveVersionList(int *out, const char *gamePath);
bool isSlotEmpty(uint32_t highID, uint32_t lowID, uint8_t slot);
bool hasCommonSave(Title *title, bool inSD, bool iine, uint8_t slot, int version);
void copySavedata(Title *title, Title *titled, int8_t allusers, int8_t allusers_d, bool common);
void backupAllSave(Title *titles, int count, OSCalendarTime *date);
void backupSavedata(Title *title, uint8_t slot, int8_t allusers, bool common);
void restoreSavedata(Title *title, uint8_t slot, int8_t sdusers, int8_t allusers, bool common);
void wipeSavedata(Title *title, int8_t allusers, bool common);
void importFromLoadiine(Title *title, bool common, int version);
void exportToLoadiine(Title *title, bool common, int version);
void setFSAFD(int fd);
int checkEntry(const char *fPath);
size_t loadFile(const char *path, uint8_t **buffer);
int32_t loadTitleIcon(Title *title);
void console_print_pos_multiline(int x, int y, char cdiv, const char *format, ...);
void console_print_pos_aligned(int y, uint16_t offset, uint8_t align, const char *format, ...);