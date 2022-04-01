#include "string.hpp"
#include <nn/act/client_cpp.h>
#include <coreinit/thread.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/log.h>
extern "C" {
#include "savemng.h"
}
using namespace std;

#define IO_MAX_FILE_BUFFER (1024 * 1024) // 1 MB

int fsaFd = -1;
char *p1;
Account *wiiuacc;
Account *sdacc;
uint8_t wiiuaccn = 0, sdaccn = 5;

VPADStatus vpad_status;
VPADReadError vpad_error;

KPADStatus kpad[4], kpad_status;

static char *newlibToFSA(char *path) {
    if (path[3] == ':') {
        switch (path[0]) {
            case 'u':
                path = replace_str(path, (char *) "usb:", (char *) "/vol/storage_usb01");
                break;
            case 'm':
                path = replace_str(path, (char *) "mlc:", (char *) "/vol/storage_mlc01");
                break;
            case 's':
                path = replace_str(path, (char *) "slc:", (char *) "/vol/storage_slccmpt01");
                break;
        }
    }
    return path;
}

void setFSAFD(int fd) {
    fsaFd = fd;
}

void show_file_operation(const char *file_name, const char *file_src, const char *file_dest) {
    console_print_pos(-2, 0, "Copying file: %s", file_name);
    console_print_pos_multiline(-2, 2, '/', "From: %s", file_src);
    console_print_pos_multiline(-2, 8, '/', "To: %s", file_dest);
}

int FSAR(int result) {
    if ((result & 0xFFFF0000) == 0xFFFC0000)
        return (result & 0xFFFF) | 0xFFFF0000;
    else
        return result;
}

int32_t loadFile(const char *fPath, uint8_t **buf) {
    int ret    = 0;
    FILE *file = fopen(fPath, "rb");
    if (file != NULL) {
        struct stat st;
        stat(fPath, &st);
        int size = st.st_size;

        *buf = (uint8_t *) malloc(size);
        if (*buf) {
            if (fread(*buf, size, 1, file) == 1)
                ret = size;
            else
                free(*buf);
        }
        fclose(file);
    }
    return ret;
}

int32_t loadFilePart(const char *fPath, uint32_t start, uint32_t size, uint8_t **buf) {
    int ret    = 0;
    FILE *file = fopen(fPath, "rb");
    if (file != NULL) {
        struct stat st;
        stat(fPath, &st);
        if ((start + size) > st.st_size) {
            fclose(file);
            return -43;
        }
        if (fseek(file, start, SEEK_SET) == -1) {
            fclose(file);
            return -43;
        }

        *buf = (uint8_t *) malloc(size);
        if (*buf) {
            if (fread(*buf, size, 1, file) == 1)
                ret = size;
            else
                free(*buf);
        }
        fclose(file);
    }
    return ret;
}

int32_t loadTitleIcon(Title *title) {
    uint32_t highID = title->highID, lowID = title->lowID;
    bool isUSB = title->isTitleOnUSB, isWii = ((highID & 0xFFFFFFF0) == 0x00010000);
    char path[256];

    if (isWii) {
        if (title->saveInit) {
            sprintf(path, "slc:/title/%08x/%08x/data/banner.bin", highID, lowID);
            return loadFilePart(path, 0xA0, 24576, &title->iconBuf);
        }
    } else {
        if (title->saveInit)
            sprintf(path, "%s:/usr/save/%08x/%08x/meta/iconTex.tga", isUSB ? "usb" : "mlc", highID, lowID);
        else
            sprintf(path, "%s:/usr/title/%08x/%08x/meta/iconTex.tga", isUSB ? "usb" : "mlc", highID, lowID);

        return loadFile(path, &title->iconBuf);
    }
    return -23;
}

int checkEntry(const char *fPath) {
    struct stat st;
    if (stat(fPath, &st) == -1)
        return 0;

    if (S_ISDIR(st.st_mode)) return 2;
    return 1;
}

int folderEmpty(const char *fPath) {
    DIR *dir = opendir(fPath);
    if (dir == NULL)
        return -1;

    int c = 0;
    struct dirent *data;
    while ((data = readdir(dir)) != NULL) {
        if (++c > 2)
            break;
    }

    closedir(dir);
    return c < 3 ? 1 : 0;
}

int createFolder(const char *fPath) { //Adapted from mkdir_p made by JonathonReinhart
    const size_t len = strlen(fPath);
    char _path[PATH_MAX];
    char *p;
    int found = 0;

    if (len > sizeof(_path) - 1) {
        return -1;
    }
    strcpy(_path, fPath);

    for (p = _path + 1; *p; p++) {
        if (*p == '/') {
            found++;
            if (found > 2) {
                *p = '\0';
                if (checkEntry(_path) == 0) {
                    if (mkdir(_path, DEFFILEMODE) == -1) return -1;
                }
                *p = '/';
            }
        }
    }

    if (checkEntry(_path) == 0) {
        if (mkdir(_path, DEFFILEMODE) == -1) return -1;
    }

    return 0;
}

void console_print_pos_aligned(int y, uint16_t offset, uint8_t align, const char *format, ...) {
    char *tmp = NULL;
    int x     = 0;

    va_list va;
    va_start(va, format);
    if ((vasprintf(&tmp, format, va) >= 0) && tmp) {
        switch (align) {
            case 0:
                x = (offset * 12);
                break;
            case 1:
                x = (853 - ttfStringWidth(tmp, -2)) / 2;
                break;
            case 2:
                x = 853 - (offset * 12) - ttfStringWidth(tmp, 0);
                break;
            default:
                x = (853 - ttfStringWidth(tmp, -2)) / 2;
                break;
        }
        ttfPrintString(x, (y + 1) * 24, tmp, false, false);
    }
    va_end(va);
    if (tmp) free(tmp);
}

void console_print_pos(int x, int y, const char *format, ...) { // Source: ftpiiu
    char *tmp = NULL;

    va_list va;
    va_start(va, format);
    vasprintf(&tmp, format, va);
    ttfPrintString((x + 4) * 12, (y + 1) * 24, tmp, false, true);
    va_end(va);
    if (tmp) free(tmp);
}

void console_print_pos_multiline(int x, int y, char cdiv, const char *format, ...) { // Source: ftpiiu
    char *tmp    = NULL;
    uint32_t len = (66 - x);

    va_list va;
    va_start(va, format);
    if ((vasprintf(&tmp, format, va) >= 0) && tmp) {

        if ((uint32_t) (ttfStringWidth(tmp, -1) / 12) > len) {
            char *p = tmp;
            if (strrchr(p, '\n') != NULL) p = strrchr(p, '\n') + 1;
            while ((uint32_t) (ttfStringWidth(p, -1) / 12) > len) {
                char *q = p;
                int l1  = strlen(q);
                for (int i = l1; i > 0; i--) {
                    char o = q[l1];
                    q[l1]  = '\0';
                    if ((uint32_t) (ttfStringWidth(p, -1) / 12) <= len) {
                        if (strrchr(p, cdiv) != NULL) p = strrchr(p, cdiv) + 1;
                        else
                            p = q + l1;
                        q[l1] = o;
                        break;
                    }
                    q[l1] = o;
                    l1--;
                }
                char buf[255];
                strcpy(buf, p);
                sprintf(p, "\n%s", buf);
                p++;
                len = 69;
            }
        }
        ttfPrintString((x + 4) * 12, (y + 1) * 24, tmp, true, true);
    }
    va_end(va);
    if (tmp) free(tmp);
}

void console_print_pos_va(int x, int y, const char *format, va_list va) { // Source: ftpiiu
    char *tmp = NULL;

    if ((vasprintf(&tmp, format, va) >= 0) && tmp) {
        ttfPrintString((x + 4) * 12, (y + 1) * 24, tmp, false, true);
    }
    if (tmp) free(tmp);
}

bool promptConfirm(Style st, const char *question) {
    clearBuffers();
    WHBLogFreetypeDraw();
    const char *msg1 = "\ue000 Yes - \ue001 No";
    const char *msg2 = "\ue000 Confirm - \ue001 Cancel";
    const char *msg;
    switch (st & 0x0F) {
        case ST_YES_NO:
            msg = msg1;
            break;
        case ST_CONFIRM_CANCEL:
            msg = msg2;
            break;
        default:
            msg = msg2;
    }
    if (st & ST_WARNING) {
        OSScreenClearBufferEx(SCREEN_TV, 0x7F7F0000);
        OSScreenClearBufferEx(SCREEN_DRC, 0x7F7F0000);
    } else if (st & ST_ERROR) {
        OSScreenClearBufferEx(SCREEN_TV, 0x7F000000);
        OSScreenClearBufferEx(SCREEN_DRC, 0x7F000000);
    } else {
        OSScreenClearBufferEx(SCREEN_TV, 0x007F0000);
        OSScreenClearBufferEx(SCREEN_DRC, 0x007F0000);
    }
    if (st & ST_MULTILINE) {

    } else {
        console_print_pos(31 - (ttfStringWidth((char *) question, 0) / 24), 7, question);
        console_print_pos(31 - (ttfStringWidth((char *) msg, -1) / 24), 9, msg);
    }
    int ret = 0;
    flipBuffers();
    WHBLogFreetypeDraw();
    sleep(0.1);
    while (1) {
        VPADRead(VPAD_CHAN_0, &vpad_status, 1, &vpad_error);
        for (int i = 0; i < 4; i++) {
            WPADExtensionType controllerType;
            // check if the controller is connected
            if (WPADProbe((WPADChan) i, &controllerType) != 0)
                continue;

            KPADRead((WPADChan) i, &(kpad[i]), 1);
            kpad_status = kpad[i];
        }
        if ((vpad_status.trigger & (VPAD_BUTTON_A)) | (kpad_status.trigger & (WPAD_BUTTON_A)) | (kpad_status.classic.trigger & (WPAD_CLASSIC_BUTTON_A)) | (kpad_status.pro.trigger & (WPAD_PRO_BUTTON_A))) {
            ret = 1;
            break;
        }
        if ((vpad_status.trigger & (VPAD_BUTTON_B)) | (kpad_status.trigger & (WPAD_BUTTON_B)) | (kpad_status.classic.trigger & (WPAD_CLASSIC_BUTTON_B)) | (kpad_status.pro.trigger & (WPAD_PRO_BUTTON_B))) {
            ret = 0;
            break;
        }
    }
    return ret;
}

void promptError(const char *message, ...) {
    clearBuffers();
    WHBLogFreetypeDraw();
    va_list va;
    va_start(va, message);
    OSScreenClearBufferEx(SCREEN_TV, 0x7F000000);
    OSScreenClearBufferEx(SCREEN_DRC, 0x7F000000);
    char *tmp = NULL;
    if ((vasprintf(&tmp, message, va) >= 0) && tmp) {
        int x = 31 - (ttfStringWidth(tmp, -2) / 24), y = 8;
        x = (x < -4 ? -4 : x);
        ttfPrintString((x + 4) * 12, (y + 1) * 24, tmp, true, false);
    }
    if (tmp) free(tmp);
    flipBuffers();
    WHBLogFreetypeDraw();
    va_end(va);
    sleep(2);
}

void getAccountsWiiU() {
    /* get persistent ID - thanks to Maschell */
    nn::act::Initialize();
    int i = 0, accn = 0;
    wiiuaccn = nn::act::GetNumOfAccounts();
    wiiuacc  = (Account *) malloc(wiiuaccn * sizeof(Account));
    uint16_t out[11];
    while ((accn < wiiuaccn) && (i <= 12)) {
        if (nn::act::IsSlotOccupied(i)) {
            unsigned int persistentID = nn::act::GetPersistentIdEx(i);
            wiiuacc[accn].pID         = persistentID;
            sprintf(wiiuacc[accn].persistentID, "%08X", persistentID);
            nn::act::GetMiiNameEx((int16_t *) out, i);
            memset(wiiuacc[accn].miiName, 0, sizeof(wiiuacc[accn].miiName));
            for (int j = 0, k = 0; j < 10; j++) {
                if (out[j] < 0x80)
                    wiiuacc[accn].miiName[k++] = (char) out[j];
                else if ((out[j] & 0xF000) > 0) {
                    wiiuacc[accn].miiName[k++] = 0xE0 | ((out[j] & 0xF000) >> 12);
                    wiiuacc[accn].miiName[k++] = 0x80 | ((out[j] & 0xFC0) >> 6);
                    wiiuacc[accn].miiName[k++] = 0x80 | (out[j] & 0x3F);
                } else if (out[j] < 0x400) {
                    wiiuacc[accn].miiName[k++] = 0xC0 | ((out[j] & 0x3C0) >> 6);
                    wiiuacc[accn].miiName[k++] = 0x80 | (out[j] & 0x3F);
                } else {
                    wiiuacc[accn].miiName[k++] = 0xD0 | ((out[j] & 0x3C0) >> 6);
                    wiiuacc[accn].miiName[k++] = 0x80 | (out[j] & 0x3F);
                }
            }
            wiiuacc[accn].slot = i;
            accn++;
        }
        i++;
    }
    nn::act::Finalize();
}

void getAccountsSD(Title *title, uint8_t slot) {
    uint32_t highID = title->highID, lowID = title->lowID;
    sdaccn = 0;
    if (sdacc) free(sdacc);

    char path[255];
    sprintf(path, "sd:/wiiu/backups/%08x%08x/%u", highID, lowID, slot);
    DIR *dir = opendir(path);
    if (dir != NULL) {
        struct dirent *data;
        while ((data = readdir(dir)) != NULL) {
            if (data->d_name[0] == '.' || strncmp(data->d_name, "common", 6) == 0) continue;
            sdaccn++;
        }
        closedir(dir);
    }

    sdacc = (Account *) malloc(sdaccn * sizeof(Account));
    dir   = opendir(path);
    if (dir != NULL) {
        struct dirent *data;
        int i = 0;
        while ((data = readdir(dir)) != NULL) {
            if (data->d_name[0] == '.' || strncmp(data->d_name, "common", 6) == 0) continue;
            sprintf(sdacc[i].persistentID, "%s", data->d_name);
            sdacc[i].pID  = strtoul(data->d_name, NULL, 16);
            sdacc[i].slot = i;
            i++;
        }
        closedir(dir);
    }
}

int DumpFile(char *pPath, char *oPath) {
    WHBLogPrint("In DumpFile");
    FILE *source = fopen(pPath, "rb");
    if (source == NULL)
        return -1;

    FILE *dest = fopen(oPath, "wb");
    if (dest == NULL) {
        fclose(source);
        return -1;
    }

    char *buffer[3];
    for (int i = 0; i < 3; i++) {
        buffer[i] = (char *) aligned_alloc(0x40, IO_MAX_FILE_BUFFER);
        if (buffer[i] == NULL) {
            fclose(source);
            fclose(dest);
            for (i--; i >= 0; i--)
                free(buffer[i]);

            return -1;
        }
    }

    setvbuf(source, buffer[0], _IOFBF, IO_MAX_FILE_BUFFER);
    setvbuf(dest, buffer[1], _IOFBF, IO_MAX_FILE_BUFFER);
    struct stat st;
    if(stat(pPath, &st) < 0) return -1;
    int sizef          = st.st_size;
    size_t sizew          = 0, size;
    uint32_t passedMs  = 1;
    uint64_t startTime = OSGetTime();
    size_t bytesWritten = 0;

    WHBLogPrintf("before fread");
    while ((size = fread(buffer[2], 1, IO_MAX_FILE_BUFFER, source)) > 0) {
        bytesWritten = fwrite(buffer[2], 1, size, dest);
        WHBLogPrint("alive");
        if(bytesWritten < size) {
            WHBLogPrint("error");
            promptError("Write %d,%s", bytesWritten, oPath);
            fclose(source);
            fclose(dest);
            for (int i = 0; i < 3; i++)
                free(buffer[i]);
            return -1;
        }
        passedMs = (uint32_t) OSTicksToMilliseconds(OSGetTime() - startTime);
        if (passedMs == 0)
            passedMs = 1; // avoid 0 div
        OSScreenClearBufferEx(SCREEN_TV, 0);
        OSScreenClearBufferEx(SCREEN_DRC, 0);
        sizew += size;
        show_file_operation(basename(pPath), pPath, oPath);
        console_print_pos(-2, 15, "Bytes Copied: %d of %d (%i kB/s)", sizew, sizef, (uint32_t) (((uint64_t) sizew * 1000) / ((uint64_t) 1024 * passedMs)));
        flipBuffers();
        WHBLogFreetypeDraw();
    }
    WHBLogPrintf("after while");
    fclose(source);
    WHBLogPrintf("after close source");
    fclose(dest);
    WHBLogPrintf("after close dest");
    for (int i = 0; i < 3; i++){
        free(buffer[i]);
        WHBLogPrintf("after close buffer %i", i); }

    IOSUHAX_FSA_ChangeMode(fsaFd, newlibToFSA(oPath), 0x666);
    WHBLogPrintf("after changemode");

    return 0;
}

int DumpDir(char *pPath, const char *tPath) { // Source: ft2sd
    DIR *dir = opendir(pPath);
    if (dir == NULL)
        return -1;

    mkdir(tPath, DEFFILEMODE);
    struct dirent *data = (dirent*)malloc(sizeof(dirent));

    size_t len = strlen(pPath);

    while ((data = readdir(dir)) != NULL) {
        OSScreenClearBufferEx(SCREEN_TV, 0);
        OSScreenClearBufferEx(SCREEN_DRC, 0);

        if (strcmp(data->d_name, "..") == 0 || strcmp(data->d_name, ".") == 0) continue;

        snprintf(pPath + len, PATH_MAX - len, "/%s", data->d_name);
        char *targetPath = (char *) malloc(PATH_MAX);
        snprintf(targetPath, PATH_MAX, "%s/%s", tPath, data->d_name);

        if (data->d_type & DT_DIR) {
            mkdir(targetPath, DEFFILEMODE);
            if (DumpDir(pPath, targetPath) != 0) {
                closedir(dir);
                free(targetPath);
                return -2;
            }
        } else {
            p1 = data->d_name;
            show_file_operation(data->d_name, pPath, targetPath);

            if (DumpFile(pPath, targetPath) != 0) {
                closedir(dir);
                free(targetPath);
                return -3;
            }
        }

        free(targetPath);
        pPath[len] = 0;
    }

    closedir(dir);

    return 0;
}

int DeleteDir(char *pPath) {
    DIR *dir = opendir(pPath);
    if (dir == NULL)
        return -1;

    struct dirent *data;

    while ((data = readdir(dir)) != NULL) {
        OSScreenClearBufferEx(SCREEN_TV, 0);
        OSScreenClearBufferEx(SCREEN_DRC, 0);

        if (strcmp(data->d_name, "..") == 0 || strcmp(data->d_name, ".") == 0) continue;

        int len = strlen(pPath);
        snprintf(pPath + len, PATH_MAX - len, "/%s", data->d_name);

        if (data->d_type & DT_DIR) {
            char origPath[PATH_SIZE];
            sprintf(origPath, "%s", pPath);
            DeleteDir(pPath);

            OSScreenClearBufferEx(SCREEN_TV, 0);
            OSScreenClearBufferEx(SCREEN_DRC, 0);

            console_print_pos(-2, 0, "Deleting folder %s", data->d_name);
            console_print_pos_multiline(-2, 2, '/', "From: \n%s", origPath);
            if (unlink(origPath) == -1) promptError("Failed to delete folder %s\n%s", origPath, strerror(errno));
        } else {
            console_print_pos(-2, 0, "Deleting file %s", data->d_name);
            console_print_pos_multiline(-2, 2, '/', "From: \n%s", pPath);
            if (unlink(pPath) == -1) promptError("Failed to delete file %s\n%s", pPath, strerror(errno));
        }

        flipBuffers();
        WHBLogFreetypeDraw();
        pPath[len] = 0;
    }

    closedir(dir);
    return 0;
}

void getUserID(char *out) { // Source: loadiine_gx2
    /* get persistent ID - thanks to Maschell */
    nn::act::Initialize();

    unsigned char slotno      = nn::act::GetSlotNo();
    unsigned int persistentID = nn::act::GetPersistentIdEx(slotno);
    nn::act::Finalize();

    sprintf(out, "%08X", persistentID);
}

int getLoadiineGameSaveDir(char *out, const char *productCode) {
    DIR *dir = opendir("sd:/wiiu/saves");

    if (dir == NULL) return -1;

    struct dirent *data;
    while ((data = readdir(dir)) != NULL) {
        if ((data->d_type & DT_DIR) && (strstr(data->d_name, productCode) != NULL)) {
            sprintf(out, "sd:/wiiu/saves/%s", data->d_name);
            closedir(dir);
            return 0;
        }
    }

    promptError("Loadiine game folder not found.");
    closedir(dir);
    return -2;
}

int getLoadiineSaveVersionList(int *out, const char *gamePath) {
    DIR *dir = opendir(gamePath);

    if (dir == NULL) {
        promptError("Loadiine game folder not found.");
        return -1;
    }

    int i = 0;
    struct dirent *data;
    while (i < 255 && (data = readdir(dir)) != NULL) {
        if ((data->d_type & DT_DIR) && (strchr(data->d_name, 'v') != NULL)) {
            out[++i] = strtol((data->d_name) + 1, NULL, 10);
        }
    }

    closedir(dir);
    return 0;
}

int getLoadiineUserDir(char *out, const char *fullSavePath, const char *userID) {
    DIR *dir = opendir(fullSavePath);

    if (dir == NULL) {
        promptError("Failed to open Loadiine game save directory.");
        return -1;
    }

    struct dirent *data;
    while ((data = readdir(dir)) != NULL) {
        if ((data->d_type & DT_DIR) && (strstr(data->d_name, userID))) {
            sprintf(out, "%s/%s", fullSavePath, data->d_name);
            closedir(dir);
            return 0;
        }
    }

    sprintf(out, "%s/u", fullSavePath);
    closedir(dir);
    if (checkEntry(out) <= 0) return -1;
    return 0;
}

bool isSlotEmpty(uint32_t highID, uint32_t lowID, uint8_t slot) {
    char path[PATH_SIZE];
    if (((highID & 0xFFFFFFF0) == 0x00010000) && (slot == 255)) {
        sprintf(path, "sd:/savegames/%08x%08x", highID, lowID);
    } else {
        sprintf(path, "sd:/wiiu/backups/%08x%08x/%u", highID, lowID, slot);
    }
    int ret = checkEntry(path);
    if (ret <= 0) return 1;
    else
        return 0;
}

int getEmptySlot(uint32_t highID, uint32_t lowID) {
    for (int i = 0; i < 256; i++) {
        if (isSlotEmpty(highID, lowID, i)) return i;
    }
    return -1;
}

bool hasAccountSave(Title *title, bool inSD, bool iine, uint32_t user, uint8_t slot, int version) {
    uint32_t highID = title->highID, lowID = title->lowID;
    bool isUSB = title->isTitleOnUSB, isWii = ((highID & 0xFFFFFFF0) == 0x00010000);
    if (highID == 0 || lowID == 0) return false;

    char srcPath[PATH_SIZE];
    if (!isWii) {
        if (!inSD) {
            const char *path = (isUSB ? "usb:/usr/save" : "mlc:/usr/save");
            if (user == 0)
                sprintf(srcPath, "%s/%08x/%08x/%s/common", path, highID, lowID, "user");
            else if (user == 0xFFFFFFFF)
                sprintf(srcPath, "%s/%08x/%08x/%s", path, highID, lowID, "user");
            else
                sprintf(srcPath, "%s/%08x/%08x/%s/%08X", path, highID, lowID, "user", user);
        } else {
            if (!iine)
                sprintf(srcPath, "sd:/wiiu/backups/%08x%08x/%u/%08X", highID, lowID, slot, user);
            else {
                if (getLoadiineGameSaveDir(srcPath, title->productCode) != 0) return false;
                if (version) sprintf(srcPath + strlen(srcPath), "/v%u", version);
                if (user == 0) {
                    uint32_t srcOffset = strlen(srcPath);
                    strcpy(srcPath + srcOffset, "/c\0");
                } else {
                    char usrPath[16];
                    sprintf(usrPath, "%08X", user);
                    getLoadiineUserDir(srcPath, srcPath, usrPath);
                }
            }
        }
    } else {
        if (!inSD) {
            sprintf(srcPath, "slc:/title/%08x/%08x/data", highID, lowID);
        } else {
            sprintf(srcPath, "sd:/wiiu/backups/%08x%08x/%u", highID, lowID, slot);
        }
    }
    if (checkEntry(srcPath) == 2)
        if (folderEmpty(srcPath) == 0)
            return true;
    return false;
}

bool hasCommonSave(Title *title, bool inSD, bool iine, uint8_t slot, int version) {
    uint32_t highID = title->highID, lowID = title->lowID;
    bool isUSB = title->isTitleOnUSB, isWii = ((highID & 0xFFFFFFF0) == 0x00010000);
    if (isWii) return false;

    char srcPath[PATH_SIZE];
    if (!inSD) {
        const char *path = (isUSB ? "usb:/usr/save" : "mlc:/usr/save");
        sprintf(srcPath, "%s/%08x/%08x/%s/common", path, highID, lowID, "user");
    } else {
        if (!iine)
            sprintf(srcPath, "sd:/wiiu/backups/%08x%08x/%u/common", highID, lowID, slot);
        else {
            if (getLoadiineGameSaveDir(srcPath, title->productCode) != 0) return false;
            if (version) sprintf(srcPath + strlen(srcPath), "/v%u", version);
            uint32_t srcOffset = strlen(srcPath);
            strcpy(srcPath + srcOffset, "/c\0");
        }
    }
    if (checkEntry(srcPath) == 2)
        if (folderEmpty(srcPath) == 0) return true;
    return false;
}

void copySavedata(Title *title, Title *titleb, int8_t allusers, int8_t allusers_d, bool common) {

    uint32_t highID = title->highID, lowID = title->lowID;
    bool isUSB       = title->isTitleOnUSB;
    uint32_t highIDb = titleb->highID, lowIDb = titleb->lowID;
    bool isUSBb = titleb->isTitleOnUSB;

    if (!promptConfirm(ST_WARNING, "Are you sure?")) return;
    int slotb = getEmptySlot(titleb->highID, titleb->lowID);
    if ((slotb >= 0) && promptConfirm(ST_YES_NO, "Backup current savedata first to next empty slot?")) {
        backupSavedata(titleb, slotb, allusers, common);
        promptError("Backup done. Now copying Savedata.");
    }

    char srcPath[PATH_SIZE];
    char dstPath[PATH_SIZE];
    const char *path  = (isUSB ? "usb:/usr/save" : "mlc:/usr/save");
    const char *pathb = (isUSBb ? "usb:/usr/save" : "mlc:/usr/save");
    sprintf(srcPath, "%s/%08x/%08x/%s", path, highID, lowID, "user");
    sprintf(dstPath, "%s/%08x/%08x/%s", pathb, highIDb, lowIDb, "user");
    createFolder(dstPath);

    if (allusers > -1) {
        uint32_t srcOffset = strlen(srcPath);
        uint32_t dstOffset = strlen(dstPath);
        if (common) {
            strcpy(srcPath + srcOffset, "/common");
            strcpy(dstPath + dstOffset, "/common");
            if (DumpDir(srcPath, dstPath) != 0) promptError("Common save not found.");
        }
        sprintf(srcPath + srcOffset, "/%s", wiiuacc[allusers].persistentID);
        sprintf(dstPath + dstOffset, "/%s", wiiuacc[allusers_d].persistentID);
    }

    if (DumpDir(srcPath, dstPath) != 0) promptError("Copy failed.");
}

void backupAllSave(Title *titles, int count, OSCalendarTime *date) {
    OSCalendarTime dateTime;
    if (date) {
        if (date->tm_year == 0) {
            OSTicksToCalendarTime(OSGetTime(), date);
            date->tm_mon++;
        }
        dateTime = (*date);
    } else {
        OSTicksToCalendarTime(OSGetTime(), &dateTime);
        dateTime.tm_mon++;
    }

    char datetime[24];
    sprintf(datetime, "%04d-%02d-%02dT%02d%02d%02d", dateTime.tm_year, dateTime.tm_mon, dateTime.tm_mday, dateTime.tm_hour, dateTime.tm_min, dateTime.tm_sec);
    for (int i = 0; i < count; i++) {
        if (titles[i].highID == 0 || titles[i].lowID == 0 || !titles[i].saveInit) continue;

        uint32_t highID = titles[i].highID, lowID = titles[i].lowID;
        bool isUSB = titles[i].isTitleOnUSB, isWii = ((highID & 0xFFFFFFF0) == 0x00010000);
        char srcPath[PATH_SIZE];
        char dstPath[PATH_SIZE];
        const char *path = (isWii ? "slc:/title" : (isUSB ? "usb:/usr/save" : "mlc:/usr/save"));
        sprintf(srcPath, "%s/%08x/%08x/%s", path, highID, lowID, isWii ? "data" : "user");
        sprintf(dstPath, "sd:/wiiu/backups/batch/%s/%08x%08x", datetime, highID, lowID);

        createFolder(dstPath);
        if (DumpDir(srcPath, dstPath) != 0) promptError("Backup failed.");
    }
}

void backupSavedata(Title *title, uint8_t slot, int8_t allusers, bool common) {

    if (!isSlotEmpty(title->highID, title->lowID, slot) && !promptConfirm(ST_WARNING, "Backup found on this slot. Overwrite it?")) return;
    uint32_t highID = title->highID, lowID = title->lowID;
    bool isUSB = title->isTitleOnUSB, isWii = ((highID & 0xFFFFFFF0) == 0x00010000);
    char srcPath[PATH_SIZE];
    char dstPath[PATH_SIZE];
    const char *path = (isWii ? "slc:/title" : (isUSB ? "usb:/usr/save" : "mlc:/usr/save"));
    sprintf(srcPath, "%s/%08x/%08x/%s", path, highID, lowID, isWii ? "data" : "user");
    if (isWii && (slot == 255)) {
        sprintf(dstPath, "sd:/savegames/%08x%08x", highID, lowID);
    } else {
        sprintf(dstPath, "sd:/wiiu/backups/%08x%08x/%u", highID, lowID, slot);
    }
    createFolder(dstPath);

    if ((allusers > -1) && !isWii) {
        uint32_t srcOffset = strlen(srcPath);
        uint32_t dstOffset = strlen(dstPath);
        if (common) {
            strcpy(srcPath + srcOffset, "/common");
            strcpy(dstPath + dstOffset, "/common");
            if (DumpDir(srcPath, dstPath) != 0) promptError("Common save not found.");
        }
        sprintf(srcPath + srcOffset, "/%s", wiiuacc[allusers].persistentID);
        sprintf(dstPath + dstOffset, "/%s", wiiuacc[allusers].persistentID);
        if (checkEntry(srcPath) == 0) {
            promptError("No save found for this user.");
            return;
        }
    }
    if (DumpDir(srcPath, dstPath) != 0) promptError("Backup failed. DO NOT restore from this slot.");
    OSCalendarTime now;
    OSTicksToCalendarTime(OSGetTime(), &now);
    char date[255];
    sprintf(date, "%d/%d/%d %d:%d", now.tm_mday, now.tm_mon, now.tm_year, now.tm_hour, now.tm_min);
    setSlotDate(title->highID, title->lowID, slot, date);
}

void restoreSavedata(Title *title, uint8_t slot, int8_t sdusers, int8_t allusers, bool common) {

    if (isSlotEmpty(title->highID, title->lowID, slot)) {
        promptError("No backup found on selected slot.");
        return;
    }
    sleep(0.1);
    if (!promptConfirm(ST_WARNING, "Are you sure?")) return;
    int slotb = getEmptySlot(title->highID, title->lowID);
    if ((slotb >= 0) && promptConfirm(ST_YES_NO, "Backup current savedata first to next empty slot?")) backupSavedata(title, slotb, allusers, common);
    uint32_t highID = title->highID, lowID = title->lowID;
    bool isUSB = title->isTitleOnUSB, isWii = ((highID & 0xFFFFFFF0) == 0x00010000);
    char srcPath[PATH_SIZE];
    char dstPath[PATH_SIZE];
    const char *path = (isWii ? "slc:/title" : (isUSB ? "usb:/usr/save" : "mlc:/usr/save"));
    if (isWii && (slot == 255)) {
        sprintf(srcPath, "sd:/savegames/%08x%08x", highID, lowID);
    } else {
        sprintf(srcPath, "sd:/wiiu/backups/%08x%08x/%u", highID, lowID, slot);
    }
    sprintf(dstPath, "%s/%08x/%08x/%s", path, highID, lowID, isWii ? "data" : "user");
    createFolder(dstPath);

    if ((sdusers > -1) && !isWii) {
        uint32_t srcOffset = strlen(srcPath);
        uint32_t dstOffset = strlen(dstPath);
        if (common) {
            strcpy(srcPath + srcOffset, "/common");
            strcpy(dstPath + dstOffset, "/common");
            if (DumpDir(srcPath, dstPath) != 0) promptError("Common save not found.");
        }
        sprintf(srcPath + srcOffset, "/%s", sdacc[sdusers].persistentID);
        sprintf(dstPath + dstOffset, "/%s", wiiuacc[allusers].persistentID);
    }

    if (DumpDir(srcPath, dstPath) != 0) promptError("Restore failed.");
}

void wipeSavedata(Title *title, int8_t allusers, bool common) {

    if (!promptConfirm(ST_WARNING, "Are you sure?") || !promptConfirm(ST_WARNING, "Hm, are you REALLY sure?")) return;
    int slotb = getEmptySlot(title->highID, title->lowID);
    if ((slotb >= 0) && promptConfirm(ST_YES_NO, "Backup current savedata first?")) backupSavedata(title, slotb, allusers, common);
    uint32_t highID = title->highID, lowID = title->lowID;
    bool isUSB = title->isTitleOnUSB, isWii = ((highID & 0xFFFFFFF0) == 0x00010000);
    char srcPath[PATH_SIZE];
    char origPath[PATH_SIZE];
    const char *path = (isWii ? "slc:/title" : (isUSB ? "usb:/usr/save" : "mlc:/usr/save"));
    sprintf(srcPath, "%s/%08x/%08x/%s", path, highID, lowID, isWii ? "data" : "user");
    if ((allusers > -1) && !isWii) {
        uint32_t offset = strlen(srcPath);
        if (common) {
            strcpy(srcPath + offset, "/common");
            sprintf(origPath, "%s", srcPath);
            if (DeleteDir(srcPath) != 0) promptError("Common save not found.");
            if (unlink(origPath) == -1) promptError("Failed to delete common folder.\n%s", strerror(errno));
        }
        sprintf(srcPath + offset, "/%s", wiiuacc[allusers].persistentID);
        sprintf(origPath, "%s", srcPath);
    }

    if (DeleteDir(srcPath) != 0) promptError("Failed to delete savefile.");
    if ((allusers > -1) && !isWii) {
        if (unlink(origPath) == -1) promptError("Failed to delete user folder.\n%s", strerror(errno));
    }
}

void importFromLoadiine(Title *title, bool common, int version) {

    if (!promptConfirm(ST_WARNING, "Are you sure?")) return;
    int slotb = getEmptySlot(title->highID, title->lowID);
    if (slotb >= 0 && promptConfirm(ST_YES_NO, "Backup current savedata first?")) backupSavedata(title, slotb, 0, common);
    uint32_t highID = title->highID, lowID = title->lowID;
    bool isUSB = title->isTitleOnUSB;
    char srcPath[PATH_SIZE];
    char dstPath[PATH_SIZE];
    if (getLoadiineGameSaveDir(srcPath, title->productCode) != 0) return;
    if (version) sprintf(srcPath + strlen(srcPath), "/v%i", version);
    char usrPath[16];
    getUserID(usrPath);
    uint32_t srcOffset = strlen(srcPath);
    getLoadiineUserDir(srcPath, srcPath, usrPath);
    sprintf(dstPath, "%s:/usr/save/%08x/%08x/user", isUSB ? "usb" : "mlc", highID, lowID);
    createFolder(dstPath);
    uint32_t dstOffset = strlen(dstPath);
    sprintf(dstPath + dstOffset, "/%s", usrPath);
    promptError(srcPath);
    promptError(dstPath);
    if (DumpDir(srcPath, dstPath) != 0) promptError("Failed to import savedata from loadiine.");
    if (common) {
        strcpy(srcPath + srcOffset, "/c\0");
        strcpy(dstPath + dstOffset, "/common\0");
        promptError(srcPath);
        promptError(dstPath);
        if (DumpDir(srcPath, dstPath) != 0) promptError("Common save not found.");
    }
}

void exportToLoadiine(Title *title, bool common, int version) {

    if (!promptConfirm(ST_WARNING, "Are you sure?")) return;
    uint32_t highID = title->highID, lowID = title->lowID;
    bool isUSB = title->isTitleOnUSB;
    char srcPath[PATH_SIZE];
    char dstPath[PATH_SIZE];
    if (getLoadiineGameSaveDir(dstPath, title->productCode) != 0) return;
    if (version) sprintf(dstPath + strlen(dstPath), "/v%u", version);
    char usrPath[16];
    getUserID(usrPath);
    uint32_t dstOffset = strlen(dstPath);
    getLoadiineUserDir(dstPath, dstPath, usrPath);
    sprintf(srcPath, "%s:/usr/save/%08x/%08x/user", isUSB ? "usb" : "mlc", highID, lowID);
    uint32_t srcOffset = strlen(srcPath);
    sprintf(srcPath + srcOffset, "/%s", usrPath);
    createFolder(dstPath);
    promptError(srcPath);
    promptError(dstPath);
    if (DumpDir(srcPath, dstPath) != 0) promptError("Failed to export savedata to loadiine.");
    if (common) {
        strcpy(dstPath + dstOffset, "/c\0");
        strcpy(srcPath + srcOffset, "/common\0");
        promptError(srcPath);
        promptError(dstPath);
        if (DumpDir(srcPath, dstPath) != 0) promptError("Common save not found.");
    }
}
